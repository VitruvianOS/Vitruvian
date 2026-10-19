/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaPlayer.h>

#include <atomic>
#include <new>
#include <mutex>
#include <string.h>

#include <Autolock.h>
#include <Handler.h>
#include <Looper.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Url.h>
#include <OS.h>
#include <View.h>

#include <media2/MediaConnection.h>
#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>
#include <media2/SimpleMediaNode.h>

#include "VideoSync.h"


namespace {


const uint32 kMsgVideoPump = 'vpmp';

// Sync tolerance: against the audio clock a frame late past this is
// dropped and early past it holds the previous frame up.
const bigtime_t kVideoSyncTolerance = 20000;

// The runner only wakes the pump; tick a quarter frame (clamped) when
// the rate is known so a due frame is noticed inside the tolerance.
const bigtime_t kVideoPumpQuantumMax = 10000;
const bigtime_t kVideoPumpQuantumMin = 2000;


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
	status_t SetTo(const BUrl& url);
	status_t _FinishSetTo();

	const BMediaFormat& Format() const { return fFormat; }
	status_t SetFormat(const BMediaFormat& fmt);

	status_t Play();
	status_t Pause();
	status_t Stop();

	status_t SeekTo(bigtime_t position, int32 flags);
	bigtime_t Position() const;
	bigtime_t Duration() const { return fDuration; }

	BMediaPlayer::player_state State() const { return fState.load(); }
	bool IsPlaying() const { return fState.load() == BMediaPlayer::B_PLAYER_PLAYING; }

	status_t SetVolume(float v);
	float    Volume() const { return fVolume; }

	void     SetTarget(BMessenger target);
	void     SetVideoView(BView* view);

	status_t SetHooks(BMediaPlayer::BufferPlayerFunc fillFunc,
		BMediaPlayer::EventNotifierFunc notifyFunc, void* cookie);

	bool  HasData() const     { return fHasData; }
	void  SetHasData(bool v)  { fHasData = v; }

	size_t BufferSize() const { return fFormat.format.u.raw_audio.buffer_size; }

	status_t InitCheck() const { return fInitErr; }

private:
	// Pulls run on the view's looper thread so no backend thread ever
	// touches frame buffers.
	class _VideoPump : public BHandler {
	public:
							_VideoPump(Impl* impl)
								:
								fImpl(impl)
							{}

		virtual void		MessageReceived(BMessage* message);

	private:
		Impl*				fImpl;
	};

	static void _ProcessHook(void* cookie, void* buffer, size_t size,
		uint32 frameCount, const BMediaFormat& format);

	void _FillFromTrack(void* buffer, size_t size,
		const media_raw_audio_format& format);

	void _PumpVideoFrame(bool force = false);
	void _StartVideoRunner();
	void _StopVideoRunner();
	void _ResetVideoPacing();
	void _PostPeaks(const float* samples, int64 frames,
		const media_raw_audio_format& format);
	void _ResetClock(bigtime_t base)
		{ fClockBase = base; fPlayedFrames = 0; }

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
	BMediaTrack*		fVideoTrack;
	BView*				fVideoView;
	BLooper*			fVideoLooper;
	_VideoPump*			fVideoPump;
	BMessageRunner*		fVideoRunner;
	media_raw_video_format	fVideoFormat;
	uint32				fVideoStride;
	uint8*				fVideoBuffer;
	size_t				fVideoBufferSize;
	bool				fVideoFramePending;
	bigtime_t			fVideoPendingTime;
	bigtime_t			fVideoClockBase;
	bool				fVideoClockValid;
	bigtime_t			fVideoLastDeliver;
	int32				fVideoZeroStamps;
	bool				fVideoPaceNominal;
	bigtime_t			fVideoNominalInterval;
	std::mutex			fLock;	// guards tracks, pull buffer, pacing state
	std::mutex			fAudioLock;	// held with fLock by seeks; the audio callback only
	std::atomic<BMediaPlayer::player_state>	fState;
	std::atomic<bool>	fEOS;
	uint32				fFrameStride;
	bigtime_t			fDuration;
	// Played-audio clock: the seek point plus frames handed to the server.
	// The pipeline position runs ahead with unsynced sinks.
	std::atomic<bigtime_t>	fClockBase;
	std::atomic<int64>	fPlayedFrames;
	float				fAudioRate;
	bigtime_t			fLastPeakPost;
	float				fPeaks[8];
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
	fVideoTrack(NULL),
	fVideoView(NULL),
	fVideoLooper(NULL),
	fVideoPump(NULL),
	fVideoRunner(NULL),
	fVideoFormat(),
	fVideoStride(0),
	fVideoBuffer(NULL),
	fVideoBufferSize(0),
	fVideoFramePending(false),
	fVideoPendingTime(0),
	fVideoClockBase(0),
	fVideoClockValid(false),
	fVideoLastDeliver(0),
	fVideoZeroStamps(0),
	fVideoPaceNominal(false),
	fVideoNominalInterval(0),
	fState(BMediaPlayer::B_PLAYER_STOPPED),
	fEOS(false),
	fFrameStride(0),
	fDuration(0),
	fClockBase(0),
	fPlayedFrames(0),
	fAudioRate(0.0f),
	fLastPeakPost(0),
	fHasTarget(false)
{
	memset(fPeaks, 0, sizeof(fPeaks));
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
	_StopVideoRunner();
	if (fVideoPump != NULL) {
		if (fVideoLooper != NULL) {
			// RemoveHandler needs the looper lock; it also waits out
			// any pump still running on that thread.
			BAutolock looperLock(fVideoLooper);
			fVideoLooper->RemoveHandler(fVideoPump);
		}
		delete fVideoPump;
	}
	if (fNode != NULL) {
		fNode->Stop();
		if (fOutput != NULL)
			fNode->UnregisterOutput(fOutput);
	}
	delete fOutput;
	delete fNode;
	delete[] fVideoBuffer;
	delete fFile;	// BMediaFile owns the tracks
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
	// Stop the pump before the track and buffer go away; one already
	// blocked on fLock re-checks them once it wins the lock.
	_StopVideoRunner();
	std::lock_guard<std::mutex> _(fLock);
	std::lock_guard<std::mutex> audio(fAudioLock);

	// Tear down any existing playback.
	if (fNode != NULL)
		fNode->Stop();
	delete fFile;
	fFile  = NULL;
	fTrack = NULL;
	fVideoTrack = NULL;
	fEOS   = false;
	fDuration = 0;
	fFrameStride = 0;
	_ResetClock(0);
	fVideoStride = 0;
	fVideoBufferSize = 0;
	delete[] fVideoBuffer;
	fVideoBuffer = NULL;
	_ResetVideoPacing();

	fFile = new(std::nothrow) BMediaFile(ref);
	if (fFile == NULL)
		return (fInitErr = B_NO_MEMORY);
	if (fFile->InitCheck() != B_OK)
		return (fInitErr = fFile->InitCheck());

	return _FinishSetTo();
}


status_t
BMediaPlayer::Impl::SetTo(const BUrl& url)
{
	std::lock_guard<std::mutex> _(fLock);
	std::lock_guard<std::mutex> audio(fAudioLock);

	if (fNode != NULL)
		fNode->Stop();
	delete fFile;
	fFile  = NULL;
	fTrack = NULL;
	fVideoTrack = NULL;
	fEOS   = false;
	fDuration = 0;
	fFrameStride = 0;
	_ResetClock(0);
	fVideoStride = 0;
	fVideoBufferSize = 0;
	delete[] fVideoBuffer;
	fVideoBuffer = NULL;

	fFile = new(std::nothrow) BMediaFile(url);
	if (fFile == NULL)
		return (fInitErr = B_NO_MEMORY);
	if (fFile->InitCheck() != B_OK)
		return (fInitErr = fFile->InitCheck());

	return _FinishSetTo();
}


status_t
BMediaPlayer::Impl::_FinishSetTo()
{
	fTrack = NULL;
	fVideoTrack = NULL;
	fVideoStride = 0;
	fVideoBufferSize = 0;
	delete[] fVideoBuffer;
	fVideoBuffer = NULL;

	// Discovery stores tracks as encoded; the negotiated decoder output
	// is what ReadFrames delivers through the union overlay.
	BMediaFormat audioFmt;
	BMediaFormat videoFmt;
	const int32 count = fFile != NULL ? fFile->CountTracks() : 0;
	for (int32 i = 0; i < count; i++) {
		BMediaTrack* track = fFile->TrackAt(i);
		if (track == NULL)
			continue;
		BMediaFormat fmt;
		if (track->DecodedFormat(&fmt) != B_OK)
			continue;
		if (fmt.IsEncodedAudio()) {
			fmt.format.type = B_MEDIA_RAW_AUDIO;
			fmt.format.u.raw_audio = fmt.format.u.encoded_audio.output;
		} else if (fmt.IsEncodedVideo()) {
			fmt.format.type = B_MEDIA_RAW_VIDEO;
			fmt.format.u.raw_video = fmt.format.u.encoded_video.output;
		}
		if (fmt.IsRawAudio() && fTrack == NULL) {
			fTrack = track;
			audioFmt = fmt;
		} else if (fmt.IsRawVideo() && fVideoTrack == NULL) {
			fVideoTrack = track;
			videoFmt = fmt;
		}
		if (fTrack != NULL && fVideoTrack != NULL)
			break;
	}

	if (fTrack == NULL && fVideoTrack == NULL)
		return (fInitErr = B_ERROR);

	status_t err;
	if (fTrack != NULL) {
		const media_raw_audio_format& raw = audioFmt.format.u.raw_audio;
		fFrameStride = (raw.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
			* raw.channel_count;
		fAudioRate = raw.frame_rate;
		fDuration = fTrack->Duration();
		err = SetFormat(audioFmt);
	} else {
		// Video-only: run the node as a silence clock so the playhead
		// advances while the view paces its own frame pulls.
		BMediaFormat fmt;
		fmt.SetToDefault();
		err = SetFormat(fmt);
	}
	if (err != B_OK)
		return (fInitErr = err);

	if (fVideoTrack != NULL) {
		const media_raw_video_format& rv = videoFmt.format.u.raw_video;
		if (rv.width == 0 || rv.height == 0)
			return (fInitErr = B_ERROR);
		fVideoFormat = rv;
		fVideoStride = rv.width * 4;	// BGRx, packed rows
		fVideoBufferSize = (size_t)fVideoStride * rv.height;
		fVideoBuffer = new(std::nothrow) uint8_t[fVideoBufferSize];
		if (fVideoBuffer == NULL)
			return (fInitErr = B_NO_MEMORY);
		const bigtime_t videoDuration = fVideoTrack->Duration();
		if (videoDuration > fDuration)
			fDuration = videoDuration;
	}

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
	if (s == BMediaPlayer::B_PLAYER_STOPPED && fEOS.load()) {
		std::lock_guard<std::mutex> _(fLock);
		std::lock_guard<std::mutex> audio(fAudioLock);
		bigtime_t t = 0;
		if (fTrack != NULL)
			fTrack->SeekToTime(&t);
		if (fVideoTrack != NULL)
			fVideoTrack->SeekToTime(&t);
		_ResetVideoPacing();
		_ResetClock(0);
		fEOS = false;
	}
	fHasData = true;
	status_t err = fNode->IsStarted() ? B_OK : fNode->Start();
	if (err != B_OK)
		return err;
	if (fVideoTrack != NULL)
		_StartVideoRunner();
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
	_StopVideoRunner();
	_SetState(BMediaPlayer::B_PLAYER_PAUSED);
	return B_OK;
}


status_t
BMediaPlayer::Impl::Stop()
{
	if (fNode == NULL)
		return B_OK;
	_StopVideoRunner();
	if (fNode->IsStarted()) {
		fNode->Stop();
		if (fNotifyFunc != NULL)
			fNotifyFunc(fCookie, BMediaPlayer::B_STOPPED);
	}
	// Publish the state before seeking: the pump tests it under fLock,
	// so it cannot deliver a frame from after the seek.
	_SetState(BMediaPlayer::B_PLAYER_STOPPED);
	std::lock_guard<std::mutex> _(fLock);
	std::lock_guard<std::mutex> audio(fAudioLock);
	bigtime_t t = 0;
	if (fTrack != NULL)
		fTrack->SeekToTime(&t);
	if (fVideoTrack != NULL)
		fVideoTrack->SeekToTime(&t);
	_ResetVideoPacing();
	_ResetClock(0);
	fEOS = false;
	return B_OK;
}


status_t
BMediaPlayer::Impl::SeekTo(bigtime_t position, int32 flags)
{
	bool resume = false;
	{
		std::lock_guard<std::mutex> _(fLock);
		std::lock_guard<std::mutex> audio(fAudioLock);
		if (fTrack == NULL && fVideoTrack == NULL)
			return B_NO_INIT;
		// A seek after end of stream resumes from the new point; the
		// transport would otherwise sit stopped until Play.
		resume = fEOS.load()
			&& fState.load() == BMediaPlayer::B_PLAYER_STOPPED;
		bigtime_t t = position;
		status_t err = B_OK;
		if (fTrack != NULL)
			err = fTrack->SeekToTime(&t, flags);
		// Both tracks share one pipeline; a second flushing seek only
		// restarts the first.
		if (err == B_OK && fVideoTrack != NULL && fTrack == NULL) {
			bigtime_t videoT = position;
			err = fVideoTrack->SeekToTime(&videoT, flags);
		}
		if (err != B_OK)
			return err;
		// The pending frame, if any, is from before the seek.
		_ResetVideoPacing();
		_ResetClock(fTrack != NULL ? t : position);
		fEOS = false;
	}
	if (resume) {
		fHasData = true;
		if (fNode != NULL && !fNode->IsStarted())
			fNode->Start();
		_StartVideoRunner();
		_SetState(BMediaPlayer::B_PLAYER_PLAYING);
	} else if (fState.load() == BMediaPlayer::B_PLAYER_PAUSED) {
		// Paused: pull and deliver the frame at the new position so the
		// seek is visible without resuming playback.
		_PumpVideoFrame(true);
	}
	_PostNotification(BMediaPlayer::B_PLAYER_POSITION_CHANGED);
	return B_OK;
}


bigtime_t
BMediaPlayer::Impl::Position() const
{
	// The last buffers can stop short of the container duration.
	if (fEOS.load() && fDuration > 0)
		return fDuration;
	if (fTrack != NULL && fAudioRate > 0.0f)
		return fClockBase + (bigtime_t)(fPlayedFrames * 1000000.0 / fAudioRate);
	if (fTrack != NULL)
		return fTrack->CurrentTime();
	if (fVideoTrack != NULL)
		return fVideoTrack->CurrentTime();
	return 0;
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
BMediaPlayer::Impl::SetVideoView(BView* view)
{
	_StopVideoRunner();
	// Detach under fLock first: an in-flight pump that already holds
	// the lock sees a NULL view and bails.
	BLooper* oldLooper = fVideoLooper;
	_VideoPump* oldPump = fVideoPump;
	fVideoLooper = NULL;
	fVideoPump = NULL;
	{
		std::lock_guard<std::mutex> _(fLock);
		fVideoView = NULL;
	}
	if (oldPump != NULL) {
		if (oldLooper != NULL) {
			// Add/RemoveHandler run under the looper lock; the pump
			// executes there too, so this also waits it out.
			BAutolock looperLock(oldLooper);
			oldLooper->RemoveHandler(oldPump);
		}
		delete oldPump;
	}
	if (view == NULL)
		return;

	BLooper* looper = view->Looper();
	if (looper == NULL)
		return;
	_VideoPump* pump = new _VideoPump(this);
	if (pump == NULL)
		return;
	{
		BAutolock looperLock(looper);
		looper->AddHandler(pump);
	}
	{
		std::lock_guard<std::mutex> _(fLock);
		fVideoLooper = looper;
		fVideoPump = pump;
		fVideoView = view;
	}
	if (fState.load() == BMediaPlayer::B_PLAYER_PLAYING)
		_StartVideoRunner();
}


void
BMediaPlayer::Impl::_VideoPump::MessageReceived(BMessage* /*message*/)
{
	fImpl->_PumpVideoFrame();
}


void
BMediaPlayer::Impl::_PumpVideoFrame(bool force)
{
	BMessage msg;
	BLooper* looper = NULL;
	BView* view = NULL;
	{
		// SeekTo and SetTo touch the same track and buffer from caller
		// threads; nothing here is safe outside the lock.
		std::lock_guard<std::mutex> _(fLock);
		if (fState.load() != BMediaPlayer::B_PLAYER_PLAYING && !force)
			return;
		if (fVideoTrack == NULL || fVideoView == NULL || fVideoBuffer == NULL)
			return;

		const bool haveAudio = fTrack != NULL;
		while (true) {
			if (!fVideoFramePending) {
				int64 frames = 1;
				media_header mh;
				memset(&mh, 0, sizeof(mh));
				status_t err = fVideoTrack->ReadFrames(fVideoBuffer,
					&frames, &mh);
				if (err == B_LAST_BUFFER_ERROR) {
					_SetState(BMediaPlayer::B_PLAYER_STOPPED);
					_PostNotification(
						BMediaPlayer::B_PLAYER_END_OF_STREAM);
					return;
				}
				if (err != B_OK || frames < 1)
					return;
				fVideoPendingTime = mh.start_time;
				fVideoFramePending = true;
				if (mh.start_time == 0) {
					// A second zero stamp means the stream
					// carries no PTS: pace from field_rate.
					if (fVideoZeroStamps > 0
							&& fVideoFormat.field_rate > 0.0f) {
						fVideoPaceNominal = true;
						fVideoNominalInterval = (bigtime_t)
							(1000000.0f
								/ fVideoFormat.field_rate);
					}
					fVideoZeroStamps++;
				} else
					fVideoZeroStamps = 0;
			}

			bigtime_t clockNow;
			bigtime_t due = fVideoPendingTime;
			bool allowDrop = haveAudio;
			if (fVideoPaceNominal) {
				// No timestamps to judge: hold the nominal
				// rate on the system clock instead.
				due = fVideoLastDeliver + fVideoNominalInterval;
				clockNow = system_time();
				allowDrop = false;
			} else if (haveAudio) {
				clockNow = Position();
			} else {
				// Timestamps are file-relative: map them onto
				// the system clock at the first pull after
				// Play or Seek.
				if (!fVideoClockValid) {
					fVideoClockBase = system_time() - fVideoPendingTime;
					fVideoClockValid = true;
				}
				due = fVideoClockBase + fVideoPendingTime;
				clockNow = system_time();
			}

			// A forced pull delivers the frame at hand regardless of
			// the clock; that is how a paused seek becomes visible.
			if (!force) {
				const BPrivate::media::video_sync_decision decision
					= BPrivate::media::DecideVideoSync(due, clockNow,
						kVideoSyncTolerance, allowDrop);
				if (decision == BPrivate::media::VIDEO_SYNC_HOLD)
					return;
				if (decision == BPrivate::media::VIDEO_SYNC_DROP) {
					fVideoFramePending = false;
					continue;
				}
			}

			// Surface presentation replaces the B_RAW_TYPE payload
			// below with a dma-buf descriptor; the keys stay.
			msg.what = BMediaPlayer::B_PLAYER_VIDEO_FRAME;
			msg.AddInt32("width", (int32)fVideoFormat.width);
			msg.AddInt32("height", (int32)fVideoFormat.height);
			msg.AddInt32("stride", (int32)fVideoStride);
			msg.AddInt64("start_time", (int64)fVideoPendingTime);
			msg.AddInt64("audio_time", (int64)(haveAudio ? clockNow : -1));
			msg.AddData("frame", B_RAW_TYPE, fVideoBuffer,
				(uint32)fVideoBufferSize);
			looper = fVideoLooper;
			view = fVideoView;
			fVideoFramePending = false;
			fVideoLastDeliver = system_time();
			break;
		}
	}

	// The pump already runs on the view's looper, so the frame goes
	// over in place: a SendMessage would bounce the payload through
	// the looper port back to this same thread. A forced (paused seek)
	// pull arrives from another thread and needs the looper lock.
	if (looper != NULL && view != NULL) {
		BAutolock lock(looper);
		if (lock.IsLocked())
			looper->DispatchMessage(&msg, view);
	}
}


void
BMediaPlayer::Impl::_StartVideoRunner()
{
	if (fVideoPump == NULL || fVideoRunner != NULL)
		return;
	if (fVideoTrack == NULL)
		return;
	// The runner only wakes the pump: timestamps and the audio clock
	// decide presentation, so a 0 field_rate (VFR) still runs.
	bigtime_t interval = kVideoPumpQuantumMax;
	if (fVideoFormat.field_rate > 0.0f) {
		interval = (bigtime_t)(1000000.0f
			/ fVideoFormat.field_rate / 4.0f);
		if (interval < kVideoPumpQuantumMin)
			interval = kVideoPumpQuantumMin;
	}
	BMessage msg(kMsgVideoPump);
	fVideoRunner = new BMessageRunner(BMessenger(fVideoPump), msg, interval);
}


void
BMediaPlayer::Impl::_StopVideoRunner()
{
	delete fVideoRunner;
	fVideoRunner = NULL;
}


void
BMediaPlayer::Impl::_ResetVideoPacing()
{
	fVideoFramePending = false;
	fVideoClockValid = false;
	fVideoZeroStamps = 0;
	fVideoPaceNominal = false;
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
	const media_raw_audio_format& format)
{
	if (fEOS.load() || fTrack == NULL
			|| fState.load() != BMediaPlayer::B_PLAYER_PLAYING) {
		memset(buffer, 0, size);
		return;
	}

	// A seek is mid-flight on the caller thread; silence for one buffer
	// beats replaying pre-seek audio. Not fLock: the video pump holds it
	// while it waits for a frame, and only drained audio lets one arrive.
	std::unique_lock<std::mutex> lock(fAudioLock, std::try_to_lock);
	if (!lock.owns_lock()) {
		memset(buffer, 0, size);
		return;
	}

	// Decoded buffers rarely match the cycle; keep reading until it is full.
	int64 wantFrames = size / fFrameStride;
	int64 gotFrames  = 0;
	status_t err = B_OK;
	while (gotFrames < wantFrames) {
		int64 frames = wantFrames - gotFrames;
		err = fTrack->ReadFrames((uint8_t*)buffer + gotFrames * fFrameStride,
			&frames);
		if (err != B_OK || frames <= 0)
			break;
		gotFrames += frames;
	}
	// Audio already in hand plays out; end of stream is seen next cycle.
	if (err == B_LAST_BUFFER_ERROR && gotFrames > 0)
		err = B_OK;
	if ((err == B_OK || err == B_LAST_BUFFER_ERROR) && gotFrames > 0)
		fPlayedFrames += gotFrames;

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

	// Apply the player volume on the decoded float samples. GStreamer
	// negotiates F32LE interleaved into this path (MediaFile OpenUri caps).
	if (fVolume != 1.0f) {
		if ((format.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
				== media_raw_audio_format::B_AUDIO_FLOAT) {
			const size_t sampleCount = (size_t)gotFrames
				* format.channel_count;
			float* samples = (float*)buffer;
			for (size_t i = 0; i < sampleCount; i++)
				samples[i] *= fVolume;
		}
	}

	_PostPeaks((const float*)buffer, gotFrames, format);
}


// Per-channel peaks for level meters, about 25 times a second.
void
BMediaPlayer::Impl::_PostPeaks(const float* samples, int64 frames,
	const media_raw_audio_format& format)
{
	if (!fHasTarget || frames <= 0
		|| (format.format & media_raw_audio_format::B_AUDIO_SIZE_MASK)
			!= media_raw_audio_format::B_AUDIO_FLOAT)
		return;

	const uint32 channels = format.channel_count < 8 ? format.channel_count : 8;
	for (int64 f = 0; f < frames; f++) {
		for (uint32 c = 0; c < channels; c++) {
			float v = samples[f * format.channel_count + c];
			if (v < 0)
				v = -v;
			if (v > fPeaks[c])
				fPeaks[c] = v;
		}
	}

	const bigtime_t now = system_time();
	if (now - fLastPeakPost < 40000)
		return;
	fLastPeakPost = now;

	BMessage msg(BMediaPlayer::B_PLAYER_PEAKS);
	for (uint32 c = 0; c < channels; c++) {
		msg.AddFloat("max", fPeaks[c]);
		fPeaks[c] = 0;
	}
	fTarget.SendMessage(&msg);
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
status_t   BMediaPlayer::SetTo(const BUrl& url) { return fImpl ? fImpl->SetTo(url) : B_NO_MEMORY; }
status_t   BMediaPlayer::Play()             { return fImpl ? fImpl->Play()  : B_NO_INIT; }
status_t   BMediaPlayer::Pause()            { return fImpl ? fImpl->Pause() : B_NO_INIT; }
status_t   BMediaPlayer::Stop()             { return fImpl ? fImpl->Stop()  : B_NO_INIT; }
status_t   BMediaPlayer::SeekTo(bigtime_t p, int32 f)
	{ return fImpl ? fImpl->SeekTo(p, f) : B_NO_INIT; }
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
