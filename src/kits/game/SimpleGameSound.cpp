/*
 * Copyright 2001-2012 Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Christopher ML Zumwalt May (zummy@users.sf.net)
 */


#include <SimpleGameSound.h>

#include <Entry.h>
#include <media2/MediaFile.h>
#include <media2/MediaFormat.h>
#include <media2/MediaTrack.h>
#include <stdlib.h>
#include <string.h>

#include "GameSoundBuffer.h"
#include "GameSoundDefs.h"
#include "GameSoundDevice.h"
#include "GSUtility.h"


BSimpleGameSound::BSimpleGameSound(const entry_ref *inFile,
	BGameSoundDevice *device)
	:
	BGameSound(device)
{
	if (InitCheck() == B_OK)
		SetInitError(Init(inFile));
}


BSimpleGameSound::BSimpleGameSound(const char *inFile, BGameSoundDevice *device)
	:
	BGameSound(device)
{
	if (InitCheck() == B_OK) {
		entry_ref file;

		if (get_ref_for_path(inFile, &file) != B_OK)
			SetInitError(B_ENTRY_NOT_FOUND);
		else
			SetInitError(Init(&file));
	}
}


BSimpleGameSound::BSimpleGameSound(const void *inData, size_t inFrameCount,
	const gs_audio_format *format, BGameSoundDevice *device)
	:
	BGameSound(device)
{
	if (InitCheck() != B_OK)
		return;

	gs_audio_format actual = *format;
	if (actual.byte_order == 0)
		actual.byte_order = B_MEDIA_HOST_ENDIAN;

	size_t frameSize
		= get_sample_size(format->format) * format->channel_count;
	uchar * data = new uchar[inFrameCount * frameSize];
	memcpy(data, inData, inFrameCount * frameSize);

	SetInitError(Init(data, inFrameCount, &actual));
}


BSimpleGameSound::BSimpleGameSound(const BSimpleGameSound &other)
	:
	BGameSound(other)
{
	gs_audio_format format;
	void *data = NULL;

	status_t error = other.Device()->Buffer(other.ID(), &format, data);
	if (error != B_OK)
		SetInitError(error);

	Init(data, 0, &format);
	free(data);
}


BSimpleGameSound::~BSimpleGameSound()
{
}


BGameSound *
BSimpleGameSound::Clone() const
{
	gs_audio_format format;
	void *data = NULL;

	status_t error = Device()->Buffer(ID(), &format, data);
	if (error != B_OK)
		return NULL;

	BSimpleGameSound *clone = new BSimpleGameSound(data, 0, &format, Device());
	free(data);

	return clone;
}


/* virtual */ status_t
BSimpleGameSound::Perform(int32 selector, void * data)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::SetIsLooping(bool looping)
{
	gs_attribute attribute;

	attribute.attribute = B_GS_LOOPING;
	attribute.value = (looping) ? -1.0 : 0.0;
	attribute.duration = bigtime_t(0);
	attribute.flags = 0;

	return Device()->SetAttributes(ID(), &attribute, 1);
}


bool
BSimpleGameSound::IsLooping() const
{
	gs_attribute attribute;

	attribute.attribute = B_GS_LOOPING;
	attribute.flags = 0;

	if (Device()->GetAttributes(ID(), &attribute, 1) != B_OK)
		return false;

	return bool(attribute.value);
}


status_t
BSimpleGameSound::Init(const entry_ref* inFile)
{
	BMediaFile file(inFile);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BMediaTrack* audioStream = file.TrackAt(0);
	if (audioStream == NULL)
		return B_ERROR;

	BMediaFormat mfmt;
	status_t error = audioStream->DecodedFormat(&mfmt);
	if (error != B_OK)
		return error;
	if (!mfmt.IsRawAudio())
		return B_ERROR;

	const int64 frames = audioStream->CountFrames();
	if (frames <= 0)
		return B_ERROR;

	gs_audio_format gsformat;
	memset(&gsformat, 0, sizeof(gsformat));
	media_to_gs_format(&gsformat, &mfmt.format.u.raw_audio);

	const size_t frameSize
		= get_sample_size(gsformat.format) * gsformat.channel_count;
	char* data = new char[frames * frameSize];
	gsformat.buffer_size = frames * frameSize;

	int64 framesTotal = 0;
	int64 framesRead  = 0;
	while (framesTotal < frames) {
		char* pos = &data[framesTotal * frameSize];
		int64 want = frames - framesTotal;
		framesRead = want;
		status_t e = audioStream->ReadFrames(pos, &framesRead);
		if (e != B_OK && e != B_LAST_BUFFER_ERROR)
			break;
		if (framesRead <= 0)
			break;
		framesTotal += framesRead;
		if (e == B_LAST_BUFFER_ERROR)
			break;
	}

	error = Init(data, framesTotal, &gsformat);
	delete[] data;
	file.ReleaseTrack(audioStream);
	return error;
}


status_t
BSimpleGameSound::Init(const void* inData, int64 inFrameCount,
	const gs_audio_format* format)
{
	gs_id sound;

	status_t error
		= Device()->CreateBuffer(&sound, format, inData, inFrameCount);
	if (error != B_OK)
		return error;

	BGameSound::Init(sound);

	return B_OK;
}


/* unimplemented for protection of the user:
 *
 * BSimpleGameSound::BSimpleGameSound()
 * BSimpleGameSound &BSimpleGameSound::operator=(const BSimpleGameSound &)
 */


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_0(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_1(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_2(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_3(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_4(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_5(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_6(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_7(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_8(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_9(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_10(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_11(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_12(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_13(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_14(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_15(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_16(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_17(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_18(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_19(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_20(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_21(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_22(int32 arg, ...)
{
	return B_ERROR;
}


status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_23(int32 arg, ...)
{
	return B_ERROR;
}
