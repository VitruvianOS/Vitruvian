/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/DynamicAudioUnits.h>

#include <math.h>
#include <new>
#include <string.h>

#include <Autolock.h>
#include <OS.h>

#include <media2/Controllable.h>


static const uint32 kMaxSampleRate = 192000;


// #pragma mark - BMixerUnit


class BMixerUnit::_Input : public BMediaInput {
public:
								_Input(const char* name)
									:
									BMediaConnection(B_MEDIA_INPUT, name),
									BMediaInput(name)
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
	}

	virtual						~_Input() {}
};


BMixerUnit::BMixerUnit()
	:
	BMediaUnit("Mixer", (media_client_kinds)(B_MEDIA_FILTER
		| B_MEDIA_CONTROLLABLE)),
	fOutputLeft(NULL),
	fOutputRight(NULL),
	fFreeInput(NULL),
	fControllable(NULL),
	fMasterGain(1.0f),
	fMasterMute(false),
	fLock("mixer unit"),
	fInitStatus(B_NO_INIT)
{
	for (int32 i = 0; i < kMaxChannels; i++) {
		fChannels[i].used.store(false);
		fChannels[i].connection = NULL;
		fChannels[i].gain.store(1.0f);
		fChannels[i].pan.store(0.0f);
		fChannels[i].level.store(0.0f);
		fChannels[i].mute.store(false);
		fChannels[i].solo.store(false);
	}

	fOutputLeft = new(std::nothrow) BMediaOutput("left");
	fOutputRight = new(std::nothrow) BMediaOutput("right");
	fControllable = new(std::nothrow) BControllable();
	if (fOutputLeft == NULL || fOutputRight == NULL
		|| fControllable == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	fOutputLeft->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
	fOutputRight->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

	fInitStatus = RegisterOutput(fOutputLeft);
	if (fInitStatus == B_OK)
		fInitStatus = RegisterOutput(fOutputRight);
	if (fInitStatus != B_OK)
		return;

	fControllable->SetChangeHook(&BMixerUnit::_ChangeHook, this);
	SetControllable(fControllable);
	_BuildWeb();
	_AddChannelInput();
}


BMixerUnit::~BMixerUnit()
{
	Stop();

	{
		BAutolock _(fLock);
		for (int32 i = 0; i < kMaxChannels; i++) {
			_Input* input = fChannels[i].connection;
			if (input == NULL)
				continue;
			fChannels[i].used.store(false);
			fChannels[i].connection = NULL;
			_RemoveLivePort(input);
			BMediaClient::UnregisterInput(input);
			delete input;
		}
		fFreeInput = NULL;
	}

	if (fOutputLeft != NULL) {
		UnregisterOutput(fOutputLeft);
		delete fOutputLeft;
	}
	if (fOutputRight != NULL) {
		UnregisterOutput(fOutputRight);
		delete fOutputRight;
	}
	delete fControllable;
}


status_t
BMixerUnit::InitCheck() const
{
	return fInitStatus;
}


int32
BMixerUnit::_IndexOf(BMediaInput* input) const
{
	for (int32 i = 0; i < kMaxChannels; i++) {
		if (fChannels[i].connection == input)
			return i;
	}
	return -1;
}


BParameterGroup*
BMixerUnit::_ChannelGroup(int32 slot)
{
	if (fControllable == NULL || fControllable->Web() == NULL)
		return NULL;
	return fControllable->Web()->GroupAt(slot + 1);
}


void
BMixerUnit::_BuildWeb()
{
	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	BParameterGroup* master = web->MakeGroup("Master");
	if (master == NULL) {
		delete web;
		return;
	}
	master->MakeContinuousParameter(kMasterGain, B_MEDIA_RAW_AUDIO,
		"Master Gain", B_MASTER_GAIN, "", 0.0f, 2.0f, 1.0f);
	BDiscreteParameter* mute = master->MakeDiscreteParameter(kMasterMute,
		B_MEDIA_RAW_AUDIO, "Master Mute", B_MUTE);
	if (mute != NULL) {
		mute->AddItem(0, "Off");
		mute->AddItem(1, "On");
	}

	// Every slot gets its group up front; unused ones stay hidden until
	// an input claims the slot, so the web never has to be rebuilt and
	// an open Control panel keeps valid parameter pointers.
	for (int32 i = 0; i < kMaxChannels; i++) {
		BString name;
		name.SetToFormat("Input %d", i + 1);
		BParameterGroup* group = web->MakeGroup(name.String());
		if (group == NULL)
			continue;
		const int32 base = kChannelBase + i * kChannelStride;
		group->MakeContinuousParameter(base + 0, B_MEDIA_RAW_AUDIO,
			"Gain", B_GAIN, "", 0.0f, 4.0f, 1.0f);
		group->MakeContinuousParameter(base + 1, B_MEDIA_RAW_AUDIO,
			"Pan", B_BALANCE, "", -1.0f, 1.0f, 0.0f);
		BDiscreteParameter* inputMute = group->MakeDiscreteParameter(
			base + 2, B_MEDIA_RAW_AUDIO, "Mute", B_MUTE);
		if (inputMute != NULL) {
			inputMute->AddItem(0, "Off");
			inputMute->AddItem(1, "On");
		}
		BDiscreteParameter* solo = group->MakeDiscreteParameter(base + 3,
			B_MEDIA_RAW_AUDIO, "Solo", B_SWITCH);
		if (solo != NULL) {
			solo->AddItem(0, "Off");
			solo->AddItem(1, "On");
		}
		BContinuousParameter* level = group->MakeContinuousParameter(
			base + 4, B_MEDIA_RAW_AUDIO, "Level", B_LEVEL, "", 0.0f, 1.0f,
			0.01f);
		if (level != NULL)
			level->SetFlags(B_HIDDEN_PARAMETER);
		group->SetFlags(B_HIDDEN_PARAMETER);
	}

	fControllable->SetParameterWeb(web);
}


/* static */ void
BMixerUnit::_ChangeHook(int32 id, bigtime_t /*when*/, const void* value,
	size_t size, void* cookie)
{
	BMixerUnit* unit = (BMixerUnit*)cookie;
	if (unit == NULL || value == NULL || size == 0)
		return;

	float floatValue = 0.0f;
	int32 intValue = 0;
	if (size == sizeof(int32))
		intValue = *(const int32*)value;
	if (size >= sizeof(float))
		floatValue = *(const float*)value;

	if (id == (int32)kMasterGain) {
		if (floatValue < 0.0f)
			floatValue = 0.0f;
		if (floatValue > 2.0f)
			floatValue = 2.0f;
		unit->fMasterGain.store(floatValue);
		return;
	}
	if (id == (int32)kMasterMute) {
		unit->fMasterMute.store(intValue != 0);
		return;
	}
	if (id < (int32)kChannelBase)
		return;

	const int32 slot = (id - (int32)kChannelBase) / (int32)kChannelStride;
	const int32 member = (id - (int32)kChannelBase) % (int32)kChannelStride;
	if (slot < 0 || slot >= kMaxChannels)
		return;

	Channel& channel = unit->fChannels[slot];
	switch (member) {
		case 0:
			if (floatValue < 0.0f)
				floatValue = 0.0f;
			if (floatValue > 4.0f)
				floatValue = 4.0f;
			channel.gain.store(floatValue);
			break;
		case 1:
			if (floatValue < -1.0f)
				floatValue = -1.0f;
			if (floatValue > 1.0f)
				floatValue = 1.0f;
			channel.pan.store(floatValue);
			break;
		case 2:
			channel.mute.store(intValue != 0);
			break;
		case 3:
			channel.solo.store(intValue != 0);
			break;
		default:
			break;
	}
}


void
BMixerUnit::_AddChannelInput()
{
	BAutolock _(fLock);

	int32 slot = -1;
	for (int32 i = 0; i < kMaxChannels; i++) {
		if (!fChannels[i].used.load()) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return;

	BString name;
	name.SetToFormat("in %d", slot + 1);
	_Input* input = new(std::nothrow) _Input(name.String());
	if (input == NULL)
		return;

	// BMediaUnit::RegisterInput refuses while started; growing the pin
	// set is exactly what this unit is for.
	if (BMediaClient::RegisterInput(input) != B_OK) {
		delete input;
		return;
	}
	_AddLivePort(input);

	fChannels[slot].connection = input;
	fChannels[slot].gain.store(1.0f);
	fChannels[slot].pan.store(0.0f);
	fChannels[slot].mute.store(false);
	fChannels[slot].solo.store(false);
	fChannels[slot].used.store(true);
	fFreeInput = input;

	BParameterGroup* group = _ChannelGroup(slot);
	if (group != NULL)
		group->SetFlags(0);
}


void
BMixerUnit::_RemoveChannelInput(_Input* input)
{
	BAutolock _(fLock);

	const int32 slot = _IndexOf(input);
	if (slot < 0)
		return;

	// The realtime cycle stops at used == false; the port goes away
	// under the backend lock, and the grace period lets a cycle that
	// already picked the channel finish before the input is deleted.
	fChannels[slot].used.store(false);
	_RemoveLivePort(input);
	BMediaClient::UnregisterInput(input);
	if (fFreeInput == input)
		fFreeInput = NULL;
	fChannels[slot].connection = NULL;
	snooze(50000);
	delete input;

	BParameterGroup* group = _ChannelGroup(slot);
	if (group != NULL)
		group->SetFlags(B_HIDDEN_PARAMETER);
}


void
BMixerUnit::HandleLink(BMediaConnection* local, BMediaConnection* /*peer*/)
{
	BMediaInput* input = dynamic_cast<BMediaInput*>(local);
	if (input == NULL)
		return;

	{
		BAutolock _(fLock);
		if (_IndexOf(input) < 0 || input != fFreeInput)
			return;
		fFreeInput = NULL;
	}

	// The free input just got a link; keep the next one ready.
	_AddChannelInput();
}


void
BMixerUnit::HandleUnlink(BMediaConnection* local, BMediaConnection* /*peer*/)
{
	_Input* input = dynamic_cast<_Input*>(local);
	if (input == NULL)
		return;
	if (_IndexOf(input) < 0)
		return;
	_RemoveChannelInput(input);
}


void
BMixerUnit::ProcessCycle(uint32 frameCount)
{
	size_t leftSize = 0;
	size_t rightSize = 0;
	float* left = (float*)BufferFor(fOutputLeft, &leftSize);
	float* right = (float*)BufferFor(fOutputRight, &rightSize);
	if (left == NULL || right == NULL)
		return;

	uint32 frames = (uint32)(leftSize / sizeof(float));
	if (rightSize / sizeof(float) < frames)
		frames = (uint32)(rightSize / sizeof(float));
	if (frames > frameCount)
		frames = frameCount;

	memset(left, 0, frames * sizeof(float));
	memset(right, 0, frames * sizeof(float));

	bool anySolo = false;
	for (int32 i = 0; i < kMaxChannels; i++) {
		if (fChannels[i].used.load() && fChannels[i].solo.load())
			anySolo = true;
	}

	for (int32 i = 0; i < kMaxChannels; i++) {
		Channel& channel = fChannels[i];
		if (!channel.used.load())
			continue;
		if (channel.mute.load())
			continue;
		if (anySolo && !channel.solo.load())
			continue;

		size_t size = 0;
		const float* in = (const float*)BufferFor(channel.connection, &size);
		if (in == NULL)
			continue;

		uint32 count = (uint32)(size / sizeof(float));
		if (count > frames)
			count = frames;

		const float gain = channel.gain.load();
		const float pan = channel.pan.load();
		const float leftGain = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
		const float rightGain = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);

		float peak = 0.0f;
		for (uint32 s = 0; s < count; s++) {
			const float sample = in[s];
			left[s] += sample * leftGain;
			right[s] += sample * rightGain;
			const float magnitude = sample < 0.0f ? -sample : sample;
			if (magnitude > peak)
				peak = magnitude;
		}
		channel.level.store(peak * gain);
	}

	if (fMasterMute.load()) {
		memset(left, 0, frames * sizeof(float));
		memset(right, 0, frames * sizeof(float));
		return;
	}

	const float master = fMasterGain.load();
	if (master == 1.0f)
		return;
	for (uint32 s = 0; s < frames; s++) {
		left[s] *= master;
		right[s] *= master;
	}
}


// #pragma mark - BSplitterUnit


class BSplitterUnit::_Output : public BMediaOutput {
public:
								_Output(const char* name)
									:
									BMediaConnection(B_MEDIA_OUTPUT, name),
									BMediaOutput(name)
	{
		SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
	}

	virtual						~_Output() {}
};


BSplitterUnit::BSplitterUnit()
	:
	BMediaUnit("Splitter", (media_client_kinds)(B_MEDIA_FILTER
		| B_MEDIA_CONTROLLABLE)),
	fInput(NULL),
	fFreeOutput(NULL),
	fControllable(NULL),
	fLock("splitter unit"),
	fInitStatus(B_NO_INIT)
{
	for (int32 i = 0; i < kMaxOutputs; i++) {
		fChannels[i].used.store(false);
		fChannels[i].connection = NULL;
		fChannels[i].gain.store(1.0f);
		fChannels[i].mute.store(false);
	}

	fInput = new(std::nothrow) BMediaInput("in");
	fControllable = new(std::nothrow) BControllable();
	if (fInput == NULL || fControllable == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	fInput->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

	fInitStatus = RegisterInput(fInput);
	if (fInitStatus != B_OK)
		return;

	fControllable->SetChangeHook(&BSplitterUnit::_ChangeHook, this);
	SetControllable(fControllable);
	_BuildWeb();
	_AddChannelOutput();
}


BSplitterUnit::~BSplitterUnit()
{
	Stop();

	{
		BAutolock _(fLock);
		for (int32 i = 0; i < kMaxOutputs; i++) {
			_Output* output = fChannels[i].connection;
			if (output == NULL)
				continue;
			fChannels[i].used.store(false);
			fChannels[i].connection = NULL;
			_RemoveLivePort(output);
			BMediaClient::UnregisterOutput(output);
			delete output;
		}
		fFreeOutput = NULL;
	}

	if (fInput != NULL) {
		UnregisterInput(fInput);
		delete fInput;
	}
	delete fControllable;
}


status_t
BSplitterUnit::InitCheck() const
{
	return fInitStatus;
}


int32
BSplitterUnit::_IndexOf(BMediaOutput* output) const
{
	for (int32 i = 0; i < kMaxOutputs; i++) {
		if (fChannels[i].connection == output)
			return i;
	}
	return -1;
}


BParameterGroup*
BSplitterUnit::_ChannelGroup(int32 slot)
{
	if (fControllable == NULL || fControllable->Web() == NULL)
		return NULL;
	return fControllable->Web()->GroupAt(slot);
}


void
BSplitterUnit::_BuildWeb()
{
	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	for (int32 i = 0; i < kMaxOutputs; i++) {
		BString name;
		name.SetToFormat("Output %d", i + 1);
		BParameterGroup* group = web->MakeGroup(name.String());
		if (group == NULL)
			continue;
		const int32 base = kChannelBase + i * kChannelStride;
		group->MakeContinuousParameter(base + 0, B_MEDIA_RAW_AUDIO,
			"Gain", B_GAIN, "", 0.0f, 4.0f, 1.0f);
		BDiscreteParameter* mute = group->MakeDiscreteParameter(base + 1,
			B_MEDIA_RAW_AUDIO, "Mute", B_MUTE);
		if (mute != NULL) {
			mute->AddItem(0, "Off");
			mute->AddItem(1, "On");
		}
		group->SetFlags(B_HIDDEN_PARAMETER);
	}

	fControllable->SetParameterWeb(web);
}


/* static */ void
BSplitterUnit::_ChangeHook(int32 id, bigtime_t /*when*/, const void* value,
	size_t size, void* cookie)
{
	BSplitterUnit* unit = (BSplitterUnit*)cookie;
	if (unit == NULL || value == NULL || size == 0
		|| id < (int32)kChannelBase) {
		return;
	}

	float floatValue = 0.0f;
	int32 intValue = 0;
	if (size == sizeof(int32))
		intValue = *(const int32*)value;
	if (size >= sizeof(float))
		floatValue = *(const float*)value;

	const int32 slot = (id - (int32)kChannelBase) / (int32)kChannelStride;
	const int32 member = (id - (int32)kChannelBase) % (int32)kChannelStride;
	if (slot < 0 || slot >= kMaxOutputs)
		return;

	Channel& channel = unit->fChannels[slot];
	if (member == 0) {
		if (floatValue < 0.0f)
			floatValue = 0.0f;
		if (floatValue > 4.0f)
			floatValue = 4.0f;
		channel.gain.store(floatValue);
	} else if (member == 1) {
		channel.mute.store(intValue != 0);
	}
}


void
BSplitterUnit::_AddChannelOutput()
{
	BAutolock _(fLock);

	int32 slot = -1;
	for (int32 i = 0; i < kMaxOutputs; i++) {
		if (!fChannels[i].used.load()) {
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return;

	BString name;
	name.SetToFormat("out %d", slot + 1);
	_Output* output = new(std::nothrow) _Output(name.String());
	if (output == NULL)
		return;

	if (BMediaClient::RegisterOutput(output) != B_OK) {
		delete output;
		return;
	}
	_AddLivePort(output);

	fChannels[slot].connection = output;
	fChannels[slot].gain.store(1.0f);
	fChannels[slot].mute.store(false);
	fChannels[slot].used.store(true);
	fFreeOutput = output;

	BParameterGroup* group = _ChannelGroup(slot);
	if (group != NULL)
		group->SetFlags(0);
}


void
BSplitterUnit::_RemoveChannelOutput(_Output* output)
{
	BAutolock _(fLock);

	const int32 slot = _IndexOf(output);
	if (slot < 0)
		return;

	fChannels[slot].used.store(false);
	_RemoveLivePort(output);
	BMediaClient::UnregisterOutput(output);
	if (fFreeOutput == output)
		fFreeOutput = NULL;
	fChannels[slot].connection = NULL;
	snooze(50000);
	delete output;

	BParameterGroup* group = _ChannelGroup(slot);
	if (group != NULL)
		group->SetFlags(B_HIDDEN_PARAMETER);
}


void
BSplitterUnit::HandleLink(BMediaConnection* local, BMediaConnection* /*peer*/)
{
	BMediaOutput* output = dynamic_cast<BMediaOutput*>(local);
	if (output == NULL)
		return;

	{
		BAutolock _(fLock);
		if (_IndexOf(output) < 0 || output != fFreeOutput)
			return;
		fFreeOutput = NULL;
	}

	// The free output just got a link; keep the next one ready.
	_AddChannelOutput();
}


void
BSplitterUnit::HandleUnlink(BMediaConnection* local, BMediaConnection* /*peer*/)
{
	_Output* output = dynamic_cast<_Output*>(local);
	if (output == NULL)
		return;
	if (_IndexOf(output) < 0)
		return;
	_RemoveChannelOutput(output);
}


void
BSplitterUnit::ProcessCycle(uint32 frameCount)
{
	size_t inSize = 0;
	const float* in = (const float*)BufferFor(fInput, &inSize);
	const uint32 inFrames = (uint32)(inSize / sizeof(float));

	for (int32 i = 0; i < kMaxOutputs; i++) {
		Channel& channel = fChannels[i];
		if (!channel.used.load())
			continue;

		size_t outSize = 0;
		float* out = (float*)BufferFor(channel.connection, &outSize);
		if (out == NULL)
			continue;

		uint32 frames = (uint32)(outSize / sizeof(float));
		if (frames > frameCount)
			frames = frameCount;

		if (in == NULL || channel.mute.load()) {
			memset(out, 0, frames * sizeof(float));
			continue;
		}

		const float gain = channel.gain.load();
		const uint32 count = frames < inFrames ? frames : inFrames;
		for (uint32 s = 0; s < count; s++)
			out[s] = in[s] * gain;
		if (frames > count)
			memset(out + count, 0, (frames - count) * sizeof(float));
	}
}


// #pragma mark - BToneUnit


BToneUnit::BToneUnit(const char* name)
	:
	BMediaUnit(name != NULL ? name : "Tone", (media_client_kinds)
		(B_MEDIA_PLAYER | B_MEDIA_CONTROLLABLE)),
	fOutput(NULL),
	fControllable(NULL),
	fFrequency(440.0f),
	fGain(0.2f),
	fWaveform(kSine),
	fPhase(0.0f),
	fNoiseState(0x12345678),
	fInitStatus(B_NO_INIT)
{
	fOutput = new(std::nothrow) BMediaOutput("tone out");
	fControllable = new(std::nothrow) BControllable();
	if (fOutput == NULL || fControllable == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	fOutput->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));

	fInitStatus = RegisterOutput(fOutput);
	if (fInitStatus != B_OK)
		return;

	fControllable->SetChangeHook(&BToneUnit::_ChangeHook, this);
	SetControllable(fControllable);
	_BuildWeb();
}


BToneUnit::~BToneUnit()
{
	Stop();
	if (fOutput != NULL) {
		UnregisterOutput(fOutput);
		delete fOutput;
	}
	delete fControllable;
}


status_t
BToneUnit::InitCheck() const
{
	return fInitStatus;
}


void
BToneUnit::_BuildWeb()
{
	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	BParameterGroup* group = web->MakeGroup(Name());
	if (group == NULL) {
		delete web;
		return;
	}

	BContinuousParameter* frequency = group->MakeContinuousParameter(
		kFrequency, B_MEDIA_RAW_AUDIO, "Frequency", B_FREQUENCY, "Hz",
		20.0f, 20000.0f, 440.0f);
	if (frequency != NULL)
		frequency->SetFlags(B_LOGARITHMIC_PARAMETER);

	BDiscreteParameter* waveform = group->MakeDiscreteParameter(kWaveform,
		B_MEDIA_RAW_AUDIO, "Waveform", B_GENERIC);
	if (waveform != NULL) {
		waveform->AddItem(kSine, "Sine");
		waveform->AddItem(kSquare, "Square");
		waveform->AddItem(kSaw, "Saw");
		waveform->AddItem(kNoise, "Noise");
	}

	group->MakeContinuousParameter(kGain, B_MEDIA_RAW_AUDIO, "Gain", B_GAIN,
		"", 0.0f, 1.0f, 0.2f);

	fControllable->SetParameterWeb(web);
}


/* static */ void
BToneUnit::_ChangeHook(int32 id, bigtime_t /*when*/, const void* value,
	size_t size, void* cookie)
{
	BToneUnit* unit = (BToneUnit*)cookie;
	if (unit == NULL || value == NULL || size == 0)
		return;

	float floatValue = 0.0f;
	int32 intValue = 0;
	if (size == sizeof(int32))
		intValue = *(const int32*)value;
	if (size >= sizeof(float))
		floatValue = *(const float*)value;

	if (id == (int32)kFrequency) {
		if (floatValue < 20.0f)
			floatValue = 20.0f;
		if (floatValue > 20000.0f)
			floatValue = 20000.0f;
		unit->fFrequency.store(floatValue);
	} else if (id == (int32)kWaveform) {
		if (intValue < kSine || intValue > kNoise)
			return;
		unit->fWaveform.store(intValue);
	} else if (id == (int32)kGain) {
		if (floatValue < 0.0f)
			floatValue = 0.0f;
		if (floatValue > 1.0f)
			floatValue = 1.0f;
		unit->fGain.store(floatValue);
	}
}


void
BToneUnit::ProcessCycle(uint32 frameCount)
{
	size_t size = 0;
	float* out = (float*)BufferFor(fOutput, &size);
	if (out == NULL)
		return;

	uint32 frames = (uint32)(size / sizeof(float));
	if (frames > frameCount)
		frames = frameCount;

	// The graph's real rate, not a hard-coded 48 kHz.
	uint32 sampleRate = SampleRate();
	if (sampleRate == 0 || sampleRate > kMaxSampleRate)
		sampleRate = 48000;

	const float gain = fGain.load();
	const int32 waveform = fWaveform.load();
	float phase = fPhase;
	const float step = fFrequency.load() / (float)sampleRate;
	uint32 noise = fNoiseState;

	for (uint32 s = 0; s < frames; s++) {
		float sample = 0.0f;
		switch (waveform) {
			case kSquare:
				sample = phase < 0.5f ? 1.0f : -1.0f;
				break;
			case kSaw:
				sample = 2.0f * phase - 1.0f;
				break;
			case kNoise:
				noise = noise * 1664525u + 1013904223u;
				sample = (float)(int32)noise / 2147483648.0f;
				break;
			case kSine:
			default:
				sample = sinf(phase * 2.0f * 3.14159265f);
				break;
		}
		out[s] = sample * gain;

		phase += step;
		if (phase >= 1.0f)
			phase -= 1.0f;
	}

	fPhase = phase;
	fNoiseState = noise;
}
