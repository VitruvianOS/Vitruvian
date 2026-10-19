/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/MediaSynth.h>

#include <new>
#include <stdio.h>
#include <string.h>

#include <media2/MediaConnection.h>
#include <media2/MediaFormat.h>
#include <media2/MediaUnit.h>


// Collects midi2 events on the RT thread into a fixed stack so ProcessCycle
// can walk them in offset order without allocating.
class BMediaSynth::MidiHook : public BMidiInput {
public:
								MidiHook();

			void				Clear() { fCount = 0; }

			struct Event {
				enum kind {
					kNoteOn,
					kNoteOff,
					kKeyPressure,
					kControlChange,
					kProgramChange,
					kChannelPressure,
					kPitchBend,
					kSystemExclusive
				};

				kind			what;
				uchar			channel;
				uchar			data1;
				uchar			data2;
				bigtime_t		time;
				const void*		data;
				size_t			length;
			};

			Event*				Events() { return fEvents; }
			int32				Count() const { return fCount; }

protected:
	virtual	void				NoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				NoteOff(uchar channel, uchar note,
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

private:
			void				_Push(const Event& event);

			Event				fEvents[128];
			int32				fCount;
};


class BMediaSynth::AudioOutput : public BMediaOutput {
public:
								AudioOutput(const char* name);
	virtual						~AudioOutput();

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format) override;
	virtual	status_t			FormatProposal(BMediaFormat* format) override;
};


BMediaSynth::MidiHook::MidiHook()
	:
	// Virtual base must be named by the most-derived class.
	BMediaConnection(B_MEDIA_INPUT, "midi"),
	BMidiInput("midi"),
	fCount(0)
{
	memset(fEvents, 0, sizeof(fEvents));
}


void
BMediaSynth::MidiHook::_Push(const Event& event)
{
	if (fCount >= (int32)(sizeof(fEvents) / sizeof(fEvents[0])))
		return;
	fEvents[fCount++] = event;
}


void
BMediaSynth::MidiHook::NoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	Event e;
	e.what = Event::kNoteOn;
	e.channel = channel;
	e.data1 = note;
	e.data2 = velocity;
	e.time = time;
	e.data = NULL;
	e.length = 0;
	_Push(e);
}


void
BMediaSynth::MidiHook::NoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	Event e;
	e.what = Event::kNoteOff;
	e.channel = channel;
	e.data1 = note;
	e.data2 = velocity;
	e.time = time;
	e.data = NULL;
	e.length = 0;
	_Push(e);
}


void
BMediaSynth::MidiHook::KeyPressure(uchar channel, uchar note, uchar pressure,
	bigtime_t time)
{
	Event e;
	e.what = Event::kKeyPressure;
	e.channel = channel;
	e.data1 = note;
	e.data2 = pressure;
	e.time = time;
	e.data = NULL;
	e.length = 0;
	_Push(e);
}


void
BMediaSynth::MidiHook::ControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	Event e;
	e.what = Event::kControlChange;
	e.channel = channel;
	e.data1 = controlNumber;
	e.data2 = controlValue;
	e.time = time;
	e.data = NULL;
	e.length = 0;
	_Push(e);
}


void
BMediaSynth::MidiHook::ProgramChange(uchar channel, uchar programNumber,
	bigtime_t time)
{
	Event e;
	e.what = Event::kProgramChange;
	e.channel = channel;
	e.data1 = programNumber;
	e.data2 = 0;
	e.time = time;
	e.data = NULL;
	e.length = 0;
	_Push(e);
}


void
BMediaSynth::MidiHook::ChannelPressure(uchar channel, uchar pressure,
	bigtime_t time)
{
	Event e;
	e.what = Event::kChannelPressure;
	e.channel = channel;
	e.data1 = pressure;
	e.data2 = 0;
	e.time = time;
	e.data = NULL;
	e.length = 0;
	_Push(e);
}


void
BMediaSynth::MidiHook::PitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time)
{
	Event e;
	e.what = Event::kPitchBend;
	e.channel = channel;
	e.data1 = lsb;
	e.data2 = msb;
	e.time = time;
	e.data = NULL;
	e.length = 0;
	_Push(e);
}


void
BMediaSynth::MidiHook::SystemExclusive(void* data, size_t length,
	bigtime_t time)
{
	Event e;
	e.what = Event::kSystemExclusive;
	e.channel = 0;
	e.data1 = 0;
	e.data2 = 0;
	e.time = time;
	e.data = data;
	e.length = length;
	_Push(e);
}


BMediaSynth::AudioOutput::AudioOutput(const char* name)
	:
	BMediaConnection(B_MEDIA_OUTPUT, name),
	BMediaOutput(name)
{
	SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO));
}


BMediaSynth::AudioOutput::~AudioOutput()
{
}


status_t
BMediaSynth::AudioOutput::PrepareToConnect(BMediaFormat*)
{
	return B_OK;
}


status_t
BMediaSynth::AudioOutput::FormatProposal(BMediaFormat*)
{
	return B_OK;
}


BMediaSynth::BMediaSynth(const char* name, uint32 channelCount)
	:
	BMediaUnit(name, B_MEDIA_FILTER),
	fChannelCount(channelCount != 0 ? channelCount : 2),
	fMidiInput(NULL),
	fMidiHook(NULL),
	fEngineReady(false),
	fLastSampleRate(0),
	fRateChangePending(false)
{
	for (uint32 i = 0; i < 8; i++)
		fScratch[i] = NULL;

	fMidiHook = new(std::nothrow) MidiHook();
	if (fMidiHook == NULL)
		return;

	fMidiInput = fMidiHook;
	if (RegisterInput(fMidiInput) != B_OK) {
		delete fMidiHook;
		fMidiHook = NULL;
		fMidiInput = NULL;
		return;
	}

	// One mono float32 output port per channel (§6.2, §12.J).
	for (uint32 i = 0; i < fChannelCount && i < 8; i++) {
		char portName[32];
		snprintf(portName, sizeof(portName), "out%u", i);
		AudioOutput* output = new(std::nothrow) AudioOutput(portName);
		if (output == NULL)
			continue;
		if (RegisterOutput(output) != B_OK)
			delete output;
	}
}


BMediaSynth::~BMediaSynth()
{
	Stop();
	if (fMidiHook != NULL) {
		UnregisterInput(fMidiInput);
		delete fMidiHook;
		fMidiHook = NULL;
		fMidiInput = NULL;
	}
	for (uint32 i = 0; i < 8; i++) {
		delete[] fScratch[i];
		fScratch[i] = NULL;
	}
}


status_t
BMediaSynth::Start()
{
	status_t status = BMediaUnit::Start();
	if (status != B_OK)
		return status;

	uint32 rate = SampleRate();
	if (rate == 0)
		rate = 48000;
	status = SynthReset(rate);
	fLastSampleRate = rate;
	fEngineReady = (status == B_OK);
	fRateChangePending.store(false);
	return status;
}


status_t
BMediaSynth::Stop()
{
	// Panic: all voices off before the node leaves the graph.
	SynthShutdown();
	fEngineReady = false;
	return BMediaUnit::Stop();
}


status_t
BMediaSynth::SetChannelCount(uint32 count)
{
	if (count == 0 || count > 8)
		return B_BAD_VALUE;
	if (IsStarted())
		return B_NOT_ALLOWED;
	fChannelCount = count;
	return B_OK;
}


uint32
BMediaSynth::ChannelCount() const
{
	return fChannelCount;
}


BMidiInput*
BMediaSynth::MidiInput() const
{
	return fMidiInput;
}


status_t
BMediaSynth::SynthReset(uint32 sampleRate)
{
	fLastSampleRate = sampleRate;
	fEngineReady = true;
	return B_OK;
}


void
BMediaSynth::SynthShutdown()
{
	fEngineReady = false;
}


void
BMediaSynth::NoteOn(uchar, uchar, uchar, uint32)
{
}


void
BMediaSynth::NoteOff(uchar, uchar, uchar, uint32)
{
}


void
BMediaSynth::KeyPressure(uchar, uchar, uchar, uint32)
{
}


void
BMediaSynth::ControlChange(uchar, uchar, uchar, uint32)
{
}


void
BMediaSynth::ProgramChange(uchar, uchar, uint32)
{
}


void
BMediaSynth::ChannelPressure(uchar, uchar, uint32)
{
}


void
BMediaSynth::PitchBend(uchar, uchar, uchar, uint32)
{
}


void
BMediaSynth::SystemExclusive(void*, size_t, uint32)
{
}


void
BMediaSynth::_Advance(float** outputs, uint32 frames)
{
	for (uint32 i = 0; i < fChannelCount && i < 8; i++) {
		if (outputs[i] != NULL)
			outputs[i] += frames;
	}
}


void
BMediaSynth::_ZeroTail(float** outputs, uint32 frames)
{
	for (uint32 i = 0; i < fChannelCount && i < 8; i++) {
		if (outputs[i] != NULL && frames > 0)
			memset(outputs[i], 0, frames * sizeof(float));
	}
}


void
BMediaSynth::_DispatchNoteOn(uchar channel, uchar note, uchar velocity,
	uint32 offset)
{
	NoteOn(channel, note, velocity, offset);
}


void
BMediaSynth::_DispatchNoteOff(uchar channel, uchar note, uchar velocity,
	uint32 offset)
{
	NoteOff(channel, note, velocity, offset);
}


void
BMediaSynth::ProcessCycle(uint32 frameCount)
{
	if (fMidiHook == NULL || frameCount == 0)
		return;

	// Rate change: silence from the RT thread while the app thread rebuilds.
	if (fRateChangePending.load()) {
		for (int32 i = 0; i < CountOutputs(); i++) {
			BMediaOutput* output = OutputAt(i);
			if (output == NULL)
				continue;
			size_t size = 0;
			void* buffer = BufferFor(output, &size);
			if (buffer != NULL && size > 0)
				memset(buffer, 0, size);
		}
		return;
	}

	uint32 rate = SampleRate();
	if (rate == 0)
		rate = 48000;
	if (fLastSampleRate != 0 && rate != fLastSampleRate) {
		fRateChangePending.store(true);
		SynthShutdown();
	}

	MidiHook::Event* events = fMidiHook->Events();
	int32 count = fMidiHook->Count();

	// Convert absolute performance times (us) to frame offsets in this cycle.
	// Late events land at 0; events past the quantum clamp to frameCount.
	// This conversion lives here and in BMidiOutput::FillBuffer only.
	bigtime_t cycleStart = CycleStartTime();
	MidiHook::Event local[128];
	int32 converted = 0;

	for (int32 i = 0; i < count && converted < 128; i++) {
		MidiHook::Event e = events[i];
		bigtime_t delta = e.time - cycleStart;
		if (delta < 0)
			delta = 0;
		uint32 frames = (uint32)((double)delta * (double)rate / 1000000.0);
		if (frames > frameCount)
			frames = frameCount;
		// Reuse time as the frame offset for the walk below.
		e.time = (bigtime_t)frames;
		local[converted++] = e;
	}

	// Sort by offset. SPA sequences are ordered; keep the guarantee anyway.
	for (int32 i = 1; i < converted; i++) {
		MidiHook::Event key = local[i];
		int32 j = i - 1;
		while (j >= 0 && (uint32)local[j].time > (uint32)key.time) {
			local[j + 1] = local[j];
			j--;
		}
		local[j + 1] = key;
	}

	// Collect output buffer pointers for this cycle.
	float* outputs[8];
	memset(outputs, 0, sizeof(outputs));
	uint32 channels = fChannelCount;
	if (channels > 8)
		channels = 8;

	int32 outIndex = 0;
	for (int32 i = 0; i < CountOutputs() && outIndex < (int32)channels; i++) {
		BMediaOutput* output = OutputAt(i);
		if (output == NULL)
			continue;
		if ((output->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO)) == 0)
			continue;
		size_t size = 0;
		void* buffer = BufferFor(output, &size);
		if (buffer != NULL)
			outputs[outIndex++] = (float*)buffer;
	}

	if (!fEngineReady || outputs[0] == NULL) {
		_ZeroTail(outputs, frameCount);
		fMidiHook->Clear();
		return;
	}

	// A cycle with no events is one Render() call: the common case. Held
	// notes keep sounding through it.
	if (converted == 0) {
		Render(outputs, outIndex, frameCount);
		fMidiHook->Clear();
		return;
	}

	uint32 pos = 0;
	int32 index = 0;
	while (index < converted) {
		uint32 offset = (uint32)local[index].time;
		if (offset > frameCount)
			offset = frameCount;

		if (offset > pos) {
			Render(outputs, outIndex, offset - pos);
			_Advance(outputs, offset - pos);
			pos = offset;
		}

		while (index < converted && (uint32)local[index].time == offset) {
			MidiHook::Event& ev = local[index];
			switch (ev.what) {
				case MidiHook::Event::kNoteOn:
					_DispatchNoteOn(ev.channel, ev.data1, ev.data2, offset);
					break;
				case MidiHook::Event::kNoteOff:
					_DispatchNoteOff(ev.channel, ev.data1, ev.data2, offset);
					break;
				case MidiHook::Event::kKeyPressure:
					KeyPressure(ev.channel, ev.data1, ev.data2, offset);
					break;
				case MidiHook::Event::kControlChange:
					ControlChange(ev.channel, ev.data1, ev.data2, offset);
					break;
				case MidiHook::Event::kProgramChange:
					ProgramChange(ev.channel, ev.data1, offset);
					break;
				case MidiHook::Event::kChannelPressure:
					ChannelPressure(ev.channel, ev.data1, offset);
					break;
				case MidiHook::Event::kPitchBend:
					PitchBend(ev.channel, ev.data1, ev.data2, offset);
					break;
				case MidiHook::Event::kSystemExclusive:
					SystemExclusive((void*)ev.data, ev.length, offset);
					break;
			}
			index++;
		}
	}

	if (pos < frameCount)
		Render(outputs, outIndex, frameCount - pos);

	fMidiHook->Clear();
}
