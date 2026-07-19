/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/SoundPlayer.h>

#include <math.h>
#include <new>
#include <string.h>

#include <media2/MediaConnection.h>
#include <media2/SimpleMediaNode.h>


namespace {


// One output connection per BSoundPlayer. We don't yet need
// PrepareToConnect / FormatProposal logic — autoconnect via PipeWire
// session manager handles routing.
class _OutputConnection : public BMediaOutput {
public:
	_OutputConnection()
		:
		BMediaConnection(B_MEDIA_OUTPUT, "BSoundPlayer-out"),
		BMediaOutput("BSoundPlayer-out")
	{}
	virtual ~_OutputConnection() {}

protected:
	virtual status_t PrepareToConnect(BMediaFormat* /*format*/) override { return B_OK; }
	virtual status_t FormatProposal(BMediaFormat* /*format*/) override   { return B_OK; }
};


} // anonymous namespace


class BSoundPlayer::Impl {
public:
	Impl(BSoundPlayer* owner, const BMediaFormat& format, const char* name,
		BSoundPlayer::BufferPlayerFunc playFunc,
		BSoundPlayer::EventNotifierFunc notifyFunc, void* cookie);
	~Impl();

	BSoundPlayer* Owner() const { return fOwner; }
	void* Cookie() const        { return fCookie; }
	void  SetCookie(void* c)    { fCookie = c; }

	status_t InitCheck() const { return fInitErr; }

	const BMediaFormat& Format() const { return fFormat; }
	status_t SetFormat(const BMediaFormat& fmt);

	status_t Start();
	void     Stop(bool block, bool flush);
	bool     IsPlaying() const { return fNode != NULL && fNode->IsStarted(); }

	float Volume() const { return fVolume; }
	void  SetVolume(float v);

	bool  HasData() const     { return fHasData; }
	void  SetHasData(bool v)  { fHasData = v; }

	size_t BufferSize() const { return Format().format.u.raw_audio.buffer_size; }

	void SetCallbacks(BSoundPlayer::BufferPlayerFunc playFunc,
		BSoundPlayer::EventNotifierFunc notifyFunc, void* cookie);
	void SetPlayFunc(BSoundPlayer::BufferPlayerFunc f)   { fPlayFunc = f; }
	void SetNotifyFunc(BSoundPlayer::EventNotifierFunc n)   { fNotifyFunc = n; }

private:
	static void _ProcessHook(void* cookie, void* buffer, size_t size,
		uint32 frameCount, const BMediaFormat& format);

	BSoundPlayer*		fOwner;
	BMediaFormat		fFormat;	// preserved for InitCheck-only reads
	BSoundPlayer::BufferPlayerFunc	fPlayFunc;
	BSoundPlayer::EventNotifierFunc		fNotifyFunc;
	void*				fCookie;

	BSimpleMediaNode*	fNode;
	_OutputConnection*	fOutput;
	status_t			fInitErr;
	bool				fHasData;
	float				fVolume;
};


BSoundPlayer::Impl::Impl(BSoundPlayer* owner, const BMediaFormat& format,
	const char* name, BSoundPlayer::BufferPlayerFunc playFunc,
	BSoundPlayer::EventNotifierFunc notifyFunc, void* cookie)
	:
	fOwner(owner),
	fFormat(format),
	fPlayFunc(playFunc),
	fNotifyFunc(notifyFunc),
	fCookie(cookie),
	fNode(NULL),
	fOutput(NULL),
	fInitErr(B_NO_INIT),
	fHasData(false),
	fVolume(1.0f)
{
	fNode = new(std::nothrow) BSimpleMediaNode(
		name != NULL ? name : "BSoundPlayer",
		B_MEDIA_RAW_AUDIO, B_MEDIA_PLAYER);
	if (fNode == NULL) {
		fInitErr = B_NO_MEMORY;
		return;
	}
	if (fNode->InitCheck() != B_OK) {
		fInitErr = fNode->InitCheck();
		return;
	}

	fNode->SetFormat(format);
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


BSoundPlayer::Impl::~Impl()
{
	if (fNode != NULL) {
		fNode->Stop();
		if (fOutput != NULL)
			fNode->UnregisterOutput(fOutput);
	}
	delete fOutput;
	delete fNode;
}


status_t
BSoundPlayer::Impl::SetFormat(const BMediaFormat& fmt)
{
	if (fNode == NULL)
		return B_NO_INIT;
	if (fNode->IsStarted())
		return B_NOT_ALLOWED;
	fFormat = fmt;
	return fNode->SetFormat(fmt);
}


status_t
BSoundPlayer::Impl::Start()
{
	if (fNode == NULL)
		return B_NO_INIT;
	status_t err = fNode->Start();
	if (err != B_OK)
		return err;
	if (fNotifyFunc != NULL)
		fNotifyFunc(fCookie, BSoundPlayer::B_STARTED);
	return B_OK;
}


void
BSoundPlayer::Impl::Stop(bool /*block*/, bool /*flush*/)
{
	if (fNode == NULL || !fNode->IsStarted())
		return;
	fNode->Stop();
	if (fNotifyFunc != NULL)
		fNotifyFunc(fCookie, BSoundPlayer::B_STOPPED);
}


void
BSoundPlayer::Impl::SetVolume(float v)
{
	if (v < 0.0f) v = 0.0f;
	if (v > 1.0f) v = 1.0f;
	fVolume = v;
	// SPA volume control is a stream-level call; until BMediaClient exposes
	// it, gain is applied by the process callback path (or by callers).
}


void
BSoundPlayer::Impl::SetCallbacks(BSoundPlayer::BufferPlayerFunc playFunc,
	BSoundPlayer::EventNotifierFunc notifyFunc, void* cookie)
{
	fPlayFunc   = playFunc;
	fNotifyFunc = notifyFunc;
	fCookie     = cookie;
}


void
BSoundPlayer::Impl::_ProcessHook(void* cookie, void* buffer, size_t size,
	uint32 /*frameCount*/, const BMediaFormat& format)
{
	Impl* self = (Impl*)cookie;
	if (!self->fHasData) {
		memset(buffer, 0, size);
		return;
	}
	if (self->fPlayFunc != NULL) {
		self->fPlayFunc(self->fCookie, buffer, size, format.format.u.raw_audio);
		return;
	}
	// Legacy override path — subclass may have overridden PlayBuffer.
	// Default impl does nothing (silence implicit by memset below).
	memset(buffer, 0, size);
	if (self->fOwner != NULL)
		self->fOwner->PlayBuffer(buffer, size, format.format.u.raw_audio);
}


// #pragma mark - BSoundPlayer public surface


BSoundPlayer::BSoundPlayer(const BMediaFormat* format, const char* name,
	BufferPlayerFunc playFunc, EventNotifierFunc notifyFunc, void* cookie)
	:
	fImpl(NULL)
{
	BMediaFormat fmt;
	if (format != NULL)
		fmt = *format;
	else
		fmt.SetToDefault();
	fImpl = new(std::nothrow) Impl(this, fmt, name, playFunc, notifyFunc, cookie);
}


BSoundPlayer::BSoundPlayer(const char* name, BufferPlayerFunc playFunc,
	EventNotifierFunc notifyFunc, void* cookie)
	:
	fImpl(NULL)
{
	BMediaFormat fmt;
	fmt.SetToDefault();
	fImpl = new(std::nothrow) Impl(this, fmt, name, playFunc, notifyFunc, cookie);
}


// Legacy protected virtuals — default impls are no-ops; subclasses override
// to provide buffer data without using the function-pointer callbacks.
void BSoundPlayer::PlayBuffer(void*, size_t, const media_raw_audio_format&) {}
void BSoundPlayer::Notify(sound_player_notification, ...) {}


void
BSoundPlayer::SetBufferPlayer(BufferPlayerFunc playFunc)
{
	if (fImpl != NULL)
		fImpl->SetPlayFunc(playFunc);
}


void
BSoundPlayer::SetNotifier(EventNotifierFunc notifyFunc)
{
	if (fImpl != NULL)
		fImpl->SetNotifyFunc(notifyFunc);
}


void*
BSoundPlayer::Cookie() const
{
	return fImpl != NULL ? fImpl->Cookie() : NULL;
}


void
BSoundPlayer::SetCookie(void* cookie)
{
	if (fImpl != NULL)
		fImpl->SetCookie(cookie);
}


BSoundPlayer::~BSoundPlayer()
{
	delete fImpl;
}


status_t
BSoundPlayer::InitCheck() const
{
	return fImpl != NULL ? fImpl->InitCheck() : B_NO_MEMORY;
}


const BMediaFormat&
BSoundPlayer::Format() const
{
	static BMediaFormat sEmpty;
	return fImpl != NULL ? fImpl->Format() : sEmpty;
}


status_t  BSoundPlayer::SetFormat(const BMediaFormat& f) { return fImpl ? fImpl->SetFormat(f) : B_NO_INIT; }
status_t  BSoundPlayer::Start()                          { return fImpl ? fImpl->Start() : B_NO_INIT; }
void      BSoundPlayer::Stop(bool b, bool f)             { if (fImpl) fImpl->Stop(b, f); }
bool      BSoundPlayer::IsPlaying() const                { return fImpl && fImpl->IsPlaying(); }
float     BSoundPlayer::Volume() const                   { return fImpl ? fImpl->Volume() : 0.0f; }
size_t    BSoundPlayer::BufferSize() const               { return fImpl ? fImpl->BufferSize() : 0; }
bool      BSoundPlayer::HasData() const                  { return fImpl && fImpl->HasData(); }
void      BSoundPlayer::SetHasData(bool v)               { if (fImpl) fImpl->SetHasData(v); }
bigtime_t BSoundPlayer::Latency() const                  { return 0; }


status_t
BSoundPlayer::SetVolume(float v)
{
	if (fImpl == NULL)
		return B_NO_INIT;
	fImpl->SetVolume(v);
	return B_OK;
}


status_t
BSoundPlayer::SetVolumeDB(float dB)
{
	return SetVolume(powf(10.0f, dB / 20.0f));
}


float
BSoundPlayer::VolumeDB() const
{
	const float v = Volume();
	if (v <= 0.0f)
		return -INFINITY;
	return 20.0f * log10f(v);
}


void
BSoundPlayer::SetCallbacks(BufferPlayerFunc playFunc,
	EventNotifierFunc notifyFunc, void* cookie)
{
	if (fImpl != NULL)
		fImpl->SetCallbacks(playFunc, notifyFunc, cookie);
}
