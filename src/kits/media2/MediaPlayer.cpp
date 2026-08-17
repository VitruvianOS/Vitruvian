/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaPlayer.h>

#include <atomic>
#include <new>
#include <mutex>
#include <string.h>

#include <Message.h>

#include <media2/MediaConnection.h>
#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>
#include <media2/SimpleMediaNode.h>


namespace {


class _OutputConnection : public BMediaOutput {
public:
	_OutputConnection()
		:
		BMediaConnection(B_MEDIA_OUTPUT, "BMediaPlayer-out"),
		BMediaOutput("BMediaPlayer-out")
	{}
	virtual ~_OutputConnection() {}

protected:
	virtual status_t PrepareToConnect(BMediaFormat*) { return B_OK; }
	virtual status_t FormatProposal(BMediaFormat*)   { return B_OK; }
};


}


class BMediaPlayer::Impl {
public:
	Impl(BMediaPlayer* owner);
	~Impl();

	status_t SetTo(const entry_ref* ref);

	const BMediaFormat& Format() const { return fFormat; }
	status_t SetFormat(const BMediaFormat& fmt);

	status_t Play();
	status_t Pause();
	status_t Stop();

	status_t SeekTo(bigtime_t position);
	bigtime_t Position() const;
	bigtime_t Duration() const { return fDuration; }

	BMediaPlayer::player_state State() const { return fState.load(); }
	bool IsPlaying() const { return fState.load() == BMediaPlayer::B_PLAYER_PLAYING; }

	status_t SetVolume(float v);
	float    Volume() const { return fVolume; }

	void     SetTarget(BMessenger target);
	void     SetVideoView(BView* /*view*/) {}	// no-op for now

	status_t SetHooks(BMediaPlayer::BufferPlayerFunc fillFunc,
		BMediaPlayer::EventNotifierFunc notifyFunc, void* cookie);

	bool  HasData() const     { return fHasData; }
	void  SetHasData(bool v)  { fHasData = v; }

	size_t BufferSize() const { return fFormat.format.u.raw_audio.buffer_size; }

	status_t InitCheck() const { return fInitErr; }

private:
	static void _ProcessHook(void* cookie, void* buffer, size_t size,
		uint32 frameCount, const BMediaFormat& format);

	void _FillFromTrack(void* buffer, size_t size,
		const media_raw_audio_format& format);

	void _SetState(BMediaPlayer::player_state s);
	void _PostNotification(uint32 what);

	BMediaPlayer*		fOwner;
	BMediaFormat		fFormat;
	BSimpleMediaNode*	fNode;
	_OutputConnection*	fOutput;
	status_t			fInitErr;
	bool				fHasData;
	float				fVolume;

	BMediaPlayer::BufferPlayerFunc		fFillFunc;
	BMediaPlayer::EventNotifierFunc	fNotifyFunc;
	void*								fCookie;

	BMediaFile*			fFile;
	BMediaTrack*		fTrack;
	std::mutex			fLock;	// guards fTrack reads + fState transitions
	std::atomic<BMediaPlayer::player_state>	fState;
	std::atomic<bool>	fEOS;
	uint32				fFrameStride;
	bigtime_t			fDuration;
	BMessenger			fTarget;
	bool				fHasTarget;
};


BMediaPlayer::Impl::Impl(BMediaPlayer* owner)
	:
	fOwner(owner),
	fFormat(),
	fNode(NULL),
	fOutput(NULL),
	fInitErr(B_NO_INIT),
	fHasData(false),
	fVolume(1.0f),
	fFillFunc(NULL),
	fNotifyFunc(NULL),
	fCookie(NULL),
	fFile(NULL),
	fTrack(NULL),
	fState(BMediaPlayer::B_PLAYER_STOPPED),
	fEOS(false),
	fFrameStride(0),
	fDuration(0),
	fHasTarget(false)
{
	fFormat.SetToDefault();

	fNode = new(std::nothrow) BSimpleMediaNode("BMediaPlayer",
		B_MEDIA_RAW_AUDIO, B_MEDIA_PLAYER);
	if (fNode == NULL) {
		fInitErr = B_NO_MEMORY;
		return;
	}
	if (fNode->InitCheck() != B_OK) {
		fInitErr = fNode->InitCheck();
		return;
	}
	fNode->SetFormat(fFormat);
	fNode->SetProcessHook(&Impl::_ProcessHook, this);

	fOutput = new(std::nothrow) _OutputConnection();
	if (fOutput == NULL) {
		fInitErr = B_NO_MEMORY;
		return;
	}
	if (fNode->RegisterOutput(fOutput) != B_OK) {
		fInitErr = B_ERROR;
		return;
	}
	fInitErr = B_OK;
}


BMediaPlayer::Impl::~Impl()
{
	if (fNode != NULL) {
		fNode->Stop();
		if (fOutput != NULL)
			fNode->UnregisterOutput(fOutput);
	}
	delete fOutput;
	delete fNode;
	delete fFile;	// BMediaFile owns the track
}


status_t
BMediaPlayer::Impl::SetFormat(const BMediaFormat& fmt)
{
	if (fNode == NULL)
		return B_NO_INIT;
	if (fNode->IsStarted())
		return B_NOT_ALLOWED;
	fFormat = fmt;
	return fNode->SetFormat(fmt);
}


status_t
BMediaPlayer::Impl::SetHooks(BMediaPlayer::BufferPlayerFunc fillFunc,
	BMediaPlayer::EventNotifierFunc notifyFunc, void* cookie)
{
	fFillFunc   = fillFunc;
	fNotifyFunc = notifyFunc;
	fCookie     = cookie;
	return B_OK;
}


status_t
BMediaPlayer::Impl::SetTo(const entry_ref* ref)
{
	std::lock_guard<std::mutex> _(fLock);

	// Tear down any existing playback.
	if (fNode != NULL)
		fNode->Stop();
	delete fFile;
	fFile  = NULL;
	fTrack = NULL;
	fEOS   = false;
	fDuration = 0;
	fFrameStride = 0;

	fFile = new(std::nothrow) BMediaFile(ref);
	if (fFile == NULL)
		return (fInitErr = B_NO_MEMORY);
	if (fFile->InitCheck() != B_OK)
		return (fInitErr = fFile->InitCheck());

	fTrack = fFile->TrackAt(0);
	if (fTrack == NULL)
		return (fInitErr = B_ERROR);

	BMediaFormat fmt;
	status_t err = fTrack->DecodedFormat(&fmt);
	if (err != B_OK || !fmt.IsRawAudio())
		return (fInitErr = err != B_OK ? err : B_ERROR);

	const media_raw_audio_format& raw = fmt.format.u.raw_audio;
	fFrameStride = (raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
		* raw.channel_count;
	fDuration = fTrack->Duration();

	err = SetFormat(fmt);
	if (err != B_OK)
		return (fInitErr = err);

	_PostNotification(BMediaPlayer::B_PLAYER_DURATION_CHANGED);
	fInitErr = B_OK;
	return B_OK;
}


status_t
BMediaPlayer::Impl::Play()
{
	if (fNode == NULL)
		return B_NO_INIT;
	const BMediaPlayer::player_state s = fState.load();
	if (s == BMediaPlayer::B_PLAYER_PLAYING)
		return B_OK;
	if (s == BMediaPlayer::B_PLAYER_STOPPED && fEOS.load() && fTrack != NULL) {
		bigtime_t t = 0;
		fTrack->SeekToTime(&t);
		fEOS = false;
	}
	fHasData = true;
	status_t err = fNode->IsStarted() ? B_OK : fNode->Start();
	if (err != B_OK)
		return err;
	if (fNotifyFunc != NULL)
		fNotifyFunc(fCookie, BMediaPlayer::B_STARTED);
	_SetState(BMediaPlayer::B_PLAYER_PLAYING);
	return B_OK;
}


status_t
BMediaPlayer::Impl::Pause()
{
	if (fNode == NULL)
		return B_NO_INIT;
	if (fState.load() != BMediaPlayer::B_PLAYER_PLAYING)
		return B_OK;
	fHasData = false;
	_SetState(BMediaPlayer::B_PLAYER_PAUSED);
	return B_OK;
}


status_t
BMediaPlayer::Impl::Stop()
{
	if (fNode == NULL)
		return B_OK;
	if (fNode->IsStarted()) {
		fNode->Stop();
		if (fNotifyFunc != NULL)
			fNotifyFunc(fCookie, BMediaPlayer::B_STOPPED);
	}
	bigtime_t t = 0;
	if (fTrack != NULL)
		fTrack->SeekToTime(&t);
	fEOS = false;
	_SetState(BMediaPlayer::B_PLAYER_STOPPED);
	return B_OK;
}


status_t
BMediaPlayer::Impl::SeekTo(bigtime_t position)
{
	std::lock_guard<std::mutex> _(fLock);
	if (fTrack == NULL)
		return B_NO_INIT;
	bigtime_t t = position;
	status_t err = fTrack->SeekToTime(&t);
	if (err != B_OK)
		return err;
	fEOS = false;
	_PostNotification(BMediaPlayer::B_PLAYER_POSITION_CHANGED);
	return B_OK;
}


bigtime_t
BMediaPlayer::Impl::Position() const
{
	return fTrack != NULL ? fTrack->CurrentTime() : 0;
}


status_t
BMediaPlayer::Impl::SetVolume(float v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	fVolume = v;
	return B_OK;
}


void
BMediaPlayer::Impl::SetTarget(BMessenger target)
{
	std::lock_guard<std::mutex> _(fLock);
	fTarget    = target;
	fHasTarget = target.IsValid();
}


void
BMediaPlayer::Impl::_SetState(BMediaPlayer::player_state s)
{
	const BMediaPlayer::player_state prev = fState.exchange(s);
	if (prev != s)
		_PostNotification(BMediaPlayer::B_PLAYER_STATE_CHANGED);
}


void
BMediaPlayer::Impl::_PostNotification(uint32 what)
{
	if (!fHasTarget)
		return;
	BMessage msg(what);
	msg.AddInt32("state",    (int32)fState.load());
	msg.AddInt64("duration", fDuration);
	fTarget.SendMessage(&msg);
}


void
BMediaPlayer::Impl::_FillFromTrack(void* buffer, size_t size,
	const media_raw_audio_format&)
{
	if (fEOS.load() || fTrack == NULL
			|| fState.load() != BMediaPlayer::B_PLAYER_PLAYING) {
		memset(buffer, 0, size);
		return;
	}

	int64 wantFrames = size / fFrameStride;
	int64 gotFrames  = wantFrames;
	status_t err = fTrack->ReadFrames(buffer, &gotFrames);

	if (err == B_LAST_BUFFER_ERROR) {
		fEOS = true;
		const size_t got = (size_t)gotFrames * fFrameStride;
		if (got < size)
			memset((uint8_t*)buffer + got, 0, size - got);
		// Transition + notify on the RT thread is fine — BMessenger is safe.
		_SetState(BMediaPlayer::B_PLAYER_STOPPED);
		_PostNotification(BMediaPlayer::B_PLAYER_END_OF_STREAM);
		if (fNotifyFunc != NULL)
			fNotifyFunc(fCookie, BMediaPlayer::B_SOUND_DONE);
		return;
	}
	if (err != B_OK) {
		memset(buffer, 0, size);
		return;
	}
	if (gotFrames < wantFrames) {
		const size_t got = (size_t)gotFrames * fFrameStride;
		memset((uint8_t*)buffer + got, 0, size - got);
	}
}


void
BMediaPlayer::Impl::_ProcessHook(void* cookie, void* buffer, size_t size,
	uint32, const BMediaFormat& format)
{
	Impl* self = (Impl*)cookie;
	const media_raw_audio_format& raw = format.format.u.raw_audio;

	if (!self->fHasData) {
		memset(buffer, 0, size);
		return;
	}
	if (self->fFillFunc != NULL) {
		self->fFillFunc(self->fCookie, buffer, size, raw);
		return;
	}
	if (self->fTrack != NULL) {
		self->_FillFromTrack(buffer, size, raw);
		return;
	}
	memset(buffer, 0, size);
	if (self->fOwner != NULL)
		self->fOwner->BufferReceived(buffer, size, raw);
}


// #pragma mark - public surface


BMediaPlayer::BMediaPlayer()
	:
	fImpl(new(std::nothrow) Impl(this))
{
}


BMediaPlayer::BMediaPlayer(const entry_ref* ref)
	:
	fImpl(new(std::nothrow) Impl(this))
{
	if (fImpl != NULL && ref != NULL)
		fImpl->SetTo(ref);
}


void
BMediaPlayer::BufferReceived(void*, size_t, const media_raw_audio_format&)
{
}


BMediaPlayer::~BMediaPlayer()
{
	delete fImpl;
}


status_t   BMediaPlayer::InitCheck() const  { return fImpl ? fImpl->InitCheck() : B_NO_MEMORY; }
status_t   BMediaPlayer::SetTo(const entry_ref* r) { return fImpl ? fImpl->SetTo(r) : B_NO_MEMORY; }
status_t   BMediaPlayer::Play()             { return fImpl ? fImpl->Play()  : B_NO_INIT; }
status_t   BMediaPlayer::Pause()            { return fImpl ? fImpl->Pause() : B_NO_INIT; }
status_t   BMediaPlayer::Stop()             { return fImpl ? fImpl->Stop()  : B_NO_INIT; }
status_t   BMediaPlayer::SeekTo(bigtime_t p){ return fImpl ? fImpl->SeekTo(p) : B_NO_INIT; }
bigtime_t  BMediaPlayer::Position() const   { return fImpl ? fImpl->Position() : 0; }
bigtime_t  BMediaPlayer::Duration() const   { return fImpl ? fImpl->Duration() : 0; }
BMediaPlayer::player_state BMediaPlayer::State() const { return fImpl ? fImpl->State() : B_PLAYER_STOPPED; }
bool       BMediaPlayer::IsPlaying() const  { return fImpl && fImpl->IsPlaying(); }
status_t   BMediaPlayer::SetVolume(float v) { return fImpl ? fImpl->SetVolume(v) : B_NO_INIT; }
float      BMediaPlayer::Volume() const     { return fImpl ? fImpl->Volume() : 0.0f; }
void       BMediaPlayer::SetTarget(BMessenger t) { if (fImpl) fImpl->SetTarget(t); }
void       BMediaPlayer::SetVideoView(BView* v) { if (fImpl) fImpl->SetVideoView(v); }
bool       BMediaPlayer::HasData() const    { return fImpl && fImpl->HasData(); }
void       BMediaPlayer::SetHasData(bool v) { if (fImpl) fImpl->SetHasData(v); }
size_t     BMediaPlayer::BufferSize() const { return fImpl ? fImpl->BufferSize() : 0; }


const BMediaFormat&
BMediaPlayer::Format() const
{
	static BMediaFormat sEmpty;
	return fImpl != NULL ? fImpl->Format() : sEmpty;
}


status_t
BMediaPlayer::SetFormat(const BMediaFormat& fmt)
{
	return fImpl ? fImpl->SetFormat(fmt) : B_NO_INIT;
}


void
BMediaPlayer::SetCallbacks(FillFunc fillFunc, Notifier notifyFunc, void* cookie)
{
	if (fImpl != NULL)
		fImpl->SetHooks(fillFunc, notifyFunc, cookie);
}


status_t
BMediaPlayer::SetHooks(BufferPlayerFunc fillFunc, EventNotifierFunc notifyFunc,
	void* cookie)
{
	if (fImpl == NULL)
		return B_NO_INIT;
	return fImpl->SetHooks(fillFunc, notifyFunc, cookie);
}
