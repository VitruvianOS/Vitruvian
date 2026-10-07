/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MidiSchedule.h"

#include <new>
#include <string.h>

#include <Autolock.h>

#include <media2/MediaConnection.h>


BMidiSchedule::BMidiSchedule(BMidiLocalProducer* producer)
	:
	fLock("midi2 schedule"),
	fOutput(NULL),
	fProducer(producer),
	fThread(-1),
	fRunning(false),
	fWake(-1)
{
	fWake = create_sem(0, "midi2 schedule wake");
	if (fWake < 0)
		return;

	fRunning = true;
	fThread = spawn_thread(_SchedulerThread, "midi2 scheduler",
		B_NORMAL_PRIORITY, this);
	if (fThread >= 0)
		resume_thread(fThread);
}


BMidiSchedule::~BMidiSchedule()
{
	Kick();
	fRunning = false;
	if (fWake >= 0)
		release_sem(fWake);
	if (fThread >= 0) {
		int32 result;
		wait_for_thread(fThread, &result);
		fThread = -1;
	}

	BAutolock lock(fLock);
	for (int32 i = 0; i < fQueue.CountItems(); i++)
		delete (Event*)fQueue.ItemAt(i);
	fQueue.MakeEmpty();
	if (fWake >= 0) {
		delete_sem(fWake);
		fWake = -1;
	}
}


void
BMidiSchedule::SetOutput(BMidiOutput* output)
{
	BAutolock lock(fLock);
	fOutput = output;
}


bool
BMidiSchedule::IsEmpty() const
{
	BAutolock lock(fLock);
	return fQueue.IsEmpty();
}


void
BMidiSchedule::Kick()
{
	if (fWake >= 0)
		release_sem(fWake);
}


status_t
BMidiSchedule::Enqueue(const void* data, size_t length, bigtime_t time,
	bool sysex)
{
	if (data == NULL || length == 0)
		return B_BAD_VALUE;
	if (length > 256 && !sysex)
		length = 256;
	if (sysex && length > 256)
		length = 256;

	bigtime_t when = time;
	if (when == 0)
		when = system_time();

	// Due now: push straight into the output ring.
	if (when <= system_time() || fOutput == NULL || fThread < 0) {
		BMidiOutput* output = fOutput;
		if (output == NULL)
			return B_NO_INIT;
		if (sysex)
			return output->SendSystemExclusive(data, length, when);
		const uint8* bytes = (const uint8*)data;
		if (length >= 1) {
			switch (bytes[0] & 0xf0) {
				case 0x80:
					if (length >= 3)
						return output->SendNoteOff(bytes[0] & 0x0f, bytes[1],
							bytes[2], when);
					break;
				case 0x90:
					if (length >= 3)
						return output->SendNoteOn(bytes[0] & 0x0f, bytes[1],
							bytes[2], when);
					break;
				case 0xa0:
					if (length >= 3)
						return output->SendKeyPressure(bytes[0] & 0x0f,
							bytes[1], bytes[2], when);
					break;
				case 0xb0:
					if (length >= 3)
						return output->SendControlChange(bytes[0] & 0x0f,
							bytes[1], bytes[2], when);
					break;
				case 0xc0:
					if (length >= 2)
						return output->SendProgramChange(bytes[0] & 0x0f,
							bytes[1], when);
					break;
				case 0xd0:
					if (length >= 2)
						return output->SendChannelPressure(bytes[0] & 0x0f,
							bytes[1], when);
					break;
				case 0xe0:
					if (length >= 3)
						return output->SendPitchBend(bytes[0] & 0x0f, bytes[1],
							bytes[2], when);
					break;
				case 0xf0:
					if (length >= 3)
						return output->SendSystemCommon(bytes[0], bytes[1],
							bytes[2], when);
					if (length >= 1)
						return output->SendSystemRealTime(bytes[0], when);
					break;
			}
		}
		return B_OK;
	}

	Event* event = new(std::nothrow) Event();
	if (event == NULL)
		return B_NO_MEMORY;
	event->time = when;
	event->length = length;
	event->sysex = sysex;
	memset(event->data, 0, sizeof(event->data));
	memset(event->sysEx, 0, sizeof(event->sysEx));
	if (sysex)
		memcpy(event->sysEx, data, length);
	else
		memcpy(event->data, data, length);

	// Backpressure on the spraying app thread when the queue is full.
	// Blocking an app thread is legitimate; never block the RT thread.
	for (;;) {
		{
			BAutolock lock(fLock);
			if (fQueue.CountItems() < kMaxQueued) {
				// Keep chronological order: producers sort events, consumers
				// may assume ascending time.
				int32 index = 0;
				int32 count = fQueue.CountItems();
				while (index < count) {
					Event* existing = (Event*)fQueue.ItemAt(index);
					if (existing->time > event->time)
						break;
					index++;
				}
				fQueue.AddItem(event, index);
				Kick();
				return B_OK;
			}
		}
		if (!fRunning)
			break;
		snooze(1000);
	}

	delete event;
	return B_ERROR;
}


void
BMidiSchedule::Drain()
{
	bigtime_t now = system_time();
	BMidiOutput* output = NULL;
	{
		BAutolock lock(fLock);
		output = fOutput;
	}

	if (output == NULL)
		return;

	for (;;) {
		Event* event = NULL;
		{
			BAutolock lock(fLock);
			if (fQueue.IsEmpty())
				break;
			Event* head = (Event*)fQueue.ItemAt(0);
			if (head->time > now)
				break;
			event = head;
			fQueue.RemoveItem(0);
		}

		if (event == NULL)
			break;

		if (event->sysex)
			output->SendSystemExclusive(event->sysEx, event->length, event->time);
		else {
			const uint8* bytes = event->data;
			size_t length = event->length;
			bigtime_t when = event->time;
			if (length >= 1) {
				switch (bytes[0] & 0xf0) {
					case 0x80:
						if (length >= 3)
							output->SendNoteOff(bytes[0] & 0x0f, bytes[1],
								bytes[2], when);
						break;
					case 0x90:
						if (length >= 3)
							output->SendNoteOn(bytes[0] & 0x0f, bytes[1],
								bytes[2], when);
						break;
					case 0xa0:
						if (length >= 3)
							output->SendKeyPressure(bytes[0] & 0x0f, bytes[1],
								bytes[2], when);
						break;
					case 0xb0:
						if (length >= 3)
							output->SendControlChange(bytes[0] & 0x0f,
								bytes[1], bytes[2], when);
						break;
					case 0xc0:
						if (length >= 2)
							output->SendProgramChange(bytes[0] & 0x0f,
								bytes[1], when);
						break;
					case 0xd0:
						if (length >= 2)
							output->SendChannelPressure(bytes[0] & 0x0f,
								bytes[1], when);
						break;
					case 0xe0:
						if (length >= 3)
							output->SendPitchBend(bytes[0] & 0x0f, bytes[1],
								bytes[2], when);
						break;
					case 0xf0:
						if (length >= 3)
							output->SendSystemCommon(bytes[0], bytes[1],
								bytes[2], when);
						if (length >= 1)
							output->SendSystemRealTime(bytes[0], when);
						break;
				}
			}
		}
		delete event;
	}
}


int32
BMidiSchedule::_SchedulerThread(void* data)
{
	BMidiSchedule* self = (BMidiSchedule*)data;
	return self->_SchedulerLoop();
}


int32
BMidiSchedule::_SchedulerLoop()
{
	while (fRunning) {
		Drain();
		if (fWake >= 0)
			acquire_sem_etc(fWake, 1, B_TIMEOUT, 2000);
	}
	Drain();
	return 0;
}
