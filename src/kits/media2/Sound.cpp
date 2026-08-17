/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/Sound.h>

#include <stdlib.h>
#include <string.h>

#include <SupportDefs.h>

#include <media2/MediaFile.h>
#include <media2/MediaTrack.h>


BSound::BSound(const entry_ref* soundFile, bool /*loadIntoMemory*/)
	:
	fEntry(),
	fData(NULL),
	fSize(0),
	fDuration(0),
	fRefCount(1),
	fInitErr(B_NO_INIT),
	fOwnsData(false)
{
	if (soundFile == NULL) {
		fInitErr = B_BAD_VALUE;
		return;
	}
	fEntry = *soundFile;

	BMediaFile file(soundFile);
	if (file.InitCheck() != B_OK) {
		fInitErr = file.InitCheck();
		return;
	}

	BMediaTrack* track = file.TrackAt(0);
	if (track == NULL) {
		fInitErr = B_ERROR;
		return;
	}

	BMediaFormat format;
	status_t err = track->DecodedFormat(&format);
	if (err != B_OK || !format.IsRawAudio()) {
		fInitErr = err != B_OK ? err : B_ERROR;
		file.ReleaseTrack(track);
		return;
	}
	fFormat = format;

	const media_raw_audio_format& raw = format.format.u.raw_audio;
	const uint32 stride = (raw.format
		& media_raw_audio_format::B_AUDIO_SIZE_MASK) * raw.channel_count;
	const int64 frames = track->CountFrames();
	if (stride == 0 || frames <= 0) {
		fInitErr = B_ERROR;
		file.ReleaseTrack(track);
		return;
	}

	fSize = (size_t)frames * stride;
	fData = malloc(fSize);
	if (fData == NULL) {
		fInitErr = B_NO_MEMORY;
		fSize = 0;
		file.ReleaseTrack(track);
		return;
	}
	fOwnsData = true;

	int64 framesTotal = 0;
	while (framesTotal < frames) {
		uint8* dst = (uint8*)fData + framesTotal * stride;
		int64 want = frames - framesTotal;
		int64 got = want;
		status_t readErr = track->ReadFrames(dst, &got);
		if (readErr != B_OK && readErr != B_LAST_BUFFER_ERROR)
			break;
		if (got <= 0)
			break;
		framesTotal += got;
		if (readErr == B_LAST_BUFFER_ERROR)
			break;
	}
	fSize = (size_t)framesTotal * stride;
	fDuration = (bigtime_t)((framesTotal * 1000000LL) / (int64)raw.frame_rate);

	file.ReleaseTrack(track);
	fInitErr = B_OK;
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


bool
BSound::GetDataAt(off_t offset, void* buffer, size_t bufferSize,
	size_t* outUsed)
{
	if (fData == NULL || offset < 0 || (size_t)offset >= fSize) {
		*outUsed = 0;
		return false;
	}
	size_t avail = fSize - (size_t)offset;
	size_t used = avail < bufferSize ? avail : bufferSize;
	memcpy(buffer, (const uint8*)fData + offset, used);
	*outUsed = used;
	return true;
}


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
