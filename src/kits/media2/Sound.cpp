/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/Sound.h>

#include <stdlib.h>
#include <string.h>

#include <SupportDefs.h>


BSound::BSound(const entry_ref* soundFile, bool /*loadIntoMemory*/)
	:
	fEntry(),
	fData(NULL),
	fSize(0),
	fDuration(0),
	fRefCount(1),
	fInitErr(B_UNSUPPORTED),
	fOwnsData(false)
{
	if (soundFile != NULL)
		fEntry = *soundFile;
}


BSound::BSound(const void* data, size_t size, const BMediaFormat& format)
	:
	fEntry(),
	fData(NULL),
	fSize(size),
	fFormat(format),
	fDuration(0),
	fRefCount(1),
	fInitErr(B_OK),
	fOwnsData(true)
{
	if (data == NULL || size == 0) {
		fInitErr = B_BAD_VALUE;
		return;
	}
	fData = malloc(size);
	if (fData == NULL) {
		fInitErr = B_NO_MEMORY;
		return;
	}
	memcpy(fData, data, size);

	if (format.IsRawAudio()) {
		const media_raw_audio_format& raw = format.format.u.raw_audio;
		const uint32 stride = (raw.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK) * raw.channel_count;
		if (stride > 0 && raw.frame_rate > 0.0f) {
			const uint64 frames = size / stride;
			fDuration = (bigtime_t)((frames * 1000000ULL) / (uint64)raw.frame_rate);
		}
	}
}


BSound::~BSound()
{
	if (fOwnsData && fData != NULL)
		free(fData);
}


status_t  BSound::InitCheck() const           { return fInitErr; }
const BMediaFormat& BSound::Format() const    { return fFormat; }
bigtime_t BSound::Duration() const            { return fDuration; }
const entry_ref* BSound::GetEntry() const     { return &fEntry; }
const void* BSound::Data() const              { return fData; }
size_t    BSound::Size() const                { return fSize; }


BSound*
BSound::Acquire()
{
	atomic_add(&fRefCount, 1);
	return this;
}


bool
BSound::Release()
{
	if (atomic_add(&fRefCount, -1) == 1) {
		delete this;
		return true;
	}
	return false;
}
