/*
 * Vitruvian — GameSoundBuffer
 * Distributed under the terms of the MIT License.
 *
 * Reworked from haiku-latest: the node/Producer machinery is gone. Mixing is
 * driven by BGameSoundDevice's BSoundPlayer callback, which calls Play() on
 * each active buffer to additively mix into the output (F32 only — the
 * mixer fast-path; non-F32 formats overwrite without gain).
 */

#include "GameSoundBuffer.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include "GSUtility.h"


GameSoundBuffer::GameSoundBuffer(const gs_audio_format* format)
	:
	fFormat(*format),
	fLooping(false),
	fFrameSize(get_sample_size(format->format) * format->channel_count),
	fIsPlaying(false),
	fGain(1.0f),
	fPan(0.0f),
	fPanLeft(1.0f),
	fPanRight(1.0f),
	fGainRamp(NULL),
	fPanRamp(NULL)
{
}


GameSoundBuffer::~GameSoundBuffer()
{
	delete fGainRamp;
	delete fPanRamp;
}


const gs_audio_format&	GameSoundBuffer::Format() const   { return fFormat; }
bool		GameSoundBuffer::IsPlaying() const            { return fIsPlaying; }
bool		GameSoundBuffer::IsLooping() const            { return fLooping; }
void		GameSoundBuffer::SetLooping(bool loop)        { fLooping = loop; }
float		GameSoundBuffer::Gain() const                 { return fGain; }
float		GameSoundBuffer::Pan() const                  { return fPan; }


status_t
GameSoundBuffer::StartPlaying()
{
	fIsPlaying = true;
	return B_OK;
}


status_t
GameSoundBuffer::StopPlaying()
{
	fIsPlaying = false;
	Reset();
	return B_OK;
}


status_t
GameSoundBuffer::SetGain(float gain, bigtime_t duration)
{
	if (gain < 0.0f) gain = 0.0f;
	if (gain > 1.0f) gain = 1.0f;
	if (duration <= 0) {
		fGain = gain;
		delete fGainRamp;
		fGainRamp = NULL;
		return B_OK;
	}
	delete fGainRamp;
	fGainRamp = InitRamp(&fGain, gain, fFormat.frame_rate, duration);
	return B_OK;
}


status_t
GameSoundBuffer::SetPan(float pan, bigtime_t duration)
{
	if (pan < -1.0f) pan = -1.0f;
	if (pan >  1.0f) pan =  1.0f;
	if (duration <= 0) {
		fPan      = pan;
		fPanLeft  = pan <= 0 ? 1.0f : 1.0f - pan;
		fPanRight = pan >= 0 ? 1.0f : 1.0f + pan;
		delete fPanRamp;
		fPanRamp = NULL;
		return B_OK;
	}
	delete fPanRamp;
	fPanRamp = InitRamp(&fPan, pan, fFormat.frame_rate, duration);
	return B_OK;
}


void
GameSoundBuffer::UpdateMods()
{
	if (fGainRamp != NULL && !ChangeRamp(fGainRamp)) {
		delete fGainRamp;
		fGainRamp = NULL;
	}
	if (fPanRamp != NULL && !ChangeRamp(fPanRamp)) {
		delete fPanRamp;
		fPanRamp = NULL;
		fPanLeft  = fPan <= 0 ? 1.0f : 1.0f - fPan;
		fPanRight = fPan >= 0 ? 1.0f : 1.0f + fPan;
	}
}


void
GameSoundBuffer::Reset()
{
}


void
GameSoundBuffer::Play(void* data, int64 frames)
{
	if (!fIsPlaying)
		return;
	FillBuffer(data, frames);
	UpdateMods();
}


status_t
GameSoundBuffer::GetAttributes(gs_attribute* attributes, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		gs_attribute& a = attributes[i];
		a.duration = 0;
		switch (a.attribute) {
			case B_GS_GAIN:    a.value = fGain;                       break;
			case B_GS_PAN:     a.value = fPan;                        break;
			case B_GS_LOOPING: a.value = fLooping ? 1.0f : 0.0f;      break;
			default:           a.value = 0;                           break;
		}
	}
	return B_OK;
}


status_t
GameSoundBuffer::SetAttributes(gs_attribute* attributes, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		const gs_attribute& a = attributes[i];
		switch (a.attribute) {
			case B_GS_GAIN:    SetGain(a.value, a.duration); break;
			case B_GS_PAN:     SetPan(a.value, a.duration);  break;
			case B_GS_LOOPING: fLooping = a.value != 0;      break;
			default:           break;
		}
	}
	return B_OK;
}


// #pragma mark - SimpleSoundBuffer


SimpleSoundBuffer::SimpleSoundBuffer(const gs_audio_format* format,
	const void* data, int64 frames)
	:
	GameSoundBuffer(format),
	fBuffer(NULL),
	fBufferSize(0),
	fPosition(0)
{
	fBufferSize = (size_t)(frames * fFrameSize);
	if (fBufferSize == 0)
		return;
	fBuffer = (char*)malloc(fBufferSize);
	if (fBuffer != NULL && data != NULL)
		memcpy(fBuffer, data, fBufferSize);
}


SimpleSoundBuffer::~SimpleSoundBuffer()
{
	free(fBuffer);
}


void
SimpleSoundBuffer::Reset()
{
	fPosition = 0;
}


void
SimpleSoundBuffer::FillBuffer(void* data, int64 frames)
{
	if (fBuffer == NULL)
		return;

	uint8_t* dst = (uint8_t*)data;
	size_t bytesNeeded = (size_t)frames * fFrameSize;

	while (bytesNeeded > 0) {
		size_t avail = fBufferSize - fPosition;
		if (avail == 0) {
			if (!fLooping) {
				StopPlaying();
				return;
			}
			fPosition = 0;
			avail = fBufferSize;
		}
		const size_t chunk = avail < bytesNeeded ? avail : bytesNeeded;
		if (Format().format == gs_audio_format::B_GS_F) {
			const float  gain = Gain();
			const float* src  = (const float*)(fBuffer + fPosition);
			float*       d    = (float*)dst;
			const size_t samples = chunk / sizeof(float);
			for (size_t i = 0; i < samples; i++)
				d[i] += src[i] * gain;
		} else {
			memcpy(dst, fBuffer + fPosition, chunk);
		}
		fPosition  += chunk;
		dst        += chunk;
		bytesNeeded -= chunk;
	}
}


// #pragma mark - StreamingSoundBuffer


#include <StreamingGameSound.h>


StreamingSoundBuffer::StreamingSoundBuffer(const gs_audio_format* format,
	void* streamHook)
	:
	GameSoundBuffer(format),
	fStreamHook(streamHook)
{
}


StreamingSoundBuffer::~StreamingSoundBuffer()
{
}


void
StreamingSoundBuffer::FillBuffer(void* data, int64 frames)
{
	if (fStreamHook == NULL) {
		memset(data, 0, frames * fFrameSize);
		return;
	}
	BStreamingGameSound* sgs = (BStreamingGameSound*)fStreamHook;
	sgs->FillBuffer(data, frames * fFrameSize);
}
