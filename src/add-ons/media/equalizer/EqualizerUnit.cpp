/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "EqualizerUnit.h"

#include <new>
#include <string.h>

#include <MediaDefs.h>
#include <String.h>

#include <media2/ParameterWeb.h>



static const char* const kBandNames[EQ_BANDS] = {
	"31.25Hz", "62.5Hz", "125Hz", "250Hz", "500Hz",
	"1000Hz", "2000Hz", "4000Hz", "8000Hz", "16000Hz"
};


EqualizerUnit::EqualizerUnit(int32 channels)
	:
	BMediaUnit("Equalizer", B_MEDIA_FILTER),
	fChannelCount(channels),
	fControllable(NULL),
	fConfiguredRate(0),
	fMute(0),
	fByPass(0),
	fInitStatus(B_NO_INIT)
{
	if (fChannelCount < 1)
		fChannelCount = 1;
	if (fChannelCount > MAX_CHANNELS)
		fChannelCount = MAX_CHANNELS;

	for (int32 i = 0; i < MAX_CHANNELS; i++) {
		fInputs[i] = NULL;
		fOutputs[i] = NULL;
	}

	for (int32 i = 0; i < fChannelCount; i++) {
		BString inName;
		inName.SetToFormat("input_%d", i);
		BString outName;
		outName.SetToFormat("output_%d", i);

		fInputs[i] = new(std::nothrow) EqualizerInput(inName.String());
		fOutputs[i] = new(std::nothrow) EqualizerOutput(outName.String());
		if (fInputs[i] == NULL || fOutputs[i] == NULL) {
			fInitStatus = B_NO_MEMORY;
			return;
		}
		fInputs[i]->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
		fOutputs[i]->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

		fInitStatus = RegisterInput(fInputs[i]);
		if (fInitStatus != B_OK)
			return;
		fInitStatus = RegisterOutput(fOutputs[i]);
		if (fInitStatus != B_OK)
			return;
	}

	fControllable = new(std::nothrow) BControllable();
	if (fControllable == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	fControllable->SetChangeHook(&EqualizerUnit::_ChangeHook, this);
	SetControllable(fControllable);

	fInitStatus = B_OK;
}


EqualizerUnit::~EqualizerUnit()
{
	Stop();
	if (fControllable != NULL) {
		fControllable->UnbindNode();
		delete fControllable;
		fControllable = NULL;
	}
	for (int32 i = 0; i < MAX_CHANNELS; i++) {
		delete fInputs[i];
		fInputs[i] = NULL;
		delete fOutputs[i];
		fOutputs[i] = NULL;
	}
}


status_t
EqualizerUnit::InitCheck() const
{
	return fInitStatus;
}


void
EqualizerUnit::_ChangeHook(int32 id, bigtime_t, const void* value,
	size_t size, void* cookie)
{
	EqualizerUnit* unit = (EqualizerUnit*)cookie;
	if (unit != NULL)
		unit->_ApplyParam(id, value, size);
}


void
EqualizerUnit::_ApplyParam(int32 id, const void* value, size_t size)
{
	if (value == NULL || size == 0)
		return;

	if (id == kPropPreAmp && size >= sizeof(float)) {
		double v = (double)*(const float*)value;
		for (int32 i = 0; i < fChannelCount; i++)
			fEqualizer[i].SetPreAmp(v);
	} else if (id == kPropMute && size >= sizeof(int32)) {
		fMute.store(*(const int32*)value);
	} else if (id == kPropByPass && size >= sizeof(int32)) {
		fByPass.store(*(const int32*)value);
	} else if (id >= kPropBands && id < kPropBands + EQ_BANDS
			&& size >= sizeof(float)) {
		int band = id - kPropBands;
		double v = (double)*(const float*)value;
		for (int32 i = 0; i < fChannelCount; i++)
			fEqualizer[i].SetBand(band, v);
	}
}


status_t
EqualizerUnit::Start()
{
	status_t result = BMediaUnit::Start();
	if (result != B_OK)
		return result;

	_PublishAndBind();
	return B_OK;
}


void
EqualizerUnit::_PublishAndBind()
{
	uint32 nodeId = NodeId();
	if (nodeId == 0 || fControllable == NULL)
		return;

	uint32 propIds[3 + EQ_BANDS];
	const char* propNames[3 + EQ_BANDS];
	uint32 valueTypes[3 + EQ_BANDS];
	float minValues[3 + EQ_BANDS];
	float maxValues[3 + EQ_BANDS];
	float steps[3 + EQ_BANDS];
	float values[3 + EQ_BANDS];
	const void* valuePtrs[3 + EQ_BANDS];
	size_t valueSizes[3 + EQ_BANDS];

	int32 n = 0;
	propIds[n] = kPropPreAmp;
	propNames[n] = "PreAmp";
	valueTypes[n] = (uint32)B_MEDIA_PROP_FLOAT;
	minValues[n] = -8.0f;
	maxValues[n] = 8.0f;
	steps[n] = 0.1f;
	values[n] = (float)fEqualizer[0].PreAmp();
	valuePtrs[n] = &values[n];
	valueSizes[n] = sizeof(float);
	n++;

	propIds[n] = kPropMute;
	propNames[n] = "Mute";
	valueTypes[n] = (uint32)B_MEDIA_PROP_BOOL;
	minValues[n] = 0.0f;
	maxValues[n] = 1.0f;
	steps[n] = 1.0f;
	values[n] = (float)fMute.load();
	valuePtrs[n] = &values[n];
	valueSizes[n] = sizeof(float);
	n++;

	propIds[n] = kPropByPass;
	propNames[n] = "ByPass";
	valueTypes[n] = (uint32)B_MEDIA_PROP_BOOL;
	minValues[n] = 0.0f;
	maxValues[n] = 1.0f;
	steps[n] = 1.0f;
	values[n] = (float)fByPass.load();
	valuePtrs[n] = &values[n];
	valueSizes[n] = sizeof(float);
	n++;

	for (int b = 0; b < EQ_BANDS; b++) {
		propIds[n] = kPropBands + b;
		propNames[n] = kBandNames[b];
		valueTypes[n] = (uint32)B_MEDIA_PROP_FLOAT;
		minValues[n] = -16.0f;
		maxValues[n] = 16.0f;
		steps[n] = 0.1f;
		values[n] = (float)fEqualizer[0].Band(b);
		valuePtrs[n] = &values[n];
		valueSizes[n] = sizeof(float);
		n++;
	}

	PublishNodeParams(propIds, propNames, valueTypes, minValues, maxValues,
		steps, valuePtrs, valueSizes, n);

	// Every parameter round-trip goes through the SPA binding only.
	fControllable->BindNode(nodeId);
}


void
EqualizerUnit::ProcessCycle(uint32 frameCount)
{
	if (frameCount == 0)
		return;

	uint32 rate = SampleRate();
	if (rate != 0 && rate != fConfiguredRate.load()) {
		for (int32 i = 0; i < fChannelCount; i++)
			fEqualizer[i].SetFormat(1, (float)rate);
		fConfiguredRate.store(rate);
	}

	if (fMute.load() != 0 || fByPass.load() != 0)
		return;

	for (int32 i = 0; i < fChannelCount; i++) {
		if (fInputs[i] == NULL || fOutputs[i] == NULL)
			continue;

		size_t inSize = 0;
		size_t outSize = 0;
		float* in = (float*)BufferFor(fInputs[i], &inSize);
		float* out = (float*)BufferFor(fOutputs[i], &outSize);
		if (in == NULL || out == NULL)
			continue;

		if (in != out)
			memcpy(out, in, frameCount * sizeof(float));

		// Per-channel mono walk; DSP itself is the ported Equalizer code.
		fEqualizer[i].ProcessBuffer(out, (int)frameCount);
	}
}
