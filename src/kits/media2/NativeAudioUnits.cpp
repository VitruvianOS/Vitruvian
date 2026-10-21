/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/NativeAudioUnits.h>

#include <algorithm>
#include <math.h>
#include <new>
#include <string.h>

#include <media2/Controllable.h>


static const uint32 kPropBase = B_MEDIA_PROP_CUSTOM_BASE + 1;
static const uint32 kMaxSampleRate = 192000;


BNativeAudioUnit::BNativeAudioUnit(const char* name)
	:
	BMediaUnit(name, (media_client_kinds)(B_MEDIA_FILTER
		| B_MEDIA_CONTROLLABLE)),
	fInput(NULL),
	fOutput(NULL),
	fControllable(NULL),
	fParameterCount(0),
	fInitStatus(B_NO_INIT)
{
	memset(fParameters, 0, sizeof(fParameters));

	fInput = new(std::nothrow) BMediaInput("input");
	fOutput = new(std::nothrow) BMediaOutput("output");
	fControllable = new(std::nothrow) BControllable();
	if (fInput == NULL || fOutput == NULL || fControllable == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	fInput->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
	fOutput->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

	fInitStatus = RegisterInput(fInput);
	if (fInitStatus == B_OK)
		fInitStatus = RegisterOutput(fOutput);
	fControllable->SetChangeHook(&BNativeAudioUnit::_ChangeHook, this);
	SetControllable(fControllable);
}


BNativeAudioUnit::~BNativeAudioUnit()
{
	Stop();
	if (fControllable != NULL) {
		fControllable->UnbindNode();
		delete fControllable;
	}
	delete fInput;
	delete fOutput;
	for (int32 i = 0; i < fParameterCount; i++)
		delete fParameters[i];
}


status_t
BNativeAudioUnit::InitCheck() const
{
	return fInitStatus;
}


void
BNativeAudioUnit::AddParameter(const char* name, float minValue,
	float maxValue, float defaultValue, const char* kind, const char* unit)
{
	if (fParameterCount >= 16)
		return;
	Parameter* parameter = new(std::nothrow) Parameter();
	if (parameter == NULL)
		return;
	parameter->name = name;
	parameter->kind = kind != NULL ? kind : B_GENERIC;
	parameter->unit = unit != NULL ? unit : "";
	parameter->minValue = minValue;
	parameter->maxValue = maxValue;
	parameter->defaultValue = defaultValue;
	parameter->value.store(defaultValue);
	fParameters[fParameterCount++] = parameter;

	// The web is rebuilt so MakeView() always sees every parameter.
	_BuildWeb();
}


int32
BNativeAudioUnit::CountParameters() const
{
	return fParameterCount;
}


status_t
BNativeAudioUnit::GetParameterInfo(int32 index, BString* outName,
	float* outMin, float* outMax, float* outValue) const
{
	if (index < 0 || index >= fParameterCount)
		return B_BAD_INDEX;
	const Parameter* parameter = fParameters[index];
	if (outName != NULL)
		*outName = parameter->name;
	if (outMin != NULL)
		*outMin = parameter->minValue;
	if (outMax != NULL)
		*outMax = parameter->maxValue;
	if (outValue != NULL)
		*outValue = parameter->value.load();
	return B_OK;
}


status_t
BNativeAudioUnit::SetParameterValue(int32 index, float value)
{
	if (index < 0 || index >= fParameterCount)
		return B_BAD_INDEX;
	Parameter* parameter = fParameters[index];
	if (value < parameter->minValue)
		value = parameter->minValue;
	if (value > parameter->maxValue)
		value = parameter->maxValue;
	parameter->value.store(value);
	return B_OK;
}


float
BNativeAudioUnit::ParameterValue(int32 index) const
{
	if (index < 0 || index >= fParameterCount)
		return 0.0f;
	return fParameters[index]->value.load();
}


void
BNativeAudioUnit::_ChangeHook(int32 id, bigtime_t, const void* value,
	size_t size, void* cookie)
{
	BNativeAudioUnit* unit = (BNativeAudioUnit*)cookie;
	if (unit == NULL || value == NULL || size < sizeof(float))
		return;
	unit->SetParameterValue(id - (int32)kPropBase, *(const float*)value);
}


status_t
BNativeAudioUnit::Start()
{
	status_t status = BMediaUnit::Start();
	if (status == B_OK)
		_PublishAndBind();
	return status;
}


void
BNativeAudioUnit::_PublishAndBind()
{
	// Detached units have no node to publish on.
	uint32 nodeId = NodeId();
	if (nodeId == 0 || fControllable == NULL || fParameterCount == 0)
		return;

	uint32 propIds[16];
	const char* names[16];
	uint32 types[16];
	float minValues[16], maxValues[16], steps[16], values[16];
	const void* valuePointers[16];
	size_t valueSizes[16];

	for (int32 i = 0; i < fParameterCount; i++) {
		propIds[i] = kPropBase + i;
		names[i] = fParameters[i]->name.String();
		types[i] = (uint32)B_MEDIA_PROP_FLOAT;
		minValues[i] = fParameters[i]->minValue;
		maxValues[i] = fParameters[i]->maxValue;
		steps[i] = (maxValues[i] - minValues[i]) / 100.0f;
		values[i] = fParameters[i]->value.load();
		valuePointers[i] = &values[i];
		valueSizes[i] = sizeof(float);
	}

	PublishNodeParams(propIds, names, types, minValues, maxValues, steps,
		valuePointers, valueSizes, fParameterCount);
}


// The parameters get a local web, so MakeView() and the Control panel
// work without a server round trip; the change hook feeds the atomics
// the realtime cycle reads.
void
BNativeAudioUnit::_BuildWeb()
{
	if (fControllable == NULL)
		return;

	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	BParameterGroup* group = web->MakeGroup(Name());
	if (group == NULL) {
		delete web;
		return;
	}

	for (int32 i = 0; i < fParameterCount; i++) {
		Parameter* parameter = fParameters[i];
		BContinuousParameter* continuous
			= group->MakeContinuousParameter((int32)kPropBase + i,
				B_MEDIA_RAW_AUDIO, parameter->name.String(),
				parameter->kind.String(), parameter->unit.String(),
				parameter->minValue, parameter->maxValue,
				(parameter->maxValue - parameter->minValue) / 100.0f);
		if (continuous != NULL && parameter->minValue > 0.0f
			&& parameter->maxValue / parameter->minValue >= 100.0f) {
			continuous->SetFlags(B_LOGARITHMIC_PARAMETER);
		}
	}

	fControllable->SetParameterWeb(web);

	for (int32 i = 0; i < fParameterCount; i++) {
		const float value = fParameters[i]->value.load();
		fControllable->SetParameterValue((int32)kPropBase + i, system_time(),
			&value, sizeof(value));
	}
}


void
BNativeAudioUnit::ProcessCycle(uint32 frameCount)
{
	if (frameCount == 0)
		return;

	size_t inSize = 0;
	size_t outSize = 0;
	const float* in = (const float*)BufferFor(fInput, &inSize);
	float* out = (float*)BufferFor(fOutput, &outSize);
	if (out == NULL)
		return;
	if (in == NULL) {
		memset(out, 0, outSize);
		return;
	}

	size_t frames = inSize / sizeof(float);
	if (outSize / sizeof(float) < frames)
		frames = outSize / sizeof(float);
	if (frames > frameCount)
		frames = frameCount;

	Process(in, out, (uint32)frames, SampleRate());
}


// #pragma mark - BGainUnit


BGainUnit::BGainUnit()
	:
	BNativeAudioUnit("Gain")
{
	AddParameter("Gain", 0.0f, 4.0f, 1.0f, B_GAIN);
}


void
BGainUnit::Process(const float* in, float* out, uint32 frameCount, uint32)
{
	const float gain = ParameterValue(0);
	for (uint32 i = 0; i < frameCount; i++)
		out[i] = in[i] * gain;
}


// #pragma mark - BCompressorUnit


BCompressorUnit::BCompressorUnit()
	:
	BNativeAudioUnit("Compressor"),
	fEnvelope(0.0f)
{
	AddParameter("Threshold", -60.0f, 0.0f, -18.0f, B_COMPRESSION, "dB");
	AddParameter("Ratio", 1.0f, 20.0f, 4.0f);
	AddParameter("Attack", 1.0f, 200.0f, 10.0f, B_GENERIC, "ms");
	AddParameter("Release", 10.0f, 1000.0f, 100.0f, B_GENERIC, "ms");
	AddParameter("Makeup", 0.0f, 24.0f, 0.0f, B_GENERIC, "dB");
}


void
BCompressorUnit::Process(const float* in, float* out, uint32 frameCount,
	uint32 sampleRate)
{
	if (sampleRate == 0)
		sampleRate = 48000;

	const float threshold = ParameterValue(0);
	const float ratio = ParameterValue(1);
	const float attack = expf(-1.0f / (ParameterValue(2) * 0.001f * sampleRate));
	const float release = expf(-1.0f / (ParameterValue(3) * 0.001f * sampleRate));
	const float makeup = powf(10.0f, ParameterValue(4) / 20.0f);

	float envelope = fEnvelope;
	for (uint32 i = 0; i < frameCount; i++) {
		const float level = fabsf(in[i]);
		const float coefficient = level > envelope ? attack : release;
		envelope = level + coefficient * (envelope - level);

		float gain = 1.0f;
		if (envelope > 1e-6f) {
			const float db = 20.0f * log10f(envelope);
			if (db > threshold)
				gain = powf(10.0f, (threshold - db) * (1.0f - 1.0f / ratio)
					/ 20.0f);
		}
		out[i] = in[i] * gain * makeup;
	}
	fEnvelope = envelope;
}


// #pragma mark - BReverbUnit


// Freeverb-style: four parallel combs into two series allpasses.
static const uint32 kCombTunings[4] = { 1116, 1188, 1277, 1356 };
static const uint32 kAllpassTunings[2] = { 556, 441 };


BReverbUnit::BReverbUnit()
	:
	BNativeAudioUnit("Reverb"),
	fConfiguredRate(0)
{
	AddParameter("Room Size", 0.0f, 1.0f, 0.5f);
	AddParameter("Damping", 0.0f, 1.0f, 0.5f);
	AddParameter("Mix", 0.0f, 1.0f, 0.3f);

	// Sized for the highest rate so the cycle never allocates.
	const uint32 maxLength = (uint32)((uint64)kMaxSampleRate * 1356 / 44100)
		+ 1;
	for (int i = 0; i < 4; i++)
		fCombs[i].buffer.assign(maxLength, 0.0f);
	for (int i = 0; i < 2; i++)
		fAllpass[i].buffer.assign(maxLength, 0.0f);
}


void
BReverbUnit::_Configure(uint32 sampleRate)
{
	for (int i = 0; i < 4; i++) {
		fCombs[i].length = (uint32)((uint64)kCombTunings[i] * sampleRate
			/ 44100);
		fCombs[i].position = 0;
		fCombs[i].filter = 0.0f;
		std::fill(fCombs[i].buffer.begin(), fCombs[i].buffer.end(), 0.0f);
	}
	for (int i = 0; i < 2; i++) {
		fAllpass[i].length = (uint32)((uint64)kAllpassTunings[i] * sampleRate
			/ 44100);
		fAllpass[i].position = 0;
		fAllpass[i].filter = 0.0f;
		std::fill(fAllpass[i].buffer.begin(), fAllpass[i].buffer.end(), 0.0f);
	}
	fConfiguredRate = sampleRate;
}


void
BReverbUnit::Process(const float* in, float* out, uint32 frameCount,
	uint32 sampleRate)
{
	if (sampleRate == 0)
		sampleRate = 48000;
	if (sampleRate > kMaxSampleRate)
		sampleRate = kMaxSampleRate;
	if (sampleRate != fConfiguredRate)
		_Configure(sampleRate);

	const float feedback = 0.7f + ParameterValue(0) * 0.28f;
	const float damping = ParameterValue(1) * 0.4f;
	const float mix = ParameterValue(2);

	for (uint32 n = 0; n < frameCount; n++) {
		const float input = in[n];
		float wet = 0.0f;
		for (int i = 0; i < 4; i++) {
			Comb& comb = fCombs[i];
			const float delayed = comb.buffer[comb.position];
			comb.filter = delayed * (1.0f - damping) + comb.filter * damping;
			comb.buffer[comb.position] = input * 0.015f
				+ comb.filter * feedback;
			if (++comb.position >= comb.length)
				comb.position = 0;
			wet += delayed;
		}
		for (int i = 0; i < 2; i++) {
			Comb& ap = fAllpass[i];
			const float delayed = ap.buffer[ap.position];
			ap.buffer[ap.position] = wet + delayed * 0.5f;
			wet = delayed - wet;
			if (++ap.position >= ap.length)
				ap.position = 0;
		}
		out[n] = input * (1.0f - mix) + wet * mix * 3.0f;
	}
}
