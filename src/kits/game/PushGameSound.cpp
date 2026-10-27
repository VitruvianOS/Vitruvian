/*
 * Copyright 2001-2012 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Christopher ML Zumwalt May (zummy@users.sf.net)
 *		Jérôme Duval
 */


#include <PushGameSound.h>

#include <List.h>
#include <string.h>

#include <mutex>

#include "GSUtility.h"


// The app thread locks and unlocks pages while the mixer thread plays them.
static std::mutex sPageLock;


static void
fill_silence(const gs_audio_format& format, void* buffer, size_t bytes)
{
	memset(buffer, format.format == gs_audio_format::B_GS_U8 ? 0x80 : 0, bytes);
}


BPushGameSound::BPushGameSound(size_t inBufferFrameCount,
	const gs_audio_format *format, size_t inBufferCount,
	BGameSoundDevice *device)
	:
	BStreamingGameSound(inBufferFrameCount, format, inBufferCount, device),
	fLockPos(0),
	fPlayPos(0)
{
	fPageLocked = new BList;

	size_t frameSize = get_sample_size(format->format) * format->channel_count;

	fPageCount = inBufferCount;
	fPageSize = frameSize * inBufferFrameCount;
	fBufferSize = fPageSize * fPageCount;

	fBuffer = new char[fBufferSize];
	fill_silence(*format, fBuffer, fBufferSize);
}


BPushGameSound::BPushGameSound(BGameSoundDevice * device)
		:	BStreamingGameSound(device),
			fLockPos(0),
			fPlayPos(0),
			fBuffer(NULL),
			fPageSize(0),
			fPageCount(0),
			fBufferSize(0)
{
	fPageLocked = new BList;
}


BPushGameSound::~BPushGameSound()
{
	delete [] fBuffer;
	delete fPageLocked;
}


BPushGameSound::lock_status
BPushGameSound::LockNextPage(void **out_pagePtr, size_t *out_pageSize)
{
	std::lock_guard<std::mutex> _(sPageLock);

	if (fBuffer == NULL || fPageSize == 0)
		return lock_failed;

	// the user can not lock every page
	if (fPageLocked->CountItems() > (int32)fPageCount - 1)
		return lock_failed;

	char* lockPage = &fBuffer[fLockPos];
	if (fPageLocked->HasItem(lockPage))
		return lock_failed;

	// the user can't lock a page being played
	if (fLockPos / fPageSize == fPlayPos / fPageSize
		&& fPlayPos % fPageSize != 0)
		return lock_failed;

	fPageLocked->AddItem(lockPage);

	// move the locker to the next page
	fLockPos += fPageSize;
	if (fLockPos >= fBufferSize)
		fLockPos = 0;

	*out_pagePtr = lockPage;
	*out_pageSize = fPageSize;

	return lock_ok;
}


status_t
BPushGameSound::UnlockPage(void *in_pagePtr)
{
	std::lock_guard<std::mutex> _(sPageLock);
	return (fPageLocked->RemoveItem(in_pagePtr)) ? B_OK : B_ERROR;
}


BPushGameSound::lock_status
BPushGameSound::LockForCyclic(void **out_basePtr, size_t *out_size)
{
	*out_basePtr = fBuffer;
	*out_size = fBufferSize;
	return lock_ok;
}


status_t
BPushGameSound::UnlockCyclic()
{
	return B_OK;
}


size_t
BPushGameSound::CurrentPosition()
{
	return fPlayPos;
}


BGameSound *
BPushGameSound::Clone() const
{
	gs_audio_format format = Format();
	size_t frameSize = get_sample_size(format.format) * format.channel_count;
	size_t bufferFrameCount = fPageSize / frameSize;

	return new BPushGameSound(bufferFrameCount, &format, fPageCount, Device());
}


status_t
BPushGameSound::Perform(int32 selector, void *data)
{
	return BStreamingGameSound::Perform(selector, data);
}


status_t
BPushGameSound::SetParameters(size_t inBufferFrameCount,
	const gs_audio_format *format, size_t inBufferCount)
{
	return B_UNSUPPORTED;
}


status_t
BPushGameSound::SetStreamHook(void (*hook)(void * inCookie, void * inBuffer,
	size_t inByteCount, BStreamingGameSound * me), void * cookie)
{
	return B_UNSUPPORTED;
}


void
BPushGameSound::FillBuffer(void *inBuffer, size_t inByteCount)
{
	char* out = (char*)inBuffer;
	std::lock_guard<std::mutex> _(sPageLock);

	if (fBuffer == NULL || fPageSize == 0) {
		fill_silence(Format(), out, inByteCount);
		return;
	}

	while (inByteCount > 0) {
		// a page the app still holds is not ready: wait on it
		char* page = &fBuffer[(fPlayPos / fPageSize) * fPageSize];
		if (fPageLocked->HasItem(page)) {
			fill_silence(Format(), out, inByteCount);
			return;
		}

		size_t chunk = fPageSize - fPlayPos % fPageSize;
		if (chunk > inByteCount)
			chunk = inByteCount;

		memcpy(out, &fBuffer[fPlayPos], chunk);
		out += chunk;
		inByteCount -= chunk;

		fPlayPos += chunk;
		if (fPlayPos >= fBufferSize)
			fPlayPos = 0;
	}
}


/* unimplemented for protection of the user:
 *
 * BPushGameSound::BPushGameSound()
 * BPushGameSound::BPushGameSound(const BPushGameSound &)
 * BPushGameSound &BPushGameSound::operator=(const BPushGameSound &)
 */


status_t
BPushGameSound::_Reserved_BPushGameSound_0(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_1(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_2(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_3(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_4(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_5(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_6(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_7(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_8(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_9(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_10(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_11(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_12(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_13(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_14(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_15(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_16(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_17(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_18(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_19(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_20(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_21(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_22(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BPushGameSound::_Reserved_BPushGameSound_23(int32 arg, ...)
{
	return B_ERROR;
}
