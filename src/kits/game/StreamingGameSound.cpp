/*
 * Vitruvian — BStreamingGameSound
 * Distributed under the terms of the MIT License.
 *
 * Reworked for media2: the legacy buffer-thread + ring-buffer machinery is
 * gone. The device-level BSoundPlayer mixer pulls from each active
 * StreamingSoundBuffer; the buffer's FillBuffer calls back into
 * BStreamingGameSound::FillBuffer on the realtime thread, which either
 * forwards to a user hook (SetStreamHook) or zero-fills.
 */

#include <StreamingGameSound.h>

#include <new>
#include <string.h>

#include <mutex>

#include "GameSoundBuffer.h"
#include "GameSoundDevice.h"
#include "GSUtility.h"


BStreamingGameSound::BStreamingGameSound(size_t /*bufferFrameCount*/,
	const gs_audio_format* format, size_t /*bufferCount*/,
	BGameSoundDevice* device)
	:
	BGameSound(device)
{
	fStreamHook   = NULL;
	fStreamCookie = NULL;
	if (format == NULL) {
		SetInitError(B_BAD_VALUE);
		return;
	}
	gs_id sound = 0;
	status_t err = Device()->CreateBuffer(&sound, this, format);
	if (err != B_OK) {
		SetInitError(err);
		return;
	}
	BGameSound::Init(sound);
}


BStreamingGameSound::BStreamingGameSound(BGameSoundDevice* device)
	:
	BGameSound(device)
{
	fStreamHook   = NULL;
	fStreamCookie = NULL;
}


BStreamingGameSound::~BStreamingGameSound()
{
}


BGameSound*
BStreamingGameSound::Clone() const
{
	return NULL;
}


status_t
BStreamingGameSound::SetStreamHook(hook h, void* cookie)
{
	std::lock_guard<std::mutex> _(fLock);
	fStreamHook = h;
	fStreamCookie     = cookie;
	return B_OK;
}


void
BStreamingGameSound::FillBuffer(void* buffer, size_t byteCount)
{
	std::lock_guard<std::mutex> _(fLock);
	if (fStreamHook != NULL)
		fStreamHook(fStreamCookie, buffer, byteCount, this);
	else
		memset(buffer, 0, byteCount);
}


status_t
BStreamingGameSound::SetAttributes(gs_attribute* attributes,
	size_t attributeCount)
{
	return BGameSound::SetAttributes(attributes, attributeCount);
}


status_t
BStreamingGameSound::Perform(int32 /*selector*/, void* /*data*/)
{
	return B_NOT_SUPPORTED;
}


status_t
BStreamingGameSound::SetParameters(size_t /*bufferFrameCount*/,
	const gs_audio_format* /*format*/, size_t /*bufferCount*/)
{
	// Buffering geometry is now driven by BSoundPlayer; the request is
	// honored at construction time only.
	return B_OK;
}


bool BStreamingGameSound::Lock()   { fLock.lock(); return true; }
void BStreamingGameSound::Unlock() { fLock.unlock(); }


// FBC reserved virtuals
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_0(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_1(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_2(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_3(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_4(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_5(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_6(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_7(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_8(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_9(int32, ...)  { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_10(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_11(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_12(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_13(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_14(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_15(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_16(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_17(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_18(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_19(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_20(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_21(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_22(int32, ...) { return B_ERROR; }
status_t BStreamingGameSound::_Reserved_BStreamingGameSound_23(int32, ...) { return B_ERROR; }
