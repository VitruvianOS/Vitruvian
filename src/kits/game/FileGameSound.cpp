/*
 * Vitruvian — BFileGameSound
 * Distributed under the terms of the MIT License.
 *
 * Reworked for media2: BMediaFile/BMediaTrack pull replaces the legacy
 * media-tracker + ring-buffer. The streaming hook plumbing is inherited
 * from BStreamingGameSound; we override FillBuffer to read decoded PCM
 * straight from the track.
 */

#include <FileGameSound.h>

#include <new>
#include <stdlib.h>
#include <string.h>

#include <mutex>

#include <Entry.h>
#include <ObjectList.h>
#include <Path.h>

#include <media2/MediaFile.h>
#include <media2/MediaFormat.h>
#include <media2/MediaTrack.h>

#include "GameSoundBuffer.h"
#include "GameSoundDevice.h"
#include "GSUtility.h"


// Internal state we own; not exposed via the header _reserved storage.
// Held in a side table keyed by `this` so we don't touch the verbatim
// haiku-latest layout of BFileGameSound.
struct FileGameSoundState {
	BMediaFile*		file;
	BMediaTrack*	track;
	gs_audio_format	format;
	uint32			frameStride;
	bool			eos;
};


static std::mutex          sStateLock;
static BObjectList<FileGameSoundState, true> sStates;	// indexed in lockstep
static BObjectList<BFileGameSound, false>    sStateOwners;


static FileGameSoundState*
GetState(BFileGameSound* self)
{
	std::lock_guard<std::mutex> _(sStateLock);
	for (int32 i = 0; i < sStateOwners.CountItems(); i++) {
		if (sStateOwners.ItemAt(i) == self)
			return sStates.ItemAt(i);
	}
	return NULL;
}


static FileGameSoundState*
AttachState(BFileGameSound* self)
{
	FileGameSoundState* s = new(std::nothrow) FileGameSoundState();
	if (s == NULL)
		return NULL;
	s->file        = NULL;
	s->track       = NULL;
	s->frameStride = 0;
	s->eos         = false;
	memset(&s->format, 0, sizeof(s->format));

	std::lock_guard<std::mutex> _(sStateLock);
	sStateOwners.AddItem(self);
	sStates.AddItem(s);
	return s;
}


static void
DetachState(BFileGameSound* self)
{
	std::lock_guard<std::mutex> _(sStateLock);
	for (int32 i = 0; i < sStateOwners.CountItems(); i++) {
		if (sStateOwners.ItemAt(i) == self) {
			FileGameSoundState* s = sStates.ItemAt(i);
			if (s != NULL) {
				delete s->file;	// owns BMediaFile -> ReleaseTrack on dtor
				delete s;
			}
			sStateOwners.RemoveItemAt(i);
			sStates.RemoveItemAt(i);
			return;
		}
	}
}


// #pragma mark - ctors


BFileGameSound::BFileGameSound(const entry_ref* file, bool looping,
	BGameSoundDevice* device)
	:
	BStreamingGameSound(device)
{
	FileGameSoundState* s = AttachState(this);
	if (s == NULL) {
		SetInitError(B_NO_MEMORY);
		return;
	}

	s->file = new(std::nothrow) BMediaFile(file);
	if (s->file == NULL || s->file->InitCheck() != B_OK) {
		SetInitError(s->file != NULL ? s->file->InitCheck() : B_NO_MEMORY);
		return;
	}

	BMediaTrack* track = s->file->TrackAt(0);
	if (track == NULL) {
		SetInitError(B_ERROR);
		return;
	}
	s->track = track;

	BMediaFormat mfmt;
	if (track->DecodedFormat(&mfmt) != B_OK || !mfmt.IsRawAudio()) {
		SetInitError(B_ERROR);
		return;
	}
	media_to_gs_format(&s->format, &mfmt.format.u.raw_audio);
	s->frameStride = get_sample_size(s->format.format) * s->format.channel_count;

	// Wire ourselves as the streaming hook target — base ctor (no-format
	// variant) didn't create a device buffer; we do it now.
	gs_id sound = 0;
	status_t err = Device()->CreateBuffer(&sound, this, &s->format);
	if (err != B_OK) {
		SetInitError(err);
		return;
	}
	BGameSound::Init(sound);

	// Default to looping per haiku-latest BFileGameSound semantics — caller
	// can override via SetAttributes(B_GS_LOOPING).
	(void)looping;	// FIXME: persist as fLooping once header storage allows
}


BFileGameSound::BFileGameSound(const char* file, bool looping,
	BGameSoundDevice* device)
	:
	BStreamingGameSound(device)
{
	entry_ref ref;
	if (file != NULL && get_ref_for_path(file, &ref) == B_OK) {
		// Delegate to the entry_ref ctor's logic by inlining its body.
		FileGameSoundState* s = AttachState(this);
		if (s == NULL) {
			SetInitError(B_NO_MEMORY);
			return;
		}
		s->file = new(std::nothrow) BMediaFile(&ref);
		if (s->file == NULL || s->file->InitCheck() != B_OK) {
			SetInitError(s->file != NULL ? s->file->InitCheck() : B_NO_MEMORY);
			return;
		}
		BMediaTrack* track = s->file->TrackAt(0);
		if (track == NULL) {
			SetInitError(B_ERROR);
			return;
		}
		s->track = track;
		BMediaFormat mfmt;
		if (track->DecodedFormat(&mfmt) != B_OK || !mfmt.IsRawAudio()) {
			SetInitError(B_ERROR);
			return;
		}
		media_to_gs_format(&s->format, &mfmt.format.u.raw_audio);
		s->frameStride = get_sample_size(s->format.format)
			* s->format.channel_count;
		gs_id sound = 0;
		status_t err = Device()->CreateBuffer(&sound, this, &s->format);
		if (err != B_OK) {
			SetInitError(err);
			return;
		}
		BGameSound::Init(sound);
	} else {
		SetInitError(B_BAD_VALUE);
	}
	(void)looping;
}


BFileGameSound::BFileGameSound(BDataIO* /*data*/, bool /*looping*/,
	BGameSoundDevice* device)
	:
	BStreamingGameSound(device)
{
	// BDataIO-backed playback not supported in Vitruvian — apps should use
	// entry_ref or path.
	SetInitError(B_NOT_SUPPORTED);
}


BFileGameSound::~BFileGameSound()
{
	DetachState(this);
}


BGameSound*
BFileGameSound::Clone() const
{
	return NULL;
}


status_t
BFileGameSound::StartPlaying()
{
	FileGameSoundState* s = GetState(this);
	if (s != NULL)
		s->eos = false;
	return BGameSound::StartPlaying();
}


status_t
BFileGameSound::StopPlaying()
{
	status_t err = BGameSound::StopPlaying();
	FileGameSoundState* s = GetState(this);
	if (s != NULL && s->track != NULL) {
		bigtime_t t = 0;
		s->track->SeekToTime(&t);
		s->eos = false;
	}
	return err;
}


status_t
BFileGameSound::Preload()
{
	return B_OK;
}


void
BFileGameSound::FillBuffer(void* buffer, size_t byteCount)
{
	FileGameSoundState* s = GetState(this);
	if (s == NULL || s->track == NULL || s->frameStride == 0) {
		memset(buffer, 0, byteCount);
		return;
	}

	int64 wantFrames = byteCount / s->frameStride;
	int64 gotFrames  = wantFrames;
	status_t err = s->track->ReadFrames(buffer, &gotFrames);

	if (err == B_LAST_BUFFER_ERROR) {
		// Loop: rewind and try once more.
		bigtime_t t = 0;
		if (s->track->SeekToTime(&t) == B_OK) {
			s->eos = false;
			const size_t got = (size_t)gotFrames * s->frameStride;
			if (got < byteCount) {
				uint8_t* tail = (uint8_t*)buffer + got;
				int64 more = (byteCount - got) / s->frameStride;
				int64 read = more;
				if (s->track->ReadFrames(tail, &read) == B_OK
						&& read > 0) {
					gotFrames += read;
				}
			}
		} else {
			s->eos = true;
		}
	} else if (err != B_OK) {
		memset(buffer, 0, byteCount);
		return;
	}

	const size_t got = (size_t)gotFrames * s->frameStride;
	if (got < byteCount)
		memset((uint8_t*)buffer + got, 0, byteCount - got);
}


status_t
BFileGameSound::Perform(int32 /*selector*/, void* /*data*/)
{
	return B_NOT_SUPPORTED;
}


status_t
BFileGameSound::SetPaused(bool /*isPaused*/, bigtime_t /*rampTime*/)
{
	return B_NOT_SUPPORTED;
}


int32
BFileGameSound::IsPaused()
{
	return B_NOT_PAUSED;
}


// FBC reserved
status_t BFileGameSound::_Reserved_BFileGameSound_0(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_1(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_2(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_3(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_4(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_5(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_6(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_7(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_8(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_9(int32, ...)  { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_10(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_11(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_12(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_13(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_14(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_15(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_16(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_17(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_18(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_19(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_20(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_21(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_22(int32, ...) { return B_ERROR; }
status_t BFileGameSound::_Reserved_BFileGameSound_23(int32, ...) { return B_ERROR; }
