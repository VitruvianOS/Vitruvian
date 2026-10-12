/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexNodes.h"

#include <math.h>
#include <string.h>

#include <media2/Controllable.h>
#include <media2/ParameterWeb.h>


enum {
	kToneParameterGain = 1
};


class CortexToneControllable : public BControllable {
public:
								CortexToneControllable(CortexToneUnit* unit);

			static	void		ChangeHook(int32 id, bigtime_t when,
									const void* value, size_t size,
									void* cookie);

private:
			CortexToneUnit*		fUnit;
};


static float
_Sine(float phase)
{
	return sinf(phase * 2.0f * 3.14159265f);
}


CortexToneControllable::CortexToneControllable(CortexToneUnit* unit)
	:
	fUnit(unit)
{
	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	BParameterGroup* group = web->MakeGroup("Cortex Tone");
	if (group != NULL) {
		group->MakeContinuousParameter(kToneParameterGain,
			B_MEDIA_RAW_AUDIO, "Gain", "gain", "", 0.0f, 1.0f, 0.01f);
	}

	SetParameterWeb(web);
	SetChangeHook(&CortexToneControllable::ChangeHook, this);
}


/* static */
void
CortexToneControllable::ChangeHook(int32 id, bigtime_t /*when*/,
	const void* value, size_t size, void* cookie)
{
	if (id != kToneParameterGain || value == NULL || size < sizeof(float))
		return;

	CortexToneControllable* self = (CortexToneControllable*)cookie;
	if (self->fUnit != NULL)
		self->fUnit->SetGain(*(const float*)value);
}


CortexToneOutput::CortexToneOutput(const char* name)
	:
	BMediaConnection(B_MEDIA_OUTPUT),
	BMediaOutput(name)
{
	BMediaFormat format;
	format.format.type = B_MEDIA_RAW_AUDIO;
	format.format.u.raw_audio = media_raw_audio_format::wildcard;
	SetAcceptedFormat(format);
}


status_t
CortexToneOutput::PrepareToConnect(BMediaFormat* format)
{
	if (format == NULL)
		return B_BAD_VALUE;
	if (format->format.type != B_MEDIA_RAW_AUDIO)
		return B_BAD_VALUE;
	if (format->format.u.raw_audio.channel_count == 0)
		format->format.u.raw_audio.channel_count = 2;
	if (format->format.u.raw_audio.frame_rate == 0)
		format->format.u.raw_audio.frame_rate = 48000;
	return B_OK;
}


status_t
CortexToneOutput::FormatProposal(BMediaFormat* format)
{
	return PrepareToConnect(format);
}


CortexToneUnit::CortexToneUnit(const char* name)
	:
	BMediaUnit(name, B_MEDIA_PLAYER),
	fOutput(NULL),
	fControllable(NULL),
	fGain(0.2f),
	fPhase(0.0f)
{
	fOutput = new(std::nothrow) CortexToneOutput("audio out");
	if (fOutput != NULL)
		RegisterOutput(fOutput);

	fControllable = new(std::nothrow) CortexToneControllable(this);
	SetControllable(fControllable);
}


CortexToneUnit::~CortexToneUnit()
{
	delete fControllable;
}


CortexToneControllable*
CortexToneUnit::ControllableInstance() const
{
	return fControllable;
}


void
CortexToneUnit::SetGain(float gain)
{
	if (gain < 0.0f)
		gain = 0.0f;
	if (gain > 1.0f)
		gain = 1.0f;
	fGain = gain;
}


void
CortexToneUnit::ProcessCycle(uint32 frameCount)
{
	if (fOutput == NULL || !fOutput->HasBinding()) {
		BMediaUnit::ProcessCycle(frameCount);
		return;
	}

	size_t size = 0;
	void* buffer = BufferFor(fOutput, &size);
	if (buffer == NULL || size < sizeof(float)) {
		BMediaUnit::ProcessCycle(frameCount);
		return;
	}

	float* samples = (float*)buffer;
	const size_t count = size / sizeof(float);
	const float gain = fGain;
	for (size_t i = 0; i < count; i++) {
		samples[i] = gain * _Sine(fPhase);
		fPhase += 440.0f / 48000.0f;
		if (fPhase >= 1.0f)
			fPhase -= 1.0f;
	}
}


CortexMidiOutputUnit::CortexMidiOutputUnit(const char* name)
	:
	BMidiUnit(name, B_MEDIA_PLAYER),
	fOutput(NULL)
{
	fOutput = new(std::nothrow) BMidiOutput("midi out");
	if (fOutput != NULL) {
		BMediaFormat format;
		format.format.type = B_MEDIA_MIDI;
		fOutput->SetAcceptedFormat(format);
		RegisterOutput(fOutput);
	}
}


CortexMidiOutputUnit::~CortexMidiOutputUnit()
{
}


status_t
CortexMidiOutputUnit::SendTestNote()
{
	if (fOutput == NULL)
		return B_NO_INIT;

	status_t status = fOutput->SendNoteOn(0, 60, 100, 0);
	if (status != B_OK)
		return status;
	return fOutput->SendNoteOff(0, 60, 100, 500000);
}


BMediaClient**
CreateCortexDemoClients(int32* outCount)
{
	const int32 kCount = 2;
	CortexToneUnit* tone = new(std::nothrow) CortexToneUnit("Cortex Tone");
	CortexMidiOutputUnit* midi
		= new(std::nothrow) CortexMidiOutputUnit("Cortex MIDI Out");

	BMediaClient* candidates[2] = { tone, midi };
	BMediaClient* valid[2];
	int32 created = 0;
	for (int32 i = 0; i < 2; i++) {
		if (candidates[i] == NULL)
			continue;
		if (candidates[i]->InitCheck() != B_OK) {
			delete candidates[i];
			continue;
		}
		valid[created++] = candidates[i];
	}

	if (outCount != NULL)
		*outCount = created;

	if (created == 0)
		return NULL;

	BMediaClient** result = new(std::nothrow) BMediaClient*[created];
	if (result == NULL)
		return NULL;
	for (int32 i = 0; i < created; i++)
		result[i] = valid[i];
	return result;
}
