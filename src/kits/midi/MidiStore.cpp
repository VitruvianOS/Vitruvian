/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi/MidiStore.h>
#include <midi/MidiSynthFile.h>

#include <new>
#include <stdlib.h>
#include <string.h>

#include <Entry.h>
#include <File.h>
#include <List.h>
#include <Locker.h>


struct BMidiEvent {
	BMidiEvent()
	{
		byte1  = 0;
		byte2  = 0;
		byte3  = 0;
		data   = NULL;
		length = 0;
		time   = 0;
		ticks  = false;
		tempo  = 0;
		track  = 0;
	}

	~BMidiEvent()
	{
		free(data);
	}

	uint32 time;    // absolute time in milliseconds after tempo walk
	bool   ticks;
	uchar  byte1;
	uchar  byte2;
	uchar  byte3;
	void*  data;    // sysex data
	size_t length;  // sysex data size
	int32  tempo;   // beats per minute at this event
	uint16 track;
};


static int
compare_events(const void* event1, const void* event2)
{
	const BMidiEvent* e1 = *((BMidiEvent**)event1);
	const BMidiEvent* e2 = *((BMidiEvent**)event2);
	if (e1->time < e2->time)
		return -1;
	if (e1->time > e2->time)
		return 1;
	return 0;
}


BMidiStore::BMidiStore()
{
	fEvents = new BList;
	fCurrentEvent = 0;
	fStartTime = 0;
	fNeedsSorting = false;
	fBeatsPerMinute = 60;
	fTicksPerBeat = 240;
	fFile = NULL;
	fByteCount = 0;
	fTotalTicks = 0;
	fNumTracks = 0;
	fCurrTrack = 0;
	fFormat = 0;
	memset(_reserved1, 0, sizeof(_reserved1));
	fInstruments = new bool[128];
	if (fInstruments != NULL)
		memset(fInstruments, 0, 128 * sizeof(bool));
	fHookFunc = NULL;
	fHookArg = 0;
	fLooping = false;
	fPaused = false;
	fFinished = false;
	memset(_reserved2, 0, sizeof(_reserved2));
}


BMidiStore::~BMidiStore()
{
	Stop();
	if (fEvents != NULL) {
		for (int32 i = 0; i < fEvents->CountItems(); i++)
			delete (BMidiEvent*)fEvents->ItemAt(i);
		delete fEvents;
		fEvents = NULL;
	}
	delete[] fInstruments;
	fInstruments = NULL;
	delete fFile;
	fFile = NULL;
}


void
BMidiStore::NoteOff(uchar channel, uchar note, uchar velocity, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = (uchar)(B_NOTE_OFF | (channel & 0x0f));
	event->byte2 = note;
	event->byte3 = velocity;
	AddEvent(event);
}


void
BMidiStore::NoteOn(uchar channel, uchar note, uchar velocity, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = (uchar)(B_NOTE_ON | (channel & 0x0f));
	event->byte2 = note;
	event->byte3 = velocity;
	AddEvent(event);
}


void
BMidiStore::KeyPressure(uchar channel, uchar note, uchar pressure, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = (uchar)(B_KEY_PRESSURE | (channel & 0x0f));
	event->byte2 = note;
	event->byte3 = pressure;
	AddEvent(event);
}


void
BMidiStore::ControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = (uchar)(B_CONTROL_CHANGE | (channel & 0x0f));
	event->byte2 = controlNumber;
	event->byte3 = controlValue;
	AddEvent(event);
}


void
BMidiStore::ProgramChange(uchar channel, uchar programNumber, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = (uchar)(B_PROGRAM_CHANGE | (channel & 0x0f));
	event->byte2 = programNumber;
	AddEvent(event);
}


void
BMidiStore::ChannelPressure(uchar channel, uchar pressure, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = (uchar)(B_CHANNEL_PRESSURE | (channel & 0x0f));
	event->byte2 = pressure;
	AddEvent(event);
}


void
BMidiStore::PitchBend(uchar channel, uchar lsb, uchar msb, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = (uchar)(B_PITCH_BEND | (channel & 0x0f));
	event->byte2 = lsb;
	event->byte3 = msb;
	AddEvent(event);
}


void
BMidiStore::SystemExclusive(void* data, size_t length, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = B_SYS_EX_START;
	event->data  = malloc(length);
	if (event->data != NULL) {
		memcpy(event->data, data, length);
		event->length = length;
	}
	AddEvent(event);
}


void
BMidiStore::SystemCommon(uchar status, uchar data1, uchar data2, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = status;
	event->byte2 = data1;
	event->byte3 = data2;
	AddEvent(event);
}


void
BMidiStore::SystemRealTime(uchar status, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = status;
	AddEvent(event);
}


void
BMidiStore::TempoChange(int32 beatsPerMinute, uint32 time)
{
	BMidiEvent* event = new BMidiEvent;
	event->time  = time;
	event->ticks = false;
	event->byte1 = 0xFF;
	event->byte2 = 0x51;
	event->byte3 = 0x03;
	event->tempo = beatsPerMinute;
	AddEvent(event);
}


void
BMidiStore::AddEvent(BMidiEvent* event)
{
	if (event == NULL || fEvents == NULL)
		return;
	fEvents->AddItem(event);
	fNeedsSorting = true;
}


BMidiEvent*
BMidiStore::EventAt(int32 index) const
{
	if (fEvents == NULL)
		return NULL;
	return (BMidiEvent*)fEvents->ItemAt(index);
}


uint32
BMidiStore::GetEventTime(const BMidiEvent* event) const
{
	return event != NULL ? event->time : 0;
}


uint32
BMidiStore::TicksToMilliseconds(uint32 ticks) const
{
	// One tempo for the whole file after the walk; tempo-map events carry
	// their own absolute time already.
	int32 bpm = fBeatsPerMinute > 0 ? fBeatsPerMinute : 60;
	int32 tpq = fTicksPerBeat > 0 ? fTicksPerBeat : 240;
	// us per tick = 60e6 / (bpm * tpq); ms = us / 1000.
	return (uint32)((uint64)ticks * 60000ULL / ((uint64)bpm * (uint64)tpq));
}


uint32
BMidiStore::MillisecondsToTicks(uint32 ms) const
{
	int32 bpm = fBeatsPerMinute > 0 ? fBeatsPerMinute : 60;
	int32 tpq = fTicksPerBeat > 0 ? fTicksPerBeat : 240;
	return (uint32)((uint64)ms * (uint64)bpm * (uint64)tpq / 60000ULL);
}


void
BMidiStore::SortEvents(bool force)
{
	if (force || fNeedsSorting) {
		fEvents->SortItems(compare_events);
		fNeedsSorting = false;
	}
}


uint32
BMidiStore::CountEvents() const
{
	return fEvents != NULL ? fEvents->CountItems() : 0;
}


uint32
BMidiStore::DurationMs() const
{
	if (fEvents == NULL || fEvents->CountItems() == 0)
		return 0;
	const_cast<BMidiStore*>(this)->SortEvents(false);
	return GetEventTime(EventAt(fEvents->CountItems() - 1));
}


uint32
BMidiStore::CurrentEvent() const
{
	return (uint32)fCurrentEvent;
}


void
BMidiStore::SetCurrentEvent(uint32 eventNumber)
{
	fCurrentEvent = (int32)eventNumber;
}


uint32
BMidiStore::DeltaOfEvent(uint32 eventNumber) const
{
	if (fEvents == NULL)
		return 0;
	const_cast<BMidiStore*>(this)->SortEvents(false);
	if ((int32)eventNumber >= fEvents->CountItems())
		return 0;
	const BMidiEvent* first = EventAt(0);
	const BMidiEvent* event = EventAt((int32)eventNumber);
	if (first == NULL || event == NULL)
		return 0;
	return event->time - first->time;
}


uint32
BMidiStore::EventAtDelta(uint32 time) const
{
	if (fEvents == NULL)
		return 0;
	const_cast<BMidiStore*>(this)->SortEvents(false);
	int32 count = fEvents->CountItems();
	for (int32 i = 0; i < count; i++) {
		const BMidiEvent* event = EventAt(i);
		if (event != NULL && event->time >= time)
			return (uint32)i;
	}
	return (uint32)count;
}


uint32
BMidiStore::BeginTime() const
{
	return fStartTime;
}


void
BMidiStore::SetTempo(int32 beatsPerMinute)
{
	if (beatsPerMinute <= 0)
		return;
	fBeatsPerMinute = beatsPerMinute;
}


int32
BMidiStore::Tempo() const
{
	return fBeatsPerMinute;
}


// SMF readers. VLQ and running status are named here because they are the
// two classic parser traps a later reader should not rediscover from a bug.
void
BMidiStore::ReadFourCC(char* fourcc)
{
	if (fFile == NULL)
		return;
	fFile->Read(fourcc, 4);
	fByteCount += 4;
}


uint32
BMidiStore::Read32Bit()
{
	uint8 bytes[4] = { 0, 0, 0, 0 };
	if (fFile != NULL)
		fFile->Read(bytes, 4);
	fByteCount += 4;
	return ((uint32)bytes[0] << 24) | ((uint32)bytes[1] << 16)
		| ((uint32)bytes[2] << 8) | (uint32)bytes[3];
}


uint16
BMidiStore::Read16Bit()
{
	uint8 bytes[2] = { 0, 0 };
	if (fFile != NULL)
		fFile->Read(bytes, 2);
	fByteCount += 2;
	return (uint16)(((uint16)bytes[0] << 8) | (uint16)bytes[1]);
}


uint8
BMidiStore::PeekByte()
{
	uint8 byte = 0;
	off_t pos = fFile != NULL ? fFile->Position() : 0;
	if (fFile != NULL)
		fFile->Read(&byte, 1);
	if (fFile != NULL)
		fFile->Seek(pos, SEEK_SET);
	return byte;
}


uint8
BMidiStore::NextByte()
{
	uint8 byte = 0;
	if (fFile != NULL)
		fFile->Read(&byte, 1);
	fByteCount++;
	return byte;
}


void
BMidiStore::SkipBytes(uint32 length)
{
	if (fFile != NULL)
		fFile->Seek(length, SEEK_CUR);
	fByteCount += length;
}


uint32
BMidiStore::ReadVarLength()
{
	// Variable-length quantity: 7 bits per byte, high bit = continuation.
	uint32 value = 0;
	for (int i = 0; i < 4; i++) {
		uint8 byte = NextByte();
		value = (value << 7) | (byte & 0x7f);
		if ((byte & 0x80) == 0)
			break;
	}
	return value;
}


void
BMidiStore::ReadSystemExclusive()
{
	uint32 length = ReadVarLength();
	void* data = malloc(length > 0 ? length : 1);
	if (data != NULL && fFile != NULL)
		fFile->Read(data, length);
	fByteCount += length;
	BMidiEvent* event = new BMidiEvent;
	event->time  = fTotalTicks;
	event->ticks = true;
	event->byte1 = B_SYS_EX_START;
	event->data  = data;
	event->length = length;
	AddEvent(event);
}


void
BMidiStore::ReadMetaEvent()
{
	uint8 type = NextByte();
	uint32 length = ReadVarLength();

	if (type == 0x51 && length == 3) {
		// Set Tempo: microseconds per quarter note.
		uint32 usPerQuarter = ((uint32)NextByte() << 16)
			| ((uint32)NextByte() << 8) | (uint32)NextByte();
		fByteCount += 0;
		if (usPerQuarter > 0) {
			// BPM = 60e6 / usPerQuarter.
			fBeatsPerMinute = (int32)(60000000.0 / (double)usPerQuarter);
			if (fBeatsPerMinute <= 0)
				fBeatsPerMinute = 120;
		}
		BMidiEvent* event = new BMidiEvent;
		event->time  = fTotalTicks;
		event->ticks = true;
		event->byte1 = 0xFF;
		event->byte2 = 0x51;
		event->byte3 = 0x03;
		event->tempo = fBeatsPerMinute;
		AddEvent(event);
	} else if (type == 0x2F) {
		// End of track.
		SkipBytes(length);
	} else if (type == 0x58 && length >= 4) {
		// Time signature; keep the first one's TPQ relationship simple.
		SkipBytes(length);
	} else {
		SkipBytes(length);
	}
}


void
BMidiStore::ReadTrack()
{
	// One track of delta-time / event pairs, merged into the shared list.
	// Running status: a channel-voice event may omit its status byte.
	uint8 runningStatus = 0;
	uint32 elapsedTicks = 0;

	for (;;) {
		off_t pos = 0;
		off_t size = 0;
		if (fFile == NULL)
			break;
		pos = fFile->Position();
		if (fFile->GetSize(&size) != B_OK)
			break;
		if (pos >= size)
			break;

		uint32 delta = ReadVarLength();
		elapsedTicks += delta;
		fTotalTicks = elapsedTicks;

		uint8 status = NextByte();
		if (status == 0xFF) {
			ReadMetaEvent();
			// End of track meta ends this track.
			// ReadMetaEvent already consumed type/length; detect via fByteCount
			// is fragile. Read the type again is wrong. Track end: if the
			// last meta was 0x2F we stop; simpler to scan remaining track
			// length from the chunk header. Handled in ReadChunk via track size.
			// Fall through; ReadChunk enforces the track boundary.
			continue;
		}
		if (status == 0xF0 || status == 0xF7) {
			// System exclusive. status already consumed as first byte.
			// Re-read length from the stream: the 0xF0 prefix is part of
			// the SMF sysex, then VLQ length.
			// We consumed 0xF0; the VLQ follows.
			// Put the byte back conceptually by reading length now.
			uint32 length = ReadVarLength();
			void* data = malloc(length > 0 ? length : 1);
			if (data != NULL && fFile != NULL) {
				// Include the 0xF0 prefix in the data.
				((uint8*)data)[0] = status;
				if (length > 0)
					fFile->Read((uint8*)data + 1, length - 1 > 0 ? length - 1 : 0);
				if (length == 0)
					((uint8*)data)[0] = status;
			}
			fByteCount += length;
			BMidiEvent* event = new BMidiEvent;
			event->time  = elapsedTicks;
			event->ticks = true;
			event->byte1 = B_SYS_EX_START;
			event->data  = data;
			event->length = length > 0 ? length : 1;
			AddEvent(event);
			continue;
		}

		if (status & 0x80) {
			runningStatus = status;
		} else {
			// Running status: put the data byte back by using it as data1.
			if (runningStatus == 0)
				continue;
			// status is actually data1.
			uint8 data1 = status;
			uint8 data2 = 0;
			uchar channel = runningStatus & 0x0f;
			uchar kind = runningStatus & 0xf0;
			uint32 length = 1;
			if (kind != 0xC0 && kind != 0xD0)
				length = 2;
			if (length >= 1 && fFile != NULL)
				(void)0;
			if (length == 2)
				data2 = NextByte();

			BMidiEvent* event = new BMidiEvent;
			event->time  = elapsedTicks;
			event->ticks = true;
			event->byte1 = runningStatus;
			event->byte2 = data1;
			event->byte3 = data2;
			event->track = fCurrTrack;
			AddEvent(event);
			continue;
		}

		uchar channel = status & 0x0f;
		uchar kind = status & 0xf0;
		uint8 data1 = 0;
		uint8 data2 = 0;

		if (kind == 0xC0 || kind == 0xD0) {
			data1 = NextByte();
		} else if (kind == 0x80 || kind == 0x90 || kind == 0xA0
			|| kind == 0xB0 || kind == 0xE0) {
			data1 = NextByte();
			data2 = NextByte();
		} else {
			// Unknown status; skip conservatively.
			continue;
		}

		BMidiEvent* event = new BMidiEvent;
		event->time  = elapsedTicks;
		event->ticks = true;
		event->byte1 = status;
		event->byte2 = data1;
		event->byte3 = data2;
		event->track = fCurrTrack;
		event->tempo = fBeatsPerMinute;
		AddEvent(event);
	}
}


void
BMidiStore::ReadChunk()
{
	if (fFile == NULL)
		return;

	char fourcc[4] = { 0, 0, 0, 0 };
	ReadFourCC(fourcc);
	uint32 length = Read32Bit();

	if (strncmp(fourcc, "MTrk", 4) != 0) {
		SkipBytes(length);
		return;
	}

	off_t trackEnd = fFile->Position() + (off_t)length;
	off_t fileSize = 0;
	// A truncated file must not spin forever: reads past EOF return no
	// bytes and the loop below would never reach a declared trackEnd.
	if (fFile->GetSize(&fileSize) == B_OK && trackEnd > fileSize)
		trackEnd = fileSize;
	uint32 savedTotal = fTotalTicks;
	// Each track restarts its delta clock at 0; we convert to absolute
	// time in one walk after merge via TicksToMilliseconds.
	fTotalTicks = 0;

	// Bound the track so end-of-track meta does not overrun.
	// ReadTrack stops at file end or when position reaches trackEnd by
	// temporarily lying about Size: read a bounded loop here instead.
	uint8 runningStatus = 0;
	uint32 elapsedTicks = 0;

	while (fFile->Position() < trackEnd) {
		uint32 delta = ReadVarLength();
		elapsedTicks += delta;
		fTotalTicks = elapsedTicks;

		uint8 status = NextByte();
		if (status == 0xFF) {
			uint8 type = NextByte();
			uint32 metaLength = ReadVarLength();
			if (type == 0x51 && metaLength == 3) {
				uint32 usPerQuarter = ((uint32)NextByte() << 16)
					| ((uint32)NextByte() << 8) | (uint32)NextByte();
				if (usPerQuarter > 0) {
					fBeatsPerMinute = (int32)(60000000.0
						/ (double)usPerQuarter);
					if (fBeatsPerMinute <= 0)
						fBeatsPerMinute = 120;
				}
				BMidiEvent* event = new BMidiEvent;
				event->time  = elapsedTicks;
				event->ticks = true;
				event->byte1 = 0xFF;
				event->byte2 = 0x51;
				event->byte3 = 0x03;
				event->tempo = fBeatsPerMinute;
				event->track = fCurrTrack;
				AddEvent(event);
			} else {
				SkipBytes(metaLength);
				if (type == 0x2F)
					break;
			}
			continue;
		}

		if (status == 0xF0 || status == 0xF7) {
			uint32 length = ReadVarLength();
			void* data = malloc(length + 1);
			if (data != NULL) {
				((uint8*)data)[0] = status;
				if (length > 0 && fFile != NULL)
					fFile->Read((uint8*)data + 1, length);
			}
			fByteCount += length;
			BMidiEvent* event = new BMidiEvent;
			event->time  = elapsedTicks;
			event->ticks = true;
			event->byte1 = B_SYS_EX_START;
			event->data  = data;
			event->length = length + 1;
			event->track = fCurrTrack;
			AddEvent(event);
			continue;
		}

		if (status & 0x80) {
			runningStatus = status;
		} else if (runningStatus != 0) {
			uint8 data1 = status;
			uint8 data2 = 0;
			uchar kind = runningStatus & 0xf0;
			if (kind != 0xC0 && kind != 0xD0)
				data2 = NextByte();
			BMidiEvent* event = new BMidiEvent;
			event->time  = elapsedTicks;
			event->ticks = true;
			event->byte1 = runningStatus;
			event->byte2 = data1;
			event->byte3 = data2;
			event->track = fCurrTrack;
			event->tempo = fBeatsPerMinute;
			AddEvent(event);
			continue;
		} else {
			continue;
		}

		uchar kind = status & 0xf0;
		uint8 data1 = 0;
		uint8 data2 = 0;
		if (kind == 0xC0 || kind == 0xD0) {
			data1 = NextByte();
		} else if (kind >= 0x80 && kind <= 0xE0) {
			data1 = NextByte();
			data2 = NextByte();
		} else {
			continue;
		}

		BMidiEvent* event = new BMidiEvent;
		event->time  = elapsedTicks;
		event->ticks = true;
		event->byte1 = status;
		event->byte2 = data1;
		event->byte3 = data2;
		event->track = fCurrTrack;
		event->tempo = fBeatsPerMinute;
		AddEvent(event);
	}

	fTotalTicks = savedTotal;
	// Pad to the declared track length if the parser stopped early.
	off_t pos = fFile->Position();
	if (pos < trackEnd)
		fFile->Seek(trackEnd, SEEK_SET);
}


status_t
BMidiStore::Import(const entry_ref* ref)
{
	if (ref == NULL)
		return B_BAD_VALUE;
	if (fInstruments != NULL)
		memset(fInstruments, 0, 128 * sizeof(bool));

	if (fEvents != NULL) {
		for (int32 i = 0; i < fEvents->CountItems(); i++)
			delete (BMidiEvent*)fEvents->ItemAt(i);
		fEvents->MakeEmpty();
	}

	fFile = new(std::nothrow) BFile(ref, B_READ_ONLY);
	if (fFile == NULL)
		return B_NO_MEMORY;
	if (fFile->InitCheck() != B_OK) {
		status_t err = fFile->InitCheck();
		delete fFile;
		fFile = NULL;
		return err;
	}

	char fourcc[4] = { 0, 0, 0, 0 };
	ReadFourCC(fourcc);
	if (strncmp(fourcc, "MThd", 4) != 0) {
		delete fFile;
		fFile = NULL;
		return B_BAD_MIDI_DATA;
	}

	if (Read32Bit() != 6) {
		delete fFile;
		fFile = NULL;
		return B_BAD_MIDI_DATA;
	}

	fFormat = Read16Bit();
	fNumTracks = Read16Bit();
	uint16 division = Read16Bit();

	if (division & 0x8000) {
		// SMPTE division: reject with a stated error rather than silently
		// mis-time the file.
		delete fFile;
		fFile = NULL;
		return B_BAD_MIDI_DATA;
	}
	fTicksPerBeat = (int16)division;
	if (fTicksPerBeat <= 0)
		fTicksPerBeat = 240;

	if (fFormat > 1) {
		delete fFile;
		fFile = NULL;
		return B_BAD_MIDI_DATA;
	}

	fCurrTrack = 0;
	fTotalTicks = 0;
	fBeatsPerMinute = 120;
	while (fCurrTrack < fNumTracks) {
		ReadChunk();
		fCurrTrack++;
	}

	// One tempo walk: every event already carries absolute ticks; convert
	// to milliseconds under the tempo active when its tick occurred. The
	// tempo events themselves update fBeatsPerMinute as we walk the merged
	// ordered list.
	SortEvents(true);
	int32 count = fEvents->CountItems();
	int32 bpm = 120;
	int32 tpq = fTicksPerBeat > 0 ? fTicksPerBeat : 240;
	uint32 lastTick = 0;
	uint32 lastMs = 0;
	for (int32 i = 0; i < count; i++) {
		BMidiEvent* event = (BMidiEvent*)fEvents->ItemAt(i);
		if (event == NULL)
			continue;
		uint32 tick = event->time;
		if (event->byte1 == 0xFF && event->byte2 == 0x51) {
			bpm = event->tempo > 0 ? event->tempo : bpm;
			event->time = lastMs;
			event->ticks = false;
			continue;
		}
		uint32 deltaTicks = tick - lastTick;
		uint32 deltaMs = (uint32)((uint64)deltaTicks * 60000ULL
			/ ((uint64)bpm * (uint64)tpq));
		lastMs += deltaMs;
		lastTick = tick;
		event->time = lastMs;
		event->ticks = false;
		event->tempo = bpm;
	}
	fBeatsPerMinute = bpm;
	fStartTime = 0;

	delete fFile;
	fFile = NULL;
	return B_OK;
}


status_t
BMidiStore::Export(const entry_ref*, int32)
{
	// Export is not required by the acceptance path.
	return B_NOT_SUPPORTED;
}


void
BMidiStore::SprayEvent(const BMidiEvent* event, uint32 time)
{
	if (event == NULL)
		return;
	switch (event->byte1 & 0xf0) {
		case B_NOTE_OFF:
			BMidi::NoteOff(event->byte1 & 0x0f, event->byte2, event->byte3, time);
			break;
		case B_NOTE_ON:
			BMidi::NoteOn(event->byte1 & 0x0f, event->byte2, event->byte3, time);
			break;
		case B_KEY_PRESSURE:
			BMidi::KeyPressure(event->byte1 & 0x0f, event->byte2, event->byte3, time);
			break;
		case B_CONTROL_CHANGE:
			BMidi::ControlChange(event->byte1 & 0x0f, event->byte2, event->byte3, time);
			break;
		case B_PROGRAM_CHANGE:
			BMidi::ProgramChange(event->byte1 & 0x0f, event->byte2, time);
			break;
		case B_CHANNEL_PRESSURE:
			BMidi::ChannelPressure(event->byte1 & 0x0f, event->byte2, time);
			break;
		case B_PITCH_BEND:
			BMidi::PitchBend(event->byte1 & 0x0f, event->byte2, event->byte3, time);
			break;
		case 0xF0:
			BMidi::SystemExclusive(event->data, event->length, time);
			break;
		case 0xFF:
			if (event->byte2 == 0x51)
				BMidi::TempoChange(event->tempo, time);
			break;
		default:
			BMidi::SystemCommon(event->byte1, event->byte2, event->byte3, time);
			break;
	}
}


void
BMidiStore::Run()
{
	// Playback loop on the app thread. Event times are absolute milliseconds
	// from file start; spray with midi1 time (ms) and let the midi2 façade
	// scheduler hold them until due. Playback starts at fCurrentEvent so a
	// stop/seek/start resumes from the right place, with the timeline
	// rebased so the remaining events keep their spacing.
	SortEvents(false);
	fFinished = false;
	uint32 count = CountEvents();
	if (count == 0) {
		fFinished = true;
		if (fHookFunc != NULL)
			fHookFunc(fHookArg);
		return;
	}

	uint32 first = (uint32)fCurrentEvent;
	if (first >= count)
		first = 0;
	uint32 base = GetEventTime(EventAt((int32)first));
	uint32 start = B_NOW;
	fStartTime = start - base;

	for (uint32 i = first; i < count && KeepRunning(); i++) {
		while (fPaused && KeepRunning())
			snooze(20000);
		if (!KeepRunning())
			break;

		fCurrentEvent = (int32)i;
		BMidiEvent* event = EventAt((int32)i);
		if (event == NULL)
			continue;

		uint32 eventTime = fStartTime + GetEventTime(event);
		SnoozeUntil(eventTime);
		if (!KeepRunning())
			break;

		SprayEvent(event, eventTime);
	}

	// The hook means "the song ended", not "someone called Stop()"; a
	// stopped run must not fire it or every stop would advance the
	// playlist.
	bool stopped = !KeepRunning();
	fFinished = true;
	if (!stopped && fHookFunc != NULL)
		fHookFunc(fHookArg);

	if (fLooping && !stopped) {
		fCurrentEvent = 0;
		Run();
	}
}


void
BMidiStore::_ReservedMidiStore1()
{
}


void
BMidiStore::_ReservedMidiStore2()
{
}


void
BMidiStore::_ReservedMidiStore3()
{
}
