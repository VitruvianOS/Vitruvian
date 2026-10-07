/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi2/MidiProducer.h>
#include <midi2/MidiConsumer.h>
#include <midi2/MidiRoster.h>

#include <new>
#include <string.h>

#include <Autolock.h>
#include <Locker.h>

#include <media2/MediaConnection.h>
#include <media2/MediaGraph.h>
#include <media2/MediaUnit.h>
#include <media2/MidiUnit.h>

#include "MidiSchedule.h"


BMidiProducer::BMidiProducer(const char *name)
	:
	BMidiEndpoint(name)
{
	fConnections = new BList;
	fOutput = NULL;
	memset(_reserved, 0, sizeof(_reserved));
	fIsConsumer = false;
	fIsLocal = true;
}


BMidiProducer::~BMidiProducer()
{
	delete fConnections;
	fConnections = NULL;
}


status_t
BMidiProducer::Connect(BMidiConsumer *cons)
{
	if (cons == NULL)
		return B_BAD_VALUE;

	{
		BAutolock lock(fLocker);
		if (fConnections->IndexOf(cons) >= 0)
			return B_OK;
	}

	// Real PipeWire link through the graph (W7/W8).
	status_t status = B_OK;
	BMidiOutput* output = Output();
	if (output != NULL && cons->Input() != NULL)
		status = MediaGraph()->Connect(output, cons->Input());
	else if (output != NULL) {
		// Remote consumer: connect via foreign ports.
		status = B_BAD_VALUE;
	}

	if (status != B_OK)
		return status;

	{
		BAutolock lock(fLocker);
		fConnections->AddItem(cons);
	}
	ConnectionMade(cons);
	return B_OK;
}


status_t
BMidiProducer::Disconnect(BMidiConsumer *cons)
{
	if (cons == NULL)
		return B_BAD_VALUE;

	{
		BAutolock lock(fLocker);
		if (fConnections->IndexOf(cons) < 0)
			return B_OK;
	}

	BMidiOutput* output = Output();
	if (output != NULL && cons->Input() != NULL)
		MediaGraph()->Disconnect(output, cons->Input());

	{
		BAutolock lock(fLocker);
		fConnections->RemoveItem(cons);
	}
	ConnectionBroken(cons);
	return B_OK;
}


bool
BMidiProducer::IsConnected(BMidiConsumer *cons) const
{
	if (cons == NULL)
		return false;
	BAutolock lock(fLocker);
	return fConnections->IndexOf(cons) >= 0;
}


BList *
BMidiProducer::Connections() const
{
	BAutolock lock(fLocker);
	return fConnections;
}


status_t
BMidiProducer::SendConnectRequest(BMidiConsumer *, bool)
{
	return B_OK;
}


void
BMidiProducer::ConnectionMade(BMidiConsumer *)
{
}


bool
BMidiProducer::ConnectionBroken(BMidiConsumer *)
{
	return true;
}


int32
BMidiProducer::CountConsumers() const
{
	BAutolock lock(fLocker);
	return fConnections->CountItems();
}


BMidiConsumer *
BMidiProducer::ConsumerAt(int32 index) const
{
	BAutolock lock(fLocker);
	return (BMidiConsumer*)fConnections->ItemAt(index);
}


bool
BMidiProducer::LockProducer() const
{
	return fLocker.Lock();
}


void
BMidiProducer::UnlockProducer() const
{
	fLocker.Unlock();
}


void
BMidiProducer::_Reserved1()
{
}


void
BMidiProducer::_Reserved2()
{
}


void
BMidiProducer::_Reserved3()
{
}


void
BMidiProducer::_Reserved4()
{
}


void
BMidiProducer::_Reserved5()
{
}


void
BMidiProducer::_Reserved6()
{
}


void
BMidiProducer::_Reserved7()
{
}


void
BMidiProducer::_Reserved8()
{
}


BMidiLocalProducer::BMidiLocalProducer(const char *name)
	:
	BMidiProducer(name)
{
	fSchedule = NULL;

	// Own one PipeWire node with a MIDI output port.
	BMidiUnit* unit = new(std::nothrow) BMidiUnit(
		name != NULL ? name : "midi2 producer", B_MEDIA_PLAYER);
	if (unit == NULL)
		return;

	BMidiOutput* output = new(std::nothrow) BMidiOutput("midi out");
	if (output == NULL) {
		delete unit;
		return;
	}
	output->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_MIDI));

	if (unit->RegisterOutput(output) != B_OK) {
		delete output;
		delete unit;
		return;
	}

	SetUnit(unit);
	SetOutput(output);
	fIsLocal = true;
	fIsConsumer = false;

	fSchedule = new(std::nothrow) BMidiSchedule(this);
	if (fSchedule != NULL)
		((BMidiSchedule*)fSchedule)->SetOutput(output);
}


BMidiLocalProducer::~BMidiLocalProducer()
{
	delete (BMidiSchedule*)fSchedule;
	fSchedule = NULL;
	if (Unit() != NULL)
		Unit()->Stop();
}


void
BMidiLocalProducer::Connected(BMidiConsumer *)
{
}


void
BMidiLocalProducer::Disconnected(BMidiConsumer *)
{
}


void
BMidiLocalProducer::SprayEvent(const void *data, size_t length, bool,
	bigtime_t time, bool sysex) const
{
	if (data == NULL || length == 0)
		return;

	BMidiSchedule* schedule = (BMidiSchedule*)fSchedule;
	if (schedule == NULL)
		return;

	// The façade queue decides WHEN; BMidiOutput's ring decides how to
	// cross the RT boundary. time==0 means now against the graph clock.
	schedule->Enqueue(data, length, time, sysex);
}


void
BMidiLocalProducer::SprayData(void *data, size_t length, bool atomic,
	bigtime_t time) const
{
	SprayEvent(data, length, atomic, time, false);
}


void
BMidiLocalProducer::SprayNoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time) const
{
	uint8 msg[3] = { (uint8)(0x80 | (channel & 0x0f)), note, velocity };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SprayNoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time) const
{
	uint8 msg[3] = { (uint8)(0x90 | (channel & 0x0f)), note, velocity };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SprayKeyPressure(uchar channel, uchar note,
	uchar pressure, bigtime_t time) const
{
	uint8 msg[3] = { (uint8)(0xa0 | (channel & 0x0f)), note, pressure };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SprayControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time) const
{
	uint8 msg[3] = { (uint8)(0xb0 | (channel & 0x0f)), controlNumber,
		controlValue };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SprayProgramChange(uchar channel, uchar programNumber,
	bigtime_t time) const
{
	uint8 msg[2] = { (uint8)(0xc0 | (channel & 0x0f)), programNumber };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SprayChannelPressure(uchar channel, uchar pressure,
	bigtime_t time) const
{
	uint8 msg[2] = { (uint8)(0xd0 | (channel & 0x0f)), pressure };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SprayPitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time) const
{
	uint8 msg[3] = { (uint8)(0xe0 | (channel & 0x0f)), lsb, msb };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SpraySystemExclusive(void *data, size_t length,
	bigtime_t time) const
{
	SprayEvent(data, length, false, time, true);
}


void
BMidiLocalProducer::SpraySystemCommon(uchar status, uchar data1, uchar data2,
	bigtime_t time) const
{
	uint8 msg[3] = { status, data1, data2 };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SpraySystemRealTime(uchar status, bigtime_t time) const
{
	uint8 msg[1] = { status };
	SprayEvent(msg, sizeof(msg), false, time, false);
}


void
BMidiLocalProducer::SprayTempoChange(int32 beatsPerMinute, bigtime_t time) const
{
	// Tempo is a midi1 file concern; midi2 producers carry it as CC or
	// leave it to BMidiStore. Forward as a no-op tempo event to consumers
	// that implement TempoChange via a private channel message.
	(void)beatsPerMinute;
	(void)time;
}


void
BMidiLocalProducer::_Reserved1()
{
}


void
BMidiLocalProducer::_Reserved2()
{
}


void
BMidiLocalProducer::_Reserved3()
{
}


void
BMidiLocalProducer::_Reserved4()
{
}


void
BMidiLocalProducer::_Reserved5()
{
}


void
BMidiLocalProducer::_Reserved6()
{
}


void
BMidiLocalProducer::_Reserved7()
{
}


void
BMidiLocalProducer::_Reserved8()
{
}
