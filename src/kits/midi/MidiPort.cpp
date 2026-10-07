/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi/MidiPort.h>
#include <midi2/MidiConsumer.h>
#include <midi2/MidiProducer.h>
#include <midi2/MidiRoster.h>

#include <new>
#include <stdlib.h>
#include <string.h>

#include <List.h>
#include <Locker.h>


BMidiPort::BMidiPort(const char* name)
	:
	super()
{
	fLocalSource = NULL;
	fLocalSink = NULL;
	fRemoteSource = NULL;
	fRemoteSink = NULL;
	fPortName = NULL;
	fStatus = B_NO_INIT;
	fDevices = new BList;
	memset(_reserved, 0, sizeof(_reserved));

	if (name != NULL)
		fStatus = Open(name);
}


BMidiPort::~BMidiPort()
{
	Close();
	EmptyDeviceList();
	delete fDevices;
	fDevices = NULL;
}


status_t
BMidiPort::InitCheck() const
{
	return fStatus;
}


status_t
BMidiPort::Open(const char* name)
{
	if (name == NULL)
		return B_BAD_VALUE;

	ScanDevices();

	// Enumerate MIDI nodes in the graph, not /dev/midi scanning.
	int32 id = 0;
	BMidiConsumer* consumer = NULL;
	BMidiProducer* producer = NULL;
	while ((consumer = BMidiRoster::NextConsumer(&id)) != NULL) {
		if (strcmp(consumer->Name(), name) == 0) {
			fRemoteSink = consumer;
			fStatus = B_OK;
			break;
		}
		consumer->Release();
	}

	id = 0;
	while ((producer = BMidiRoster::NextProducer(&id)) != NULL) {
		if (strcmp(producer->Name(), name) == 0) {
			fRemoteSource = producer;
			fStatus = B_OK;
			break;
		}
		producer->Release();
	}

	if (fStatus != B_OK)
		return fStatus;

	if (fPortName != NULL)
		free(fPortName);
	fPortName = strdup(name);
	if (fPortName == NULL) {
		fStatus = B_NO_MEMORY;
		return fStatus;
	}

	return B_OK;
}


void
BMidiPort::Close()
{
	if (fRemoteSource != NULL) {
		fRemoteSource->Release();
		fRemoteSource = NULL;
	}
	if (fRemoteSink != NULL) {
		fRemoteSink->Release();
		fRemoteSink = NULL;
	}
	if (fPortName != NULL) {
		free(fPortName);
		fPortName = NULL;
	}
	fStatus = B_NO_INIT;
}


const char*
BMidiPort::PortName() const
{
	return fPortName;
}


void
BMidiPort::NoteOff(uchar channel, uchar note, uchar velocity, uint32 time)
{
	if (fRemoteSource != NULL) {
		// Ports receive into their local sink; spray is for sources.
		super::NoteOff(channel, note, velocity, time);
	} else
		super::NoteOff(channel, note, velocity, time);
}


void
BMidiPort::NoteOn(uchar channel, uchar note, uchar velocity, uint32 time)
{
	super::NoteOn(channel, note, velocity, time);
}


void
BMidiPort::KeyPressure(uchar channel, uchar note, uchar pressure, uint32 time)
{
	super::KeyPressure(channel, note, pressure, time);
}


void
BMidiPort::ControlChange(uchar channel, uchar controlNumber, uchar controlValue,
	uint32 time)
{
	super::ControlChange(channel, controlNumber, controlValue, time);
}


void
BMidiPort::ProgramChange(uchar channel, uchar programNumber, uint32 time)
{
	super::ProgramChange(channel, programNumber, time);
}


void
BMidiPort::ChannelPressure(uchar channel, uchar pressure, uint32 time)
{
	super::ChannelPressure(channel, pressure, time);
}


void
BMidiPort::PitchBend(uchar channel, uchar lsb, uchar msb, uint32 time)
{
	super::PitchBend(channel, lsb, msb, time);
}


void
BMidiPort::SystemExclusive(void* data, size_t length, uint32 time)
{
	super::SystemExclusive(data, length, time);
}


void
BMidiPort::SystemCommon(uchar status, uchar data0, uchar data2, uint32 time)
{
	super::SystemCommon(status, data0, data2, time);
}


void
BMidiPort::SystemRealTime(uchar status, uint32 time)
{
	super::SystemRealTime(status, time);
}


status_t
BMidiPort::Start()
{
	if (fStatus != B_OK)
		return fStatus;
	return super::Start();
}


void
BMidiPort::Stop()
{
	super::Stop();
}


int32
BMidiPort::CountDevices()
{
	ScanDevices();
	return fDevices != NULL ? fDevices->CountItems() : 0;
}


status_t
BMidiPort::GetDeviceName(int32 n, char* name, size_t bufSize)
{
	if (name == NULL || bufSize == 0)
		return B_BAD_VALUE;
	ScanDevices();
	if (fDevices == NULL || n < 0 || n >= fDevices->CountItems())
		return B_BAD_VALUE;
	const char* deviceName = (const char*)fDevices->ItemAt(n);
	if (deviceName == NULL)
		return B_BAD_VALUE;
	strncpy(name, deviceName, bufSize - 1);
	name[bufSize - 1] = '\0';
	return B_OK;
}


void
BMidiPort::ScanDevices()
{
	if (fDevices == NULL)
		return;
	EmptyDeviceList();

	int32 id = 0;
	BMidiConsumer* consumer;
	while ((consumer = BMidiRoster::NextConsumer(&id)) != NULL) {
		fDevices->AddItem(strdup(consumer->Name()));
		consumer->Release();
	}

	id = 0;
	BMidiProducer* producer;
	while ((producer = BMidiRoster::NextProducer(&id)) != NULL) {
		fDevices->AddItem(strdup(producer->Name()));
		producer->Release();
	}
}


void
BMidiPort::EmptyDeviceList()
{
	if (fDevices == NULL)
		return;
	for (int32 i = 0; i < fDevices->CountItems(); i++) {
		char* name = (char*)fDevices->ItemAt(i);
		free(name);
	}
	fDevices->MakeEmpty();
}


void
BMidiPort::Run()
{
	// A port's Run thread pumps received events into the BMidi virtuals.
	// Delivery happens on the midi2 consumer thread; this loop only keeps
	// the thread alive for apps that expect Run() to block.
	while (KeepRunning())
		snooze(20000);
}


void
BMidiPort::_ReservedMidiPort1()
{
}


void
BMidiPort::_ReservedMidiPort2()
{
}


void
BMidiPort::_ReservedMidiPort3()
{
}
