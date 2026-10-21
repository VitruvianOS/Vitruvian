/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/NativeMidiUnits.h>

#include <algorithm>
#include <math.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <File.h>
#include <OS.h>
#include <SupportDefs.h>

#include <media2/MediaConnection.h>


static const uint8 kNoteOffStatus = 0x80;
static const uint8 kNoteOnStatus = 0x90;


static int32
_ClampInt(int32 value, int32 minValue, int32 maxValue)
{
	if (value < minValue)
		return minValue;
	if (value > maxValue)
		return maxValue;
	return value;
}


// #pragma mark - BMidiNativeUnit


BMidiNativeUnit::BMidiNativeUnit(const char* name, media_client_kinds kinds)
	:
	BMidiUnit(name, kinds),
	fParamCount(0),
	fControllable(NULL),
	fFlushPending(false),
	fInitStatus(B_OK)
{
	memset(fParams, 0, sizeof(fParams));

	fControllable = new(std::nothrow) BControllable();
	if (fControllable == NULL)
		fInitStatus = B_NO_MEMORY;
}


BMidiNativeUnit::~BMidiNativeUnit()
{
	Stop();
	if (fControllable != NULL) {
		fControllable->UnbindNode();
		delete fControllable;
	}
	for (int32 i = 0; i < fParamCount; i++)
		delete fParams[i];
}


status_t
BMidiNativeUnit::InitCheck() const
{
	return fInitStatus;
}


BControllable*
BMidiNativeUnit::ControllableInstance() const
{
	return fControllable;
}


void
BMidiNativeUnit::AddContinuousParam(int32 id, const char* name, float minValue,
	float maxValue, float defaultValue)
{
	if (id < 0 || id >= 32 || fParamCount >= 32 || fParams[id] != NULL)
		return;

	Param* param = new(std::nothrow) Param();
	if (param == NULL)
		return;
	param->name = name;
	param->minValue = minValue;
	param->maxValue = maxValue;
	param->defaultValue = defaultValue;
	param->floatValue.store(defaultValue);
	param->intValue.store((int32)defaultValue);
	param->items = NULL;
	param->itemCount = 0;
	param->maxBytes = 0;
	param->type = 0;
	fParams[id] = param;
	if (id >= fParamCount)
		fParamCount = id + 1;
}


void
BMidiNativeUnit::AddDiscreteParam(int32 id, const char* name,
	int32 defaultValue, const char* const* items, int32 itemCount,
	const char* kind)
{
	if (id < 0 || id >= 32 || fParamCount >= 32 || fParams[id] != NULL)
		return;

	Param* param = new(std::nothrow) Param();
	if (param == NULL)
		return;
	param->name = name;
	param->minValue = 0;
	param->maxValue = itemCount > 0 ? itemCount - 1 : 0;
	param->defaultValue = (float)defaultValue;
	param->floatValue.store((float)defaultValue);
	param->intValue.store(defaultValue);
	param->items = items;
	param->itemCount = itemCount;
	param->maxBytes = 0;
	param->type = 1;
	// The kind string is stashed in textValue's sibling via name copy.
	param->textValue = kind != NULL ? kind : "";
	fParams[id] = param;
	if (id >= fParamCount)
		fParamCount = id + 1;
}


void
BMidiNativeUnit::AddTextParam(int32 id, const char* name, size_t maxBytes)
{
	if (id < 0 || id >= 32 || fParamCount >= 32 || fParams[id] != NULL)
		return;

	Param* param = new(std::nothrow) Param();
	if (param == NULL)
		return;
	param->name = name;
	param->minValue = 0;
	param->maxValue = 0;
	param->defaultValue = 0;
	param->floatValue.store(0);
	param->intValue.store(0);
	param->items = NULL;
	param->itemCount = 0;
	param->maxBytes = maxBytes;
	param->type = 2;
	fParams[id] = param;
	if (id >= fParamCount)
		fParamCount = id + 1;
}


void
BMidiNativeUnit::FinishParameters(const char* groupName)
{
	if (fControllable == NULL || fParamCount == 0)
		return;

	BParameterWeb* web = new(std::nothrow) BParameterWeb();
	if (web == NULL)
		return;

	BParameterGroup* group = web->MakeGroup(groupName);
	if (group == NULL) {
		delete web;
		return;
	}

	for (int32 i = 0; i < fParamCount; i++) {
		Param* param = fParams[i];
		if (param == NULL)
			continue;
		if (param->type == 1) {
			const char* kind = param->textValue.String();
			BDiscreteParameter* discrete = group->MakeDiscreteParameter(i,
				B_MEDIA_MIDI, param->name.String(),
				kind[0] != '\0' ? kind : B_GENERIC);
			if (discrete != NULL) {
				if (param->items != NULL) {
					for (int32 item = 0; item < param->itemCount; item++)
						discrete->AddItem(item, param->items[item]);
				} else
					discrete->AddItem((int32)param->defaultValue,
						param->name.String());
			}
		} else if (param->type == 2) {
			group->MakeTextParameter(i, B_MEDIA_MIDI, param->name.String(),
				B_GENERIC, param->maxBytes);
		} else {
			group->MakeContinuousParameter(i, B_MEDIA_MIDI,
				param->name.String(), B_GENERIC, "",
				param->minValue, param->maxValue,
				(param->maxValue - param->minValue) / 100.0f);
		}
	}

	fControllable->SetParameterWeb(web);
	fControllable->SetChangeHook(&BMidiNativeUnit::_ChangeHook, this);

	// Seed the controllable so the control panel reads the defaults back.
	for (int32 i = 0; i < fParamCount; i++) {
		Param* param = fParams[i];
		if (param == NULL || param->type == 2)
			continue;
		if (param->type == 1) {
			int32 value = param->intValue.load();
			fControllable->SetParameterValue(i, system_time(), &value,
				sizeof(value));
		} else {
			float value = param->floatValue.load();
			fControllable->SetParameterValue(i, system_time(), &value,
				sizeof(value));
		}
	}
}


float
BMidiNativeUnit::FloatParam(int32 id) const
{
	Param* param = ParamAt(id);
	return param != NULL ? param->floatValue.load() : 0.0f;
}


int32
BMidiNativeUnit::IntParam(int32 id) const
{
	Param* param = ParamAt(id);
	return param != NULL ? param->intValue.load() : 0;
}


const char*
BMidiNativeUnit::TextParam(int32 id) const
{
	Param* param = ParamAt(id);
	return param != NULL ? param->textValue.String() : "";
}


status_t
BMidiNativeUnit::SetParameter(int32 id, float value)
{
	if (fControllable == NULL)
		return B_NO_INIT;

	Param* param = ParamAt(id);
	if (param == NULL)
		return B_BAD_VALUE;

	if (param->type == 1) {
		int32 intValue = _ClampInt((int32)lroundf(value),
			(int32)param->minValue, (int32)param->maxValue);
		return fControllable->SetParameterValue(id, system_time(), &intValue,
			sizeof(intValue));
	}

	if (param->type == 2)
		return B_BAD_VALUE;

	if (value < param->minValue)
		value = param->minValue;
	if (value > param->maxValue)
		value = param->maxValue;
	return fControllable->SetParameterValue(id, system_time(), &value,
		sizeof(value));
}


void
BMidiNativeUnit::RequestFlush()
{
	fFlushPending.store(true);
}


bool
BMidiNativeUnit::FlushPending() const
{
	return fFlushPending.load();
}


void
BMidiNativeUnit::ConsumeFlush()
{
	fFlushPending.store(false);
}


void
BMidiNativeUnit::ParameterChanged(int32 /*id*/)
{
}


BMidiNativeUnit::Param*
BMidiNativeUnit::ParamAt(int32 id) const
{
	if (id < 0 || id >= fParamCount)
		return NULL;
	return fParams[id];
}


/* static */ void
BMidiNativeUnit::_ChangeHook(int32 id, bigtime_t /*when*/, const void* value,
	size_t size, void* cookie)
{
	BMidiNativeUnit* self = (BMidiNativeUnit*)cookie;
	if (self == NULL || value == NULL)
		return;

	Param* param = self->ParamAt(id);
	if (param == NULL)
		return;

	if (param->type == 1) {
		if (size < sizeof(int32))
			return;
		int32 intValue = *(const int32*)value;
		intValue = _ClampInt(intValue, (int32)param->minValue,
			(int32)param->maxValue);
		param->intValue.store(intValue);
		param->floatValue.store((float)intValue);
	} else if (param->type == 2) {
		size_t use = size < param->maxBytes ? size : param->maxBytes;
		param->textValue.SetTo((const char*)value, use);
	} else {
		if (size < sizeof(float))
			return;
		float floatValue = *(const float*)value;
		if (floatValue < param->minValue)
			floatValue = param->minValue;
		if (floatValue > param->maxValue)
			floatValue = param->maxValue;
		param->floatValue.store(floatValue);
		param->intValue.store((int32)floatValue);
	}

	self->ParameterChanged(id);
}


// #pragma mark - BMidiThruUnit


class BMidiThruUnit::ThruInput : public BMidiInput {
public:
								ThruInput(BMidiThruUnit* unit,
									const char* name);

protected:
	virtual	void				NoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				NoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				KeyPressure(uchar channel, uchar note,
									uchar pressure, bigtime_t time) override;
	virtual	void				ControlChange(uchar channel,
									uchar controlNumber, uchar controlValue,
									bigtime_t time) override;
	virtual	void				ProgramChange(uchar channel,
									uchar programNumber, bigtime_t time) override;
	virtual	void				ChannelPressure(uchar channel,
									uchar pressure, bigtime_t time) override;
	virtual	void				PitchBend(uchar channel, uchar lsb,
									uchar msb, bigtime_t time) override;
	virtual	void				SystemExclusive(void* data, size_t length,
									bigtime_t time) override;
	virtual	void				SystemCommon(uchar status, uchar data1,
									uchar data2, bigtime_t time) override;
	virtual	void				SystemRealTime(uchar status,
									bigtime_t time) override;

private:
			BMidiThruUnit*		fUnit;
};


BMidiThruUnit::ThruInput::ThruInput(BMidiThruUnit* unit, const char* name)
	:
	// Virtual base must be named by the most-derived class.
	BMediaConnection(B_MEDIA_INPUT, name),
	BMidiInput(name),
	fUnit(unit)
{
}


void
BMidiThruUnit::ThruInput::NoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	fUnit->HandleNoteOff(channel, note, velocity, time);
}


void
BMidiThruUnit::ThruInput::NoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	fUnit->HandleNoteOn(channel, note, velocity, time);
}


void
BMidiThruUnit::ThruInput::KeyPressure(uchar channel, uchar note,
	uchar pressure, bigtime_t time)
{
	fUnit->HandleKeyPressure(channel, note, pressure, time);
}


void
BMidiThruUnit::ThruInput::ControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	fUnit->HandleControlChange(channel, controlNumber, controlValue, time);
}


void
BMidiThruUnit::ThruInput::ProgramChange(uchar channel, uchar programNumber,
	bigtime_t time)
{
	fUnit->HandleProgramChange(channel, programNumber, time);
}


void
BMidiThruUnit::ThruInput::ChannelPressure(uchar channel, uchar pressure,
	bigtime_t time)
{
	fUnit->HandleChannelPressure(channel, pressure, time);
}


void
BMidiThruUnit::ThruInput::PitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time)
{
	fUnit->HandlePitchBend(channel, lsb, msb, time);
}


void
BMidiThruUnit::ThruInput::SystemExclusive(void* data, size_t length,
	bigtime_t time)
{
	fUnit->HandleSystemExclusive(data, length, time);
}


void
BMidiThruUnit::ThruInput::SystemCommon(uchar status, uchar data1, uchar data2,
	bigtime_t time)
{
	fUnit->HandleSystemCommon(status, data1, data2, time);
}


void
BMidiThruUnit::ThruInput::SystemRealTime(uchar status, bigtime_t time)
{
	fUnit->HandleSystemRealTime(status, time);
}


BMidiThruUnit::BMidiThruUnit(const char* name, media_client_kinds kinds,
	int32 outputCount, const char* const* outputNames)
	:
	BMidiNativeUnit(name, kinds),
	fThruInput(NULL),
	fOutputCount(outputCount)
{
	memset(fOutputs, 0, sizeof(fOutputs));

	fThruInput = new(std::nothrow) ThruInput(this, "midi in");
	if (fThruInput == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}
	if (RegisterInput(fThruInput) != B_OK) {
		delete fThruInput;
		fThruInput = NULL;
		fInitStatus = B_NO_MEMORY;
		return;
	}

	if (fOutputCount > 2)
		fOutputCount = 2;
	for (int32 i = 0; i < fOutputCount; i++) {
		const char* portName
			= outputNames != NULL ? outputNames[i] : "midi out";
		fOutputs[i] = new(std::nothrow) BMidiOutput(portName);
		if (fOutputs[i] == NULL)
			continue;
		BMediaFormat format;
		format.format.type = B_MEDIA_MIDI;
		fOutputs[i]->SetAcceptedFormat(format);
		if (RegisterOutput(fOutputs[i]) != B_OK) {
			delete fOutputs[i];
			fOutputs[i] = NULL;
		}
	}
}


BMidiThruUnit::~BMidiThruUnit()
{
	Stop();
	if (fThruInput != NULL) {
		UnregisterInput(fThruInput);
		delete fThruInput;
	}
	for (int32 i = 0; i < 2; i++) {
		if (fOutputs[i] != NULL) {
			UnregisterOutput(fOutputs[i]);
			delete fOutputs[i];
		}
	}
}


BMidiOutput*
BMidiThruUnit::OutputAt(int32 index) const
{
	if (index < 0 || index >= fOutputCount)
		return NULL;
	return fOutputs[index];
}


void
BMidiThruUnit::CheckFlush(bigtime_t time)
{
	if (!FlushPending())
		return;
	ConsumeFlush();
	HandleFlush(time);
}


void
BMidiThruUnit::ProcessCycle(uint32 /*frameCount*/)
{
	// In graph mode this runs every quantum, which bounds how long a
	// changed control can leave tracked notes sounding.
	CheckFlush(CycleStartTime());
}


void
BMidiThruUnit::HandleFlush(bigtime_t /*time*/)
{
}


void
BMidiThruUnit::HandleNoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendNoteOff(channel, note, velocity, time);
}


void
BMidiThruUnit::HandleNoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendNoteOn(channel, note, velocity, time);
}


void
BMidiThruUnit::HandleKeyPressure(uchar channel, uchar note, uchar pressure,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendKeyPressure(channel, note, pressure, time);
}


void
BMidiThruUnit::HandleControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendControlChange(channel, controlNumber, controlValue, time);
}


void
BMidiThruUnit::HandleProgramChange(uchar channel, uchar programNumber,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendProgramChange(channel, programNumber, time);
}


void
BMidiThruUnit::HandleChannelPressure(uchar channel, uchar pressure,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendChannelPressure(channel, pressure, time);
}


void
BMidiThruUnit::HandlePitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendPitchBend(channel, lsb, msb, time);
}


void
BMidiThruUnit::HandleSystemExclusive(void* data, size_t length, bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendSystemExclusive(data, length, time);
}


void
BMidiThruUnit::HandleSystemCommon(uchar status, uchar data1, uchar data2,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendSystemCommon(status, data1, data2, time);
}


void
BMidiThruUnit::HandleSystemRealTime(uchar status, bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendSystemRealTime(status, time);
}


// #pragma mark - BTransposeUnit


BTransposeUnit::BTransposeUnit()
	:
	BMidiThruUnit("Transpose", B_MEDIA_FILTER)
{
	for (int32 channel = 0; channel < 16; channel++) {
		for (int32 note = 0; note < 128; note++)
			fSentNote[channel][note] = -1;
	}

	AddContinuousParam(0, "Shift", -48.0f, 48.0f, 0.0f);
	FinishParameters("Transpose");
}


void
BTransposeUnit::ParameterChanged(int32 id)
{
	if (id == 0)
		RequestFlush();
}


void
BTransposeUnit::HandleFlush(bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	for (int32 channel = 0; channel < 16; channel++) {
		for (int32 note = 0; note < 128; note++) {
			int16 sent = fSentNote[channel][note];
			if (sent < 0)
				continue;
			output->SendNoteOff((uchar)channel, (uchar)sent, 0, time);
			fSentNote[channel][note] = -1;
		}
	}
}


void
BTransposeUnit::HandleNoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	int32 shift = (int32)lroundf(FloatParam(0));
	int32 sent = _ClampInt((int32)note + shift, 0, 127);

	// Retriggering the same key under a new shift releases the old pitch
	// first, so nothing hangs.
	int16 previous = fSentNote[channel][note];
	if (previous >= 0 && previous != sent)
		output->SendNoteOff(channel, (uchar)previous, 0, time);

	fSentNote[channel][note] = (int16)sent;
	output->SendNoteOn(channel, (uchar)sent, velocity, time);
}


void
BTransposeUnit::HandleNoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	int16 sent = fSentNote[channel][note];
	fSentNote[channel][note] = -1;
	if (sent < 0) {
		sent = (int16)_ClampInt((int32)note
			+ (int32)lroundf(FloatParam(0)), 0, 127);
	}
	output->SendNoteOff(channel, (uchar)sent, velocity, time);
}


// #pragma mark - BChannelFilterUnit


static const char* const kOnOffItems[] = { "Off", "On" };


BChannelFilterUnit::BChannelFilterUnit()
	:
	BMidiThruUnit("Channel Filter", B_MEDIA_FILTER)
{
	memset(fSentChannel, -1, sizeof(fSentChannel));

	fPassMask.store(0xffff);

	for (int32 i = 0; i < 16; i++) {
		char name[32];
		snprintf(name, sizeof(name), "Ch %d Pass", i + 1);
		AddDiscreteParam(i, name, 1, kOnOffItems, 2, B_ENABLE);
	}
	AddContinuousParam(16, "Remap From", -1.0f, 15.0f, -1.0f);
	AddContinuousParam(17, "Remap To", 0.0f, 15.0f, 0.0f);
	FinishParameters("Channel Filter");
}


bool
BChannelFilterUnit::_IsPassed(uchar channel) const
{
	return (fPassMask.load() & (1u << (channel & 0x0f))) != 0;
}


uchar
BChannelFilterUnit::_MapChannel(uchar channel) const
{
	int32 from = (int32)lroundf(FloatParam(16));
	if (from >= 0 && (channel & 0x0f) == (uchar)from)
		return (uchar)_ClampInt((int32)lroundf(FloatParam(17)), 0, 15);
	return channel;
}


void
BChannelFilterUnit::_RebuildPassMask()
{
	uint32 mask = 0;
	for (int32 i = 0; i < 16; i++) {
		if (IntParam(i) != 0)
			mask |= 1u << i;
	}
	fPassMask.store(mask);
}


void
BChannelFilterUnit::ParameterChanged(int32 id)
{
	if (id < 16)
		_RebuildPassMask();
	RequestFlush();
}


void
BChannelFilterUnit::HandleFlush(bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	for (int32 channel = 0; channel < 16; channel++) {
		for (int32 note = 0; note < 128; note++) {
			int8 sent = fSentChannel[channel][note];
			if (sent < 0)
				continue;
			output->SendNoteOff((uchar)sent, (uchar)note, 0, time);
			fSentChannel[channel][note] = -1;
		}
	}
}


void
BChannelFilterUnit::HandleNoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	if (!_IsPassed(channel)) {
		int8 previous = fSentChannel[channel][note];
		if (previous >= 0) {
			output->SendNoteOff((uchar)previous, note, 0, time);
			fSentChannel[channel][note] = -1;
		}
		return;
	}

	uchar mapped = _MapChannel(channel);
	int8 previous = fSentChannel[channel][note];
	if (previous >= 0 && previous != (int8)mapped)
		output->SendNoteOff((uchar)previous, note, 0, time);

	fSentChannel[channel][note] = (int8)mapped;
	output->SendNoteOn(mapped, note, velocity, time);
}


void
BChannelFilterUnit::HandleNoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	// The note-off always follows the channel the note-on went out on,
	// even if the channel is blocked or remapped since.
	int8 sent = fSentChannel[channel][note];
	fSentChannel[channel][note] = -1;
	if (sent >= 0) {
		output->SendNoteOff((uchar)sent, note, velocity, time);
		return;
	}
	if (_IsPassed(channel))
		output->SendNoteOff(_MapChannel(channel), note, velocity, time);
}


void
BChannelFilterUnit::HandleKeyPressure(uchar channel, uchar note,
	uchar pressure, bigtime_t time)
{
	if (!_IsPassed(channel))
		return;
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendKeyPressure(_MapChannel(channel), note, pressure, time);
}


void
BChannelFilterUnit::HandleControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	if (!_IsPassed(channel))
		return;
	BMidiOutput* output = OutputAt(0);
	if (output != NULL) {
		output->SendControlChange(_MapChannel(channel), controlNumber,
			controlValue, time);
	}
}


void
BChannelFilterUnit::HandleProgramChange(uchar channel, uchar programNumber,
	bigtime_t time)
{
	if (!_IsPassed(channel))
		return;
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendProgramChange(_MapChannel(channel), programNumber, time);
}


void
BChannelFilterUnit::HandleChannelPressure(uchar channel, uchar pressure,
	bigtime_t time)
{
	if (!_IsPassed(channel))
		return;
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendChannelPressure(_MapChannel(channel), pressure, time);
}


void
BChannelFilterUnit::HandlePitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time)
{
	if (!_IsPassed(channel))
		return;
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendPitchBend(_MapChannel(channel), lsb, msb, time);
}


// #pragma mark - BKeySplitUnit


static const char* const kSplitOutputNames[] = { "out a", "out b" };


BKeySplitUnit::BKeySplitUnit()
	:
	BMidiThruUnit("Key Split", B_MEDIA_FILTER, 2, kSplitOutputNames)
{
	memset(fRoute, 0, sizeof(fRoute));

	AddContinuousParam(0, "Split Point", 0.0f, 127.0f, 60.0f);
	FinishParameters("Key Split");
}


void
BKeySplitUnit::ParameterChanged(int32 id)
{
	if (id == 0)
		RequestFlush();
}


uint8
BKeySplitUnit::_RouteFor(uchar note) const
{
	int32 split = (int32)lroundf(FloatParam(0));
	return note < split ? 1 : 2;
}


void
BKeySplitUnit::HandleFlush(bigtime_t time)
{
	for (int32 channel = 0; channel < 16; channel++) {
		for (int32 note = 0; note < 128; note++) {
			uint8 route = fRoute[channel][note];
			if (route == 0)
				continue;
			BMidiOutput* output = OutputAt(route - 1);
			if (output != NULL)
				output->SendNoteOff((uchar)channel, (uchar)note, 0, time);
			fRoute[channel][note] = 0;
		}
	}
}


void
BKeySplitUnit::HandleNoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	uint8 route = _RouteFor(note);
	BMidiOutput* output = OutputAt(route - 1);
	if (output == NULL)
		return;

	uint8 previous = fRoute[channel][note];
	if (previous != 0 && previous != route) {
		BMidiOutput* oldOutput = OutputAt(previous - 1);
		if (oldOutput != NULL)
			oldOutput->SendNoteOff(channel, note, 0, time);
	}

	fRoute[channel][note] = route;
	output->SendNoteOn(channel, note, velocity, time);
}


void
BKeySplitUnit::HandleNoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	uint8 route = fRoute[channel][note];
	fRoute[channel][note] = 0;
	if (route == 0)
		route = _RouteFor(note);
	BMidiOutput* output = OutputAt(route - 1);
	if (output != NULL)
		output->SendNoteOff(channel, note, velocity, time);
}


void
BKeySplitUnit::HandleKeyPressure(uchar channel, uchar note, uchar pressure,
	bigtime_t time)
{
	uint8 route = fRoute[channel][note];
	if (route == 0)
		route = _RouteFor(note);
	BMidiOutput* output = OutputAt(route - 1);
	if (output != NULL)
		output->SendKeyPressure(channel, note, pressure, time);
}


void
BKeySplitUnit::HandleControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	// Non-note events reach both halves.
	for (int32 i = 0; i < 2; i++) {
		BMidiOutput* output = OutputAt(i);
		if (output != NULL) {
			output->SendControlChange(channel, controlNumber, controlValue,
				time);
		}
	}
}


void
BKeySplitUnit::HandleProgramChange(uchar channel, uchar programNumber,
	bigtime_t time)
{
	for (int32 i = 0; i < 2; i++) {
		BMidiOutput* output = OutputAt(i);
		if (output != NULL)
			output->SendProgramChange(channel, programNumber, time);
	}
}


void
BKeySplitUnit::HandleChannelPressure(uchar channel, uchar pressure,
	bigtime_t time)
{
	for (int32 i = 0; i < 2; i++) {
		BMidiOutput* output = OutputAt(i);
		if (output != NULL)
			output->SendChannelPressure(channel, pressure, time);
	}
}


void
BKeySplitUnit::HandlePitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time)
{
	for (int32 i = 0; i < 2; i++) {
		BMidiOutput* output = OutputAt(i);
		if (output != NULL)
			output->SendPitchBend(channel, lsb, msb, time);
	}
}


// #pragma mark - BVelocityCurveUnit


static const char* const kCurveModes[] = {
	"Fixed", "Scaled", "Compressed", "Random"
};


BVelocityCurveUnit::BVelocityCurveUnit()
	:
	BMidiThruUnit("Velocity Curve", B_MEDIA_FILTER),
	fRandomState(0x9e3779b9u)
{
	AddDiscreteParam(0, "Mode", 1, kCurveModes, 4, NULL);
	AddContinuousParam(1, "Value", 0.0f, 127.0f, 64.0f);
	FinishParameters("Velocity Curve");
}


uchar
BVelocityCurveUnit::_Transform(uchar velocity)
{
	int32 mode = IntParam(0);
	int32 value = (int32)lroundf(FloatParam(1));
	int32 result = velocity;

	switch (mode) {
		case 0:
			result = value;
			break;
		case 1:
			result = (int32)lroundf(velocity * (value / 64.0f));
			break;
		case 2:
			result = 64 + (int32)lroundf((velocity - 64) * (value / 127.0f));
			break;
		case 3:
			fRandomState ^= fRandomState << 13;
			fRandomState ^= fRandomState >> 17;
			fRandomState ^= fRandomState << 5;
			result = 1 + (int32)(fRandomState % 127);
			break;
		default:
			break;
	}

	// Zero would turn the note-on into a note-off.
	return (uchar)_ClampInt(result, 1, 127);
}


void
BVelocityCurveUnit::HandleNoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendNoteOn(channel, note, _Transform(velocity), time);
}


// #pragma mark - BChordUnit


static const int32 kChordIntervals[][8] = {
	{ 0, 4, 7, -1, -1, -1, -1, -1 },
	{ 0, 3, 7, -1, -1, -1, -1, -1 },
	{ 0, 3, 6, -1, -1, -1, -1, -1 },
	{ 0, 4, 8, -1, -1, -1, -1, -1 },
	{ 0, 5, 7, -1, -1, -1, -1, -1 },
	{ 0, 4, 7, 10, -1, -1, -1, -1 },
	{ 0, 4, 7, 11, -1, -1, -1, -1 },
	{ 0, 3, 7, 10, -1, -1, -1, -1 },
	{ 0, 12, -1, -1, -1, -1, -1, -1 },
	{ 0, 7, -1, -1, -1, -1, -1, -1 },
};

static const char* const kChordTypes[] = {
	"Major", "Minor", "Diminished", "Augmented", "Suspended 4",
	"Dominant 7", "Major 7", "Minor 7", "Octave", "Fifth"
};


BChordUnit::BChordUnit()
	:
	BMidiThruUnit("Chord", B_MEDIA_FILTER)
{
	memset(fSentCount, 0, sizeof(fSentCount));
	memset(fSentNote, -1, sizeof(fSentNote));

	AddDiscreteParam(0, "Chord", 0, kChordTypes, 10, NULL);
	FinishParameters("Chord");
}


void
BChordUnit::HandleStop(bigtime_t performanceTime)
{
	HandleFlush(performanceTime);
	BMidiUnit::HandleStop(performanceTime);
}


void
BChordUnit::ParameterChanged(int32 id)
{
	if (id == 0)
		RequestFlush();
}


int32
BChordUnit::_IntervalCount() const
{
	int32 type = _ClampInt(IntParam(0), 0, 9);
	int32 count = 0;
	while (count < 8 && kChordIntervals[type][count] >= 0)
		count++;
	return count;
}


int32
BChordUnit::_IntervalAt(int32 index) const
{
	int32 type = _ClampInt(IntParam(0), 0, 9);
	return kChordIntervals[type][index];
}


void
BChordUnit::HandleFlush(bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	for (int32 channel = 0; channel < 16; channel++) {
		for (int32 note = 0; note < 128; note++) {
			uint8 count = fSentCount[channel][note];
			for (uint8 i = 0; i < count; i++) {
				int8 sent = fSentNote[channel][note][i];
				if (sent >= 0)
					output->SendNoteOff((uchar)channel, (uchar)sent, 0, time);
				fSentNote[channel][note][i] = -1;
			}
			fSentCount[channel][note] = 0;
		}
	}
}


void
BChordUnit::HandleNoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	int32 count = _IntervalCount();
	uint8 sent = 0;
	for (int32 i = 0; i < count && sent < 8; i++) {
		int32 pitch = _ClampInt((int32)note + _IntervalAt(i), 0, 127);
		fSentNote[channel][note][sent] = (int8)pitch;
		sent++;
		output->SendNoteOn(channel, (uchar)pitch, velocity, time);
	}
	fSentCount[channel][note] = sent;
}


void
BChordUnit::HandleNoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	uint8 count = fSentCount[channel][note];
	fSentCount[channel][note] = 0;
	for (uint8 i = 0; i < count; i++) {
		int8 sent = fSentNote[channel][note][i];
		fSentNote[channel][note][i] = -1;
		if (sent >= 0)
			output->SendNoteOff(channel, (uchar)sent, velocity, time);
	}
}


// #pragma mark - BCCMapperUnit


BCCMapperUnit::BCCMapperUnit()
	:
	BMidiThruUnit("CC Mapper", B_MEDIA_FILTER)
{
	AddDiscreteParam(0, "Enabled", 1, kOnOffItems, 2, B_ENABLE);
	AddContinuousParam(1, "From CC", 0.0f, 127.0f, 1.0f);
	AddContinuousParam(2, "To CC", 0.0f, 127.0f, 1.0f);
	AddContinuousParam(3, "Input Min", 0.0f, 127.0f, 0.0f);
	AddContinuousParam(4, "Input Max", 0.0f, 127.0f, 127.0f);
	AddContinuousParam(5, "Output Min", 0.0f, 127.0f, 0.0f);
	AddContinuousParam(6, "Output Max", 0.0f, 127.0f, 127.0f);
	FinishParameters("CC Mapper");
}


void
BCCMapperUnit::HandleControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	int32 from = (int32)lroundf(FloatParam(1));
	if (IntParam(0) == 0 || controlNumber != from) {
		output->SendControlChange(channel, controlNumber, controlValue, time);
		return;
	}

	int32 inMin = (int32)lroundf(FloatParam(3));
	int32 inMax = (int32)lroundf(FloatParam(4));
	int32 outMin = (int32)lroundf(FloatParam(5));
	int32 outMax = (int32)lroundf(FloatParam(6));

	int32 value = controlValue;
	if (inMax != inMin) {
		float scaled = (value - inMin) / (float)(inMax - inMin);
		value = outMin + (int32)lroundf(scaled * (outMax - outMin));
	} else
		value = outMin;

	output->SendControlChange(channel, (uchar)_ClampInt(
		(int32)lroundf(FloatParam(2)), 0, 127),
		(uchar)_ClampInt(value, 0, 127), time);
}


// #pragma mark - BClockUnit


static const char* const kClockModes[] = {
	"MIDI Clock", "Clock + Start/Stop", "Note Ticks", "Clock + Ticks"
};


BClockUnit::BClockUnit()
	:
	BMidiNativeUnit("Clock", B_MEDIA_PLAYER),
	fOutput(NULL),
	fLastCycle(0),
	fPhase(0),
	fNoteOffDue(0),
	fTickedNote(false)
{
	fOutput = new(std::nothrow) BMidiOutput("midi out");
	if (fOutput != NULL) {
		BMediaFormat format;
		format.format.type = B_MEDIA_MIDI;
		fOutput->SetAcceptedFormat(format);
		if (RegisterOutput(fOutput) != B_OK) {
			delete fOutput;
			fOutput = NULL;
		}
	}
	if (fOutput == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	AddContinuousParam(0, "Tempo", 20.0f, 300.0f, 120.0f);
	AddDiscreteParam(1, "Mode", 0, kClockModes, 4, NULL);
	AddContinuousParam(2, "Channel", 0.0f, 15.0f, 0.0f);
	AddContinuousParam(3, "Tick Note", 0.0f, 127.0f, 37.0f);
	AddContinuousParam(4, "Tick Velocity", 1.0f, 127.0f, 100.0f);
	FinishParameters("Clock");
}


BClockUnit::~BClockUnit()
{
	Stop();
	if (fOutput != NULL) {
		UnregisterOutput(fOutput);
		delete fOutput;
	}
}


void
BClockUnit::HandleStart(bigtime_t performanceTime)
{
	fLastCycle = 0;
	fPhase = 0;
	fTickedNote = false;
	fNoteOffDue = 0;

	int32 mode = IntParam(1);
	if (mode == 1 && fOutput != NULL)
		fOutput->SendSystemRealTime(0xfa, performanceTime);
}


void
BClockUnit::HandleStop(bigtime_t performanceTime)
{
	int32 mode = IntParam(1);
	if (mode == 1 && fOutput != NULL)
		fOutput->SendSystemRealTime(0xfc, performanceTime);
	if (fTickedNote && fOutput != NULL) {
		fOutput->SendNoteOff((uchar)_ClampInt(IntParam(2), 0, 15),
			(uchar)_ClampInt(IntParam(3), 0, 127), 0, performanceTime);
		fTickedNote = false;
	}
}


void
BClockUnit::_SendTick(bigtime_t time, bool quarter)
{
	if (fOutput == NULL)
		return;

	int32 mode = IntParam(1);
	bool clock = mode == 0 || mode == 1 || mode == 3;
	bool ticks = mode == 2 || mode == 3;

	if (clock)
		fOutput->SendSystemRealTime(0xf8, time);

	if (quarter && ticks) {
		uchar channel = (uchar)_ClampInt(IntParam(2), 0, 15);
		uchar note = (uchar)_ClampInt(IntParam(3), 0, 127);
		uchar velocity = (uchar)_ClampInt(IntParam(4), 1, 127);
		if (fTickedNote)
			fOutput->SendNoteOff(channel, note, 0, time);
		fOutput->SendNoteOn(channel, note, velocity, time);
		fTickedNote = true;
		fNoteOffDue = time + 8000;
	}
}


void
BClockUnit::ProcessCycle(uint32 frameCount)
{
	bigtime_t now = CycleStartTime();
	uint32 rate = SampleRate();
	if (rate == 0)
		rate = 48000;
	if (frameCount == 0)
		frameCount = Quantum();

	if (fTickedNote && fNoteOffDue > 0 && now >= fNoteOffDue
			&& fOutput != NULL) {
		fOutput->SendNoteOff((uchar)_ClampInt(IntParam(2), 0, 15),
			(uchar)_ClampInt(IntParam(3), 0, 127), 0, now);
		fTickedNote = false;
	}

	if (fLastCycle == 0) {
		fLastCycle = now;
		return;
	}

	bigtime_t delta = now - fLastCycle;
	if (delta < 0)
		delta = 0;
	// A long stall must not burst a hundred pulses into the ring.
	if (delta > 100000)
		delta = 100000;
	fLastCycle = now;

	float tempo = FloatParam(0);
	if (tempo < 20.0f)
		tempo = 20.0f;
	bigtime_t pulse = (bigtime_t)(60000000.0 / (tempo * 24.0));
	if (pulse < 1000)
		pulse = 1000;

	fPhase += delta;
	int32 pulses = 0;
	while (fPhase >= pulse && pulses < 64) {
		fPhase -= pulse;
		pulses++;
		_SendTick(now, pulses % 24 == 0);
	}
}


// #pragma mark - BArpeggiatorUnit


static const char* const kArpPatterns[] = { "Up", "Down", "Up-Down", "Random" };
static const char* const kArpRates[] = {
	"1/4", "1/8", "1/16", "1/8 Triplet", "1/16 Triplet"
};


BArpeggiatorUnit::BArpeggiatorUnit()
	:
	BMidiThruUnit("Arpeggiator", B_MEDIA_FILTER),
	fHeldCount(0),
	fStepIndex(0),
	fRandomState(0x2545f491u),
	fLastCycle(0),
	fPhase(0),
	fSounding(false),
	fSoundingNote(0),
	fSoundingChannel(0)
{
	memset(fHeld, 0, sizeof(fHeld));
	memset(fHeldNote, 0, sizeof(fHeldNote));
	memset(fHeldChannel, 0, sizeof(fHeldChannel));

	AddDiscreteParam(0, "Pattern", 0, kArpPatterns, 4, NULL);
	AddDiscreteParam(1, "Rate", 2, kArpRates, 5, NULL);
	AddContinuousParam(2, "Tempo", 20.0f, 300.0f, 120.0f);
	AddContinuousParam(3, "Octaves", 1.0f, 4.0f, 1.0f);
	AddContinuousParam(4, "Velocity", 1.0f, 127.0f, 100.0f);
	FinishParameters("Arpeggiator");
}


BArpeggiatorUnit::~BArpeggiatorUnit()
{
	Stop();
}


void
BArpeggiatorUnit::HandleStop(bigtime_t performanceTime)
{
	HandleFlush(performanceTime);
	fHeldCount = 0;
	memset(fHeld, 0, sizeof(fHeld));
	BMidiUnit::HandleStop(performanceTime);
}


void
BArpeggiatorUnit::ParameterChanged(int32 id)
{
	if (id == 0 || id == 1 || id == 3)
		RequestFlush();
}


void
BArpeggiatorUnit::HandleFlush(bigtime_t time)
{
	_ReleasePattern(time);
}


void
BArpeggiatorUnit::_ReleasePattern(bigtime_t time)
{
	if (!fSounding)
		return;
	BMidiOutput* output = OutputAt(0);
	if (output != NULL)
		output->SendNoteOff(fSoundingChannel, fSoundingNote, 0, time);
	fSounding = false;
}


void
BArpeggiatorUnit::HandleNoteOn(uchar channel, uchar note, uchar /*velocity*/,
	bigtime_t /*time*/)
{
	if (fHeld[note] != 0 || fHeldCount >= 256)
		return;
	fHeld[note] = 1;
	fHeldNote[fHeldCount] = note;
	fHeldChannel[fHeldCount] = channel;
	fHeldCount++;
}


void
BArpeggiatorUnit::HandleNoteOff(uchar /*channel*/, uchar note,
	uchar /*velocity*/, bigtime_t time)
{
	if (fHeld[note] == 0)
		return;
	fHeld[note] = 0;
	for (int32 i = 0; i < fHeldCount; i++) {
		if (fHeldNote[i] != note)
			continue;
		for (int32 j = i; j < fHeldCount - 1; j++) {
			fHeldNote[j] = fHeldNote[j + 1];
			fHeldChannel[j] = fHeldChannel[j + 1];
		}
		fHeldCount--;
		break;
	}
	if (fHeldCount == 0)
		_ReleasePattern(time);
}


void
BArpeggiatorUnit::_AdvanceStep(bigtime_t time)
{
	_ReleasePattern(time);

	if (fHeldCount == 0)
		return;

	int32 pattern = _ClampInt(IntParam(0), 0, 3);
	int32 count = fHeldCount;
	int32 index = 0;

	switch (pattern) {
		case 0:
			index = fStepIndex % count;
			break;
		case 1:
			index = count - 1 - (fStepIndex % count);
			break;
		case 2:
		{
			int32 span = count * 2 - 2;
			if (span <= 0)
				index = 0;
			else {
				int32 pos = fStepIndex % span;
				index = pos < count ? pos : span - pos;
			}
			break;
		}
		case 3:
			fRandomState ^= fRandomState << 13;
			fRandomState ^= fRandomState >> 17;
			fRandomState ^= fRandomState << 5;
			index = (int32)(fRandomState % (uint32)count);
			break;
		default:
			break;
	}

	int32 octaves = _ClampInt((int32)lroundf(FloatParam(3)), 1, 4);
	int32 octave = (fStepIndex / count) % octaves;
	int32 note = fHeldNote[index] + 12 * octave;

	BMidiOutput* output = OutputAt(0);
	if (output == NULL)
		return;

	uchar channel = fHeldChannel[index];
	uchar pitch = (uchar)_ClampInt(note, 0, 127);
	uchar velocity = (uchar)_ClampInt((int32)lroundf(FloatParam(4)), 1, 127);
	output->SendNoteOn(channel, pitch, velocity, time);
	fSounding = true;
	fSoundingNote = pitch;
	fSoundingChannel = channel;
	fStepIndex++;
}


void
BArpeggiatorUnit::ProcessCycle(uint32 /*frameCount*/)
{
	CheckFlush(CycleStartTime());

	bigtime_t now = CycleStartTime();
	if (fLastCycle == 0) {
		fLastCycle = now;
		return;
	}

	bigtime_t delta = now - fLastCycle;
	if (delta < 0)
		delta = 0;
	if (delta > 100000)
		delta = 100000;
	fLastCycle = now;

	if (fHeldCount == 0) {
		fPhase = 0;
		return;
	}

	static const float kDivisors[] = { 1.0f, 2.0f, 4.0f, 1.5f, 3.0f };
	int32 rateIndex = _ClampInt(IntParam(1), 0, 4);
	float tempo = FloatParam(2);
	if (tempo < 20.0f)
		tempo = 20.0f;
	bigtime_t step = (bigtime_t)(60000000.0 / (tempo * kDivisors[rateIndex]));
	if (step < 1000)
		step = 1000;

	fPhase += delta;
	int32 steps = 0;
	while (fPhase >= step && steps < 16) {
		fPhase -= step;
		steps++;
		_AdvanceStep(now);
	}
}


// #pragma mark - BMidiFilePlayerUnit


BMidiFilePlayerUnit::BMidiFilePlayerUnit()
	:
	BMidiNativeUnit("MIDI File Player", B_MEDIA_PLAYER),
	fOutput(NULL),
	fEvents(NULL),
	fEventCount(0),
	fIndex(0),
	fBaseTime(0),
	fPlaying(false)
{
	fOutput = new(std::nothrow) BMidiOutput("midi out");
	if (fOutput != NULL) {
		BMediaFormat format;
		format.format.type = B_MEDIA_MIDI;
		fOutput->SetAcceptedFormat(format);
		if (RegisterOutput(fOutput) != B_OK) {
			delete fOutput;
			fOutput = NULL;
		}
	}
	if (fOutput == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	AddDiscreteParam(0, "Loop", 0, kOnOffItems, 2, B_ENABLE);
	AddTextParam(1, "File", 512);
	FinishParameters("MIDI File Player");
}


BMidiFilePlayerUnit::~BMidiFilePlayerUnit()
{
	Stop();
	if (fOutput != NULL) {
		UnregisterOutput(fOutput);
		delete fOutput;
	}
	delete[] fEvents;
}


namespace {


struct MidiTickEvent {
	uint32		tick;
	uint8		status;
	uint8		data1;
	uint8		data2;
	uint8		length;
	uint32		tempo;
};


struct MidiOutEvent {
	bigtime_t	time;
	uint8		data[3];
	uint8		length;
};


static bool
_TickEventLess(const MidiTickEvent& a, const MidiTickEvent& b)
{
	return a.tick < b.tick;
}


} // anonymous namespace


status_t
BMidiFilePlayerUnit::_LoadFile(const char* path)
{
	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;

	BFile file(path, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	off_t size = 0;
	if (file.GetSize(&size) != B_OK || size < 14 || size > 64 * 1024 * 1024)
		return B_BAD_VALUE;

	uint8* bytes = new(std::nothrow) uint8[size];
	if (bytes == NULL)
		return B_NO_MEMORY;
	if (file.ReadAt(0, bytes, size) != size) {
		delete[] bytes;
		return B_ERROR;
	}

	if (memcmp(bytes, "MThd", 4) != 0) {
		delete[] bytes;
		return B_BAD_VALUE;
	}

	uint32 headerLength = ((uint32)bytes[4] << 24) | ((uint32)bytes[5] << 16)
		| ((uint32)bytes[6] << 8) | bytes[7];
	uint16 format = ((uint16)bytes[8] << 8) | bytes[9];
	uint16 trackCount = ((uint16)bytes[10] << 8) | bytes[11];
	uint16 division = ((uint16)bytes[12] << 8) | bytes[13];
	if (format > 1 || (division & 0x8000) != 0 || division == 0) {
		delete[] bytes;
		return B_UNSUPPORTED;
	}

	std::vector<MidiTickEvent> tickEvents;
	size_t offset = 8 + headerLength;

	for (uint16 track = 0; track < trackCount && offset + 8 <= (size_t)size;
			track++) {
		if (memcmp(bytes + offset, "MTrk", 4) != 0)
			break;
		uint32 trackLength = ((uint32)bytes[offset + 4] << 24)
			| ((uint32)bytes[offset + 5] << 16)
			| ((uint32)bytes[offset + 6] << 8) | bytes[offset + 7];
		size_t pos = offset + 8;
		size_t end = pos + trackLength;
		if (end > (size_t)size)
			end = size;

		uint32 tick = 0;
		uint8 running = 0;
		bool done = false;
		while (pos < end && !done) {
			// Variable-length delta time.
			uint32 delta = 0;
			for (int i = 0; i < 4 && pos < end; i++) {
				uint8 byte = bytes[pos++];
				delta = (delta << 7) | (byte & 0x7f);
				if ((byte & 0x80) == 0)
					break;
			}
			tick += delta;
			if (pos >= end)
				break;

			uint8 status = bytes[pos];
			if ((status & 0x80) != 0)
				pos++;
			else
				status = running;
			if (status < 0x80)
				break;

			if (status == 0xff) {
				// Meta event.
				if (pos >= end)
					break;
				uint8 metaType = bytes[pos++];
				uint32 length = 0;
				for (int i = 0; i < 4 && pos < end; i++) {
					uint8 byte = bytes[pos++];
					length = (length << 7) | (byte & 0x7f);
					if ((byte & 0x80) == 0)
						break;
				}
				if (metaType == 0x51 && length == 3 && pos + 3 <= end) {
					MidiTickEvent event;
					event.tick = tick;
					event.status = 0;
					event.data1 = 0;
					event.data2 = 0;
					event.length = 0;
					event.tempo = ((uint32)bytes[pos] << 16)
						| ((uint32)bytes[pos + 1] << 8) | bytes[pos + 2];
					tickEvents.push_back(event);
				}
				if (metaType == 0x2f)
					done = true;
				pos += length;
				running = 0;
				continue;
			}
			if (status == 0xf0 || status == 0xf7) {
				uint32 length = 0;
				for (int i = 0; i < 4 && pos < end; i++) {
					uint8 byte = bytes[pos++];
					length = (length << 7) | (byte & 0x7f);
					if ((byte & 0x80) == 0)
						break;
				}
				pos += length;
				running = 0;
				continue;
			}
			if (status >= 0xf8) {
				running = 0;
				continue;
			}

			uint8 kind = status & 0xf0;
			int32 dataBytes = 2;
			if (kind == 0xc0 || kind == 0xd0)
				dataBytes = 1;
			if (pos + dataBytes > end)
				break;
			running = status;

			MidiTickEvent event;
			event.tick = tick;
			event.status = status;
			event.data1 = bytes[pos];
			event.data2 = dataBytes > 1 ? bytes[pos + 1] : 0;
			event.length = (uint8)(1 + dataBytes);
			event.tempo = 0;
			tickEvents.push_back(event);
			pos += dataBytes;
		}
		offset = end;
	}

	delete[] bytes;

	// Merge the tracks by tick, then convert to microseconds with the
	// tempo map. A stable sort keeps same-tick order intact.
	std::stable_sort(tickEvents.begin(), tickEvents.end(), _TickEventLess);

	std::vector<MidiOutEvent> outputEvents;

	uint32 currentTempo = 500000;
	uint32 lastTick = 0;
	bigtime_t now = 0;
	for (size_t i = 0; i < tickEvents.size(); i++) {
		const MidiTickEvent& event = tickEvents[i];
		if (event.tick > lastTick) {
			now += (bigtime_t)((uint64)(event.tick - lastTick)
				* currentTempo / division);
			lastTick = event.tick;
		}
		if (event.status == 0) {
			if (event.tempo != 0)
				currentTempo = event.tempo;
			continue;
		}
		MidiOutEvent out;
		out.time = now;
		out.data[0] = event.status;
		out.data[1] = event.data1;
		out.data[2] = event.data2;
		out.length = event.length;
		outputEvents.push_back(out);
	}

	if (outputEvents.size() > 500000)
		outputEvents.resize(500000);

	delete[] fEvents;
	fEvents = NULL;
	fEventCount = 0;

	if (!outputEvents.empty()) {
		fEvents = new(std::nothrow) Event[outputEvents.size()];
		if (fEvents == NULL)
			return B_NO_MEMORY;
		for (size_t i = 0; i < outputEvents.size(); i++) {
			fEvents[i].time = outputEvents[i].time;
			memcpy(fEvents[i].data, outputEvents[i].data, 3);
			fEvents[i].length = outputEvents[i].length;
		}
		fEventCount = (int32)outputEvents.size();
	}
	return B_OK;
}


void
BMidiFilePlayerUnit::ParameterChanged(int32 id)
{
	if (id != 1)
		return;

	_LoadFile(TextParam(1));
	if (IsStarted())
		_Rewind(system_time());
}


void
BMidiFilePlayerUnit::_Rewind(bigtime_t performanceTime)
{
	fIndex = 0;
	fBaseTime = performanceTime;
	fPlaying = fEventCount > 0;
}


void
BMidiFilePlayerUnit::HandleStart(bigtime_t performanceTime)
{
	_Rewind(performanceTime);
}


void
BMidiFilePlayerUnit::HandleStop(bigtime_t performanceTime)
{
	fPlaying = false;
	BMidiUnit::HandleStop(performanceTime);
}


void
BMidiFilePlayerUnit::HandleSeek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	fBaseTime = performanceTime - mediaTime;
	fIndex = 0;
	while (fIndex < fEventCount && fEvents[fIndex].time < mediaTime)
		fIndex++;
	fPlaying = fEventCount > 0 && fIndex < fEventCount;
}


void
BMidiFilePlayerUnit::ProcessCycle(uint32 /*frameCount*/)
{
	if (!fPlaying || fOutput == NULL || fEvents == NULL)
		return;

	bigtime_t now = CycleStartTime();
	if (now == 0)
		now = system_time();
	bigtime_t elapsed = now - fBaseTime;

	while (fIndex < fEventCount && fEvents[fIndex].time <= elapsed) {
		const Event& event = fEvents[fIndex];
		bigtime_t time = fBaseTime + event.time;
		switch (event.data[0] & 0xf0) {
			case kNoteOnStatus:
				fOutput->SendNoteOn(event.data[0] & 0x0f, event.data[1],
					event.data[2], time);
				break;
			case kNoteOffStatus:
				fOutput->SendNoteOff(event.data[0] & 0x0f, event.data[1],
					event.data[2], time);
				break;
			case 0xb0:
				fOutput->SendControlChange(event.data[0] & 0x0f,
					event.data[1], event.data[2], time);
				break;
			case 0xc0:
				fOutput->SendProgramChange(event.data[0] & 0x0f,
					event.data[1], time);
				break;
			case 0xd0:
				fOutput->SendChannelPressure(event.data[0] & 0x0f,
					event.data[1], time);
				break;
			case 0xe0:
				fOutput->SendPitchBend(event.data[0] & 0x0f, event.data[1],
					event.data[2], time);
				break;
			case 0xa0:
				fOutput->SendKeyPressure(event.data[0] & 0x0f, event.data[1],
					event.data[2], time);
				break;
			default:
				break;
		}
		fIndex++;
	}

	if (fIndex >= fEventCount) {
		if (IntParam(0) != 0)
			_Rewind(now);
		else {
			fPlaying = false;
			fOutput->AllNotesOff(now);
		}
	}
}


// #pragma mark - BMidiOutUnit


BMidiOutUnit::BMidiOutUnit()
	:
	BMidiNativeUnit("MIDI Out", B_MEDIA_PLAYER)
{
	BMidiOutput* output = new(std::nothrow) BMidiOutput("midi out");
	if (output != NULL) {
		BMediaFormat format;
		format.format.type = B_MEDIA_MIDI;
		output->SetAcceptedFormat(format);
		if (RegisterOutput(output) != B_OK)
			delete output;
	}
}
