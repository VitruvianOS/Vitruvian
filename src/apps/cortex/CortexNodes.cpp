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
	BSimpleMediaUnit(name, B_MEDIA_PLAYER),
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
	SetCycleHook(&CortexToneUnit::_Cycle, this);
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


/* static */ void
CortexToneUnit::_Cycle(void* cookie, BSimpleMediaUnit* unit,
	uint32 /*frameCount*/)
{
	CortexToneUnit* self = (CortexToneUnit*)cookie;
	if (self->fOutput == NULL)
		return;

	size_t size = 0;
	void* buffer = unit->BufferFor(self->fOutput, &size);
	if (buffer == NULL || size < sizeof(float))
		return;

	// Unlinked, the unit stays silent.
	if (!self->fOutput->HasBinding()) {
		memset(buffer, 0, size);
		return;
	}

	float* samples = (float*)buffer;
	const size_t count = size / sizeof(float);
	const float gain = self->fGain;
	for (size_t i = 0; i < count; i++) {
		samples[i] = gain * _Sine(self->fPhase);
		self->fPhase += 440.0f / 48000.0f;
		if (self->fPhase >= 1.0f)
			self->fPhase -= 1.0f;
	}
}


BMidiUnit*
CreateCortexMidiOut(const char* name)
{
	BMidiUnit* unit = new(std::nothrow) BMidiUnit(name, B_MEDIA_PLAYER);
	if (unit == NULL)
		return NULL;

	BMidiOutput* output = new(std::nothrow) BMidiOutput("midi out");
	if (output != NULL) {
		BMediaFormat format;
		format.format.type = B_MEDIA_MIDI;
		output->SetAcceptedFormat(format);
		unit->RegisterOutput(output);
	}
	return unit;
}


status_t
SendCortexTestNote(BMidiUnit* unit)
{
	BMidiOutput* output = unit != NULL ? unit->MidiOutputAt(0) : NULL;
	if (output == NULL)
		return B_NO_INIT;

	status_t status = output->SendNoteOn(0, 60, 100, 0);
	if (status != B_OK)
		return status;
	return output->SendNoteOff(0, 60, 100, 500000);
}


BMediaClient**
CreateCortexDemoClients(int32* outCount)
{
	CortexToneUnit* tone = new(std::nothrow) CortexToneUnit("Cortex Tone");
	BMidiUnit* midi = CreateCortexMidiOut("Cortex MIDI Out");

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
