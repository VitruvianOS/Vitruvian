/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MidiFilterExt.h>

#include <new>
#include <string.h>

#include <DataIO.h>


class BMidiChannelFilter::FilterInput : public BMidiInput {
public:
								FilterInput(BMidiChannelFilter* owner)
									:
									BMediaConnection(B_MEDIA_INPUT,
										"filter in"),
									BMidiInput("filter in"),
									fOwner(owner)
								{
								}

	virtual						~FilterInput()
								{
								}

	virtual	void				NoteOff(uchar channel, uchar note,
										uchar velocity, bigtime_t time)
								{
									fOwner->_NoteOff(channel, note, velocity,
										time);
								}

	virtual	void				NoteOn(uchar channel, uchar note,
										uchar velocity, bigtime_t time)
								{
									fOwner->_NoteOn(channel, note, velocity,
										time);
								}

	virtual	void				KeyPressure(uchar channel, uchar note,
										uchar pressure, bigtime_t time)
								{
									fOwner->_KeyPressure(channel, note,
										pressure, time);
								}

	virtual	void				ControlChange(uchar channel,
										uchar controlNumber,
										uchar controlValue, bigtime_t time)
								{
									fOwner->_ControlChange(channel,
										controlNumber, controlValue, time);
								}

	virtual	void				ProgramChange(uchar channel,
										uchar programNumber, bigtime_t time)
								{
									fOwner->_ProgramChange(channel,
										programNumber, time);
								}

	virtual	void				ChannelPressure(uchar channel,
										uchar pressure, bigtime_t time)
								{
									fOwner->_ChannelPressure(channel, pressure,
										time);
								}

	virtual	void				PitchBend(uchar channel, uchar lsb,
										uchar msb, bigtime_t time)
								{
									fOwner->_PitchBend(channel, lsb, msb,
										time);
								}

	virtual	void				SystemExclusive(void* data, size_t length,
										bigtime_t time)
								{
									fOwner->_SystemExclusive(data, length,
										time);
								}

private:
			BMidiChannelFilter*	fOwner;
};


BMidiChannelFilter::BMidiChannelFilter(const char* name, uint32 channelMask)
	:
	BMidiUnit(name != NULL ? name : "midi channel filter",
		B_MEDIA_FILTER),
	fInput(NULL),
	fOutput(NULL),
	fChannelMask(channelMask),
	fRemapFrom(-1),
	fRemapTo(-1)
{
	fInput = new(std::nothrow) FilterInput(this);
	fOutput = new(std::nothrow) BMidiOutput("filter out");
	if (fInput == NULL || fOutput == NULL)
		return;

	fInput->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_MIDI));
	fOutput->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_MIDI));

	RegisterInput(fInput);
	RegisterOutput(fOutput);
}


BMidiChannelFilter::~BMidiChannelFilter()
{
	Stop();
	delete fInput;
	delete fOutput;
	fInput = NULL;
	fOutput = NULL;
}


void
BMidiChannelFilter::SetChannelMask(uint32 mask)
{
	fChannelMask.store(mask);
}


uint32
BMidiChannelFilter::ChannelMask() const
{
	return fChannelMask.load();
}


void
BMidiChannelFilter::SetChannelRemap(int32 from, int32 to)
{
	fRemapFrom.store(from);
	fRemapTo.store(to);
}


void
BMidiChannelFilter::ChannelRemap(int32* outFrom, int32* outTo) const
{
	if (outFrom != NULL)
		*outFrom = fRemapFrom.load();
	if (outTo != NULL)
		*outTo = fRemapTo.load();
}


bool
BMidiChannelFilter::_Accept(uchar channel) const
{
	uint32 mask = fChannelMask.load();
	if (mask == 0)
		return true;
	if (channel >= 32)
		return false;
	return (mask & ((uint32)1 << channel)) != 0;
}


uchar
BMidiChannelFilter::_RemapChannel(uchar channel) const
{
	int32 from = fRemapFrom.load();
	int32 to = fRemapTo.load();
	if (from < 0 || to < 0 || (int32)channel != from)
		return channel;
	if (to > 15)
		return channel;
	return (uchar)to;
}


void
BMidiChannelFilter::_NoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	if (fOutput == NULL || !_Accept(channel))
		return;
	fOutput->SendNoteOff(_RemapChannel(channel), note, velocity, time);
}


void
BMidiChannelFilter::_NoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	if (fOutput == NULL || !_Accept(channel))
		return;
	fOutput->SendNoteOn(_RemapChannel(channel), note, velocity, time);
}


void
BMidiChannelFilter::_KeyPressure(uchar channel, uchar note, uchar pressure,
	bigtime_t time)
{
	if (fOutput == NULL || !_Accept(channel))
		return;
	fOutput->SendKeyPressure(_RemapChannel(channel), note, pressure, time);
}


void
BMidiChannelFilter::_ControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	if (fOutput == NULL || !_Accept(channel))
		return;
	fOutput->SendControlChange(_RemapChannel(channel), controlNumber,
		controlValue, time);
}


void
BMidiChannelFilter::_ProgramChange(uchar channel, uchar programNumber,
	bigtime_t time)
{
	if (fOutput == NULL || !_Accept(channel))
		return;
	fOutput->SendProgramChange(_RemapChannel(channel), programNumber, time);
}


void
BMidiChannelFilter::_ChannelPressure(uchar channel, uchar pressure,
	bigtime_t time)
{
	if (fOutput == NULL || !_Accept(channel))
		return;
	fOutput->SendChannelPressure(_RemapChannel(channel), pressure, time);
}


void
BMidiChannelFilter::_PitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time)
{
	if (fOutput == NULL || !_Accept(channel))
		return;
	fOutput->SendPitchBend(_RemapChannel(channel), lsb, msb, time);
}


void
BMidiChannelFilter::_SystemExclusive(void* data, size_t length, bigtime_t time)
{
	if (fOutput == NULL || data == NULL)
		return;
	fOutput->SendSystemExclusive(data, length, time);
}


BMidiSysExCollector::BMidiSysExCollector(const char* name)
	:
	BMediaConnection(B_MEDIA_INPUT,
		name != NULL ? name : "sysex collector"),
	BMidiInput(name != NULL ? name : "sysex collector"),
	fUsed(0),
	fDropped(0)
{
	SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_MIDI));
}


BMidiSysExCollector::~BMidiSysExCollector()
{
}


void
BMidiSysExCollector::SystemExclusive(void* data, size_t length,
	bigtime_t /*time*/)
{
	if (data == NULL || length == 0)
		return;

	// Called on the PipeWire data thread: no lock, no allocate. Fixed
	// buffer with a drop counter (handoff 12.L L7).
	size_t used = fUsed.load(std::memory_order_relaxed);
	size_t space = kCapacity - used;
	if (length > space) {
		fDropped.fetch_add(length, std::memory_order_relaxed);
		return;
	}

	memcpy(fBuffer + used, data, length);
	fUsed.store(used + length, std::memory_order_release);
}


status_t
BMidiSysExCollector::Dump(BMemoryIO* out) const
{
	if (out == NULL)
		return B_BAD_VALUE;

	size_t used = fUsed.load(std::memory_order_acquire);
	if (used == 0)
		return B_OK;

	ssize_t written = out->Write(fBuffer, used);
	if (written < 0 || (size_t)written != used)
		return B_ERROR;

	return B_OK;
}


void
BMidiSysExCollector::Reset()
{
	fUsed.store(0, std::memory_order_release);
	fDropped.store(0, std::memory_order_relaxed);
}


uint32
BMidiSysExCollector::DroppedBytes() const
{
	return (uint32)fDropped.load(std::memory_order_relaxed);
}
