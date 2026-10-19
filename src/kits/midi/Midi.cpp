/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi/Midi.h>
#include <midi2/MidiConsumer.h>
#include <midi2/MidiProducer.h>
#include <midi2/MidiRoster.h>

#include <new>
#include <string.h>

#include <Autolock.h>
#include <List.h>
#include <Locker.h>


namespace {

// Hands events that reach a BMidi's consumer to the object's own hooks, as
// the legacy kit does; a plain BMidiLocalConsumer drops them.
class MidiForwarder : public BMidiLocalConsumer {
public:
	MidiForwarder(BMidi* owner) : fOwner(owner) {}

	void NoteOff(uchar c, uchar n, uchar v, bigtime_t t)
		{ fOwner->NoteOff(c, n, v, _Ms(t)); }
	void NoteOn(uchar c, uchar n, uchar v, bigtime_t t)
		{ fOwner->NoteOn(c, n, v, _Ms(t)); }
	void KeyPressure(uchar c, uchar n, uchar p, bigtime_t t)
		{ fOwner->KeyPressure(c, n, p, _Ms(t)); }
	void ControlChange(uchar c, uchar n, uchar v, bigtime_t t)
		{ fOwner->ControlChange(c, n, v, _Ms(t)); }
	void ProgramChange(uchar c, uchar n, bigtime_t t)
		{ fOwner->ProgramChange(c, n, _Ms(t)); }
	void ChannelPressure(uchar c, uchar p, bigtime_t t)
		{ fOwner->ChannelPressure(c, p, _Ms(t)); }
	void PitchBend(uchar c, uchar l, uchar m, bigtime_t t)
		{ fOwner->PitchBend(c, l, m, _Ms(t)); }
	void SystemExclusive(void* d, size_t l, bigtime_t t)
		{ fOwner->SystemExclusive(d, l, _Ms(t)); }
	void SystemCommon(uchar s, uchar d1, uchar d2, bigtime_t t)
		{ fOwner->SystemCommon(s, d1, d2, _Ms(t)); }
	void SystemRealTime(uchar s, bigtime_t t)
		{ fOwner->SystemRealTime(s, _Ms(t)); }
	void TempoChange(int32 bpm, bigtime_t t)
		{ fOwner->TempoChange(bpm, _Ms(t)); }
	void AllNotesOff(bool justChannel, bigtime_t t)
		{ fOwner->AllNotesOff(justChannel, _Ms(t)); }

private:
	static uint32 _Ms(bigtime_t t) { return (uint32)(t / 1000); }

	BMidi*	fOwner;
};

}	// namespace


static BLocker sConnectionsLock("midi connections");


// One clearly named helper each side of the M1/M2 boundary. midi1 time is
// milliseconds since boot; midi2 is microseconds. A silent unit error here
// produces events 1000x early or late.
bigtime_t
BMidi::_Midi1ToMidi2(uint32 time)
{
	if (time == 0)
		return 0;
	return (bigtime_t)time * 1000;
}


uint32
BMidi::_Midi2ToMidi1(bigtime_t time)
{
	return (uint32)(time / 1000);
}


status_t
_run_thread(void* data)
{
	BMidi* midi = (BMidi*)data;
	if (midi == NULL)
		return B_BAD_VALUE;
	midi->Run();
	return B_OK;
}


BMidi::BMidi()
{
	fProducer = new(std::nothrow) BMidiLocalProducer();
	fConsumer = new(std::nothrow) MidiForwarder(this);
	fConnections = new BList;
	fThreadId = -1;
	fIsRunning = false;
	memset(_reserved2, 0, sizeof(_reserved2));
}


BMidi::~BMidi()
{
	Stop();
	delete fConnections;
	fConnections = NULL;
	delete fProducer;
	fProducer = NULL;
	delete fConsumer;
	fConsumer = NULL;
}


void
BMidi::NoteOff(uchar channel, uchar note, uchar velocity, uint32 time)
{
	SprayNoteOff(channel, note, velocity, time);
}


void
BMidi::NoteOn(uchar channel, uchar note, uchar velocity, uint32 time)
{
	SprayNoteOn(channel, note, velocity, time);
}


void
BMidi::KeyPressure(uchar channel, uchar note, uchar pressure, uint32 time)
{
	SprayKeyPressure(channel, note, pressure, time);
}


void
BMidi::ControlChange(uchar channel, uchar controlNumber, uchar controlValue,
	uint32 time)
{
	SprayControlChange(channel, controlNumber, controlValue, time);
}


void
BMidi::ProgramChange(uchar channel, uchar programNumber, uint32 time)
{
	SprayProgramChange(channel, programNumber, time);
}


void
BMidi::ChannelPressure(uchar channel, uchar pressure, uint32 time)
{
	SprayChannelPressure(channel, pressure, time);
}


void
BMidi::PitchBend(uchar channel, uchar lsb, uchar msb, uint32 time)
{
	SprayPitchBend(channel, lsb, msb, time);
}


void
BMidi::SystemExclusive(void* data, size_t length, uint32 time)
{
	SpraySystemExclusive(data, length, time);
}


void
BMidi::SystemCommon(uchar status, uchar data1, uchar data2, uint32 time)
{
	SpraySystemCommon(status, data1, data2, time);
}


void
BMidi::SystemRealTime(uchar status, uint32 time)
{
	SpraySystemRealTime(status, time);
}


void
BMidi::TempoChange(int32 beatsPerMinute, uint32 time)
{
	SprayTempoChange(beatsPerMinute, time);
}


void
BMidi::AllNotesOff(bool justChannel, uint32 time)
{
	(void)justChannel;
	for (int32 channel = 0; channel < 16; channel++)
		SprayControlChange((uchar)channel, B_ALL_NOTES_OFF, 0, time);
}


status_t
BMidi::Start()
{
	if (fIsRunning)
		return B_OK;
	fIsRunning = true;
	fThreadId = spawn_thread(_run_thread, "midi run", B_NORMAL_PRIORITY,
		this);
	if (fThreadId < 0) {
		fIsRunning = false;
		return fThreadId;
	}
	resume_thread(fThreadId);
	return B_OK;
}


void
BMidi::Stop()
{
	if (!fIsRunning)
		return;
	fIsRunning = false;
	if (fThreadId >= 0) {
		int32 result;
		wait_for_thread(fThreadId, &result);
		fThreadId = -1;
	}
}


bool
BMidi::IsRunning() const
{
	return fIsRunning;
}


void
BMidi::Connect(BMidi* toObject)
{
	if (toObject == NULL)
		return;

	// One way: what we spray reaches them. A return path would feed their
	// output back in, and a BMidiStore records whatever it receives.
	if (fProducer != NULL && toObject->fConsumer != NULL)
		fProducer->Connect(toObject->fConsumer);

	BAutolock lock(sConnectionsLock);
	fConnections->AddItem(toObject);
}


void
BMidi::Disconnect(BMidi* fromObject)
{
	if (fromObject == NULL)
		return;

	if (fProducer != NULL && fromObject->fConsumer != NULL)
		fProducer->Disconnect(fromObject->fConsumer);

	BAutolock lock(sConnectionsLock);
	fConnections->RemoveItem(fromObject);
}


bool
BMidi::IsConnected(BMidi* toObject) const
{
	if (toObject == NULL)
		return false;
	BAutolock lock(sConnectionsLock);
	return fConnections->IndexOf(toObject) >= 0;
}


BList*
BMidi::Connections() const
{
	BAutolock lock(sConnectionsLock);
	return fConnections;
}


void
BMidi::SnoozeUntil(uint32 time) const
{
	// Returns immediately if the time has passed.
	uint32 now = B_NOW;
	if (time <= now)
		return;
	snooze_until((bigtime_t)time * 1000, B_SYSTEM_TIMEBASE);
}


void
BMidi::SprayNoteOff(uchar channel, uchar note, uchar velocity, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SprayNoteOff(channel, note, velocity, _Midi1ToMidi2(time));
}


void
BMidi::SprayNoteOn(uchar channel, uchar note, uchar velocity, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SprayNoteOn(channel, note, velocity, _Midi1ToMidi2(time));
}


void
BMidi::SprayKeyPressure(uchar channel, uchar note, uchar pressure, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SprayKeyPressure(channel, note, pressure,
			_Midi1ToMidi2(time));
}


void
BMidi::SprayControlChange(uchar channel, uchar controlNumber, uchar controlValue,
	uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SprayControlChange(channel, controlNumber, controlValue,
			_Midi1ToMidi2(time));
}


void
BMidi::SprayProgramChange(uchar channel, uchar programNumber, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SprayProgramChange(channel, programNumber,
			_Midi1ToMidi2(time));
}


void
BMidi::SprayChannelPressure(uchar channel, uchar pressure, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SprayChannelPressure(channel, pressure,
			_Midi1ToMidi2(time));
}


void
BMidi::SprayPitchBend(uchar channel, uchar lsb, uchar msb, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SprayPitchBend(channel, lsb, msb, _Midi1ToMidi2(time));
}


void
BMidi::SpraySystemExclusive(void* data, size_t length, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SpraySystemExclusive(data, length, _Midi1ToMidi2(time));
}


void
BMidi::SpraySystemCommon(uchar status, uchar data1, uchar data2, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SpraySystemCommon(status, data1, data2,
			_Midi1ToMidi2(time));
}


void
BMidi::SpraySystemRealTime(uchar status, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SpraySystemRealTime(status, _Midi1ToMidi2(time));
}


void
BMidi::SprayTempoChange(int32 beatsPerMinute, uint32 time) const
{
	if (fProducer != NULL)
		fProducer->SprayTempoChange(beatsPerMinute, _Midi1ToMidi2(time));
}


bool
BMidi::KeepRunning()
{
	return fIsRunning;
}


void
BMidi::Run()
{
	// Default Run does nothing; subclasses provide the app-thread loop.
}


void
BMidi::_ReservedMidi1()
{
}


void
BMidi::_ReservedMidi2()
{
}


void
BMidi::_ReservedMidi3()
{
}
