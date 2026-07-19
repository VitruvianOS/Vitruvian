/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaPlayer.h>

#include <atomic>
#include <new>
#include <mutex>
#include <string.h>

#include <Message.h>

#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>
#include <media2/SoundPlayer.h>


class BMediaPlayer::Impl {
public:
	Impl();
	~Impl();

	status_t SetTo(const entry_ref* ref);

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

	status_t InitCheck() const { return fInitErr; }

private:
	static void _FillCallback(void* cookie, void* buffer, size_t size,
		const media_raw_audio_format& fmt);

	void _SetState(BMediaPlayer::player_state s);
	void _PostNotification(uint32 what);

	BMediaFile*			fFile;
	BMediaTrack*		fTrack;
	BSoundPlayer*		fPlayer;
	std::mutex			fLock;	// guards fTrack reads + fState transitions
	std::atomic<BMediaPlayer::player_state>	fState;
	std::atomic<bool>	fEOS;
	uint32				fFrameStride;
	bigtime_t			fDuration;
	float				fVolume;
	BMessenger			fTarget;
	bool				fHasTarget;
	status_t			fInitErr;
};


BMediaPlayer::Impl::Impl()
	:
	fFile(NULL),
	fTrack(NULL),
	fPlayer(NULL),
	fState(BMediaPlayer::B_PLAYER_STOPPED),
	fEOS(false),
	fFrameStride(0),
	fDuration(0),
	fVolume(1.0f),
	fHasTarget(false),
	fInitErr(B_NO_INIT)
{
}


BMediaPlayer::Impl::~Impl()
{
	if (fPlayer != NULL) {
		fPlayer->Stop(true, true);
		delete fPlayer;
	}
	delete fFile;	// BMediaFile owns the track
}


status_t
BMediaPlayer::Impl::SetTo(const entry_ref* ref)
{
	std::lock_guard<std::mutex> _(fLock);

	// Tear down any existing playback.
	if (fPlayer != NULL) {
		fPlayer->Stop(true, true);
		delete fPlayer;
		fPlayer = NULL;
	}
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

	fPlayer = new(std::nothrow) BSoundPlayer(&fmt, "BMediaPlayer",
		&Impl::_FillCallback, NULL, this);
	if (fPlayer == NULL)
		return (fInitErr = B_NO_MEMORY);
	if (fPlayer->InitCheck() != B_OK)
		return (fInitErr = fPlayer->InitCheck());

	_PostNotification(BMediaPlayer::B_PLAYER_DURATION_CHANGED);
	fInitErr = B_OK;
	return B_OK;
}


status_t
BMediaPlayer::Impl::Play()
{
	if (fPlayer == NULL)
		return B_NO_INIT;
	const BMediaPlayer::player_state s = fState.load();
	if (s == BMediaPlayer::B_PLAYER_PLAYING)
		return B_OK;
	if (s == BMediaPlayer::B_PLAYER_STOPPED && fEOS.load()) {
		bigtime_t t = 0;
		fTrack->SeekToTime(&t);
		fEOS = false;
	}
	fPlayer->SetHasData(true);
	status_t err = fPlayer->Start();
	if (err != B_OK)
		return err;
	_SetState(BMediaPlayer::B_PLAYER_PLAYING);
	return B_OK;
}


status_t
BMediaPlayer::Impl::Pause()
{
	if (fPlayer == NULL)
		return B_NO_INIT;
	if (fState.load() != BMediaPlayer::B_PLAYER_PLAYING)
		return B_OK;
	fPlayer->SetHasData(false);
	_SetState(BMediaPlayer::B_PLAYER_PAUSED);
	return B_OK;
}


status_t
BMediaPlayer::Impl::Stop()
{
	if (fPlayer == NULL)
		return B_OK;
	fPlayer->Stop(true, true);
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
	if (fPlayer != NULL)
		fPlayer->SetVolume(v);
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
BMediaPlayer::Impl::_FillCallback(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format& /*fmt*/)
{
	Impl* self = (Impl*)cookie;
	if (self->fEOS.load() || self->fTrack == NULL
			|| self->fState.load() != BMediaPlayer::B_PLAYER_PLAYING) {
		memset(buffer, 0, size);
		return;
	}

	int64 wantFrames = size / self->fFrameStride;
	int64 gotFrames  = wantFrames;
	status_t err = self->fTrack->ReadFrames(buffer, &gotFrames);

	if (err == B_LAST_BUFFER_ERROR) {
		self->fEOS = true;
		const size_t got = (size_t)gotFrames * self->fFrameStride;
		if (got < size)
			memset((uint8_t*)buffer + got, 0, size - got);
		// Transition + notify on the RT thread is fine — BMessenger is safe.
		self->_SetState(BMediaPlayer::B_PLAYER_STOPPED);
		self->_PostNotification(BMediaPlayer::B_PLAYER_END_OF_STREAM);
		return;
	}
	if (err != B_OK) {
		memset(buffer, 0, size);
		return;
	}
	if (gotFrames < wantFrames) {
		const size_t got = (size_t)gotFrames * self->fFrameStride;
		memset((uint8_t*)buffer + got, 0, size - got);
	}
}


// #pragma mark - public surface


BMediaPlayer::BMediaPlayer()
	:
	fImpl(new(std::nothrow) Impl())
{
}


BMediaPlayer::BMediaPlayer(const entry_ref* ref)
	:
	fImpl(new(std::nothrow) Impl())
{
	if (fImpl != NULL && ref != NULL)
		fImpl->SetTo(ref);
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
