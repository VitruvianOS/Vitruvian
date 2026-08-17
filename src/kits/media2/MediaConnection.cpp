/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MediaConnection.h>
#include <media2/MediaUnit.h>

#include <atomic>
#include <new>
#include <string.h>

#include <Autolock.h>
#include <DataIO.h>
#include <Locker.h>
#include <OS.h>

#include <spa/pod/pod.h>
#include <spa/pod/iter.h>
#include <spa/pod/builder.h>
#include <spa/control/control.h>

#include <pipewire/stream.h>
#include <pipewire/filter.h>
#include <pipewire/port.h>
#include <pipewire/proxy.h>
#include <pipewire/core.h>

#include "PipeWireBackend.h"

using namespace BPrivate::media;


static std::atomic<media_connection_id> sNextId{1};


enum {
	kStatusNoteOff         = 0x80,
	kStatusNoteOn          = 0x90,
	kStatusKeyPressure     = 0xa0,
	kStatusControlChange   = 0xb0,
	kStatusProgramChange   = 0xc0,
	kStatusChannelPressure = 0xd0,
	kStatusPitchBend       = 0xe0,
	kStatusSystemExclusive = 0xf0,
	kStatusSystemCommonMin = 0xf1,
	kStatusSystemCommonMax = 0xf6,
	kStatusSystemRealMin   = 0xf8
};


static uint8
data_bytes_for(uint8 status)
{
	switch (status & 0xf0) {
		case kStatusNoteOff:
		case kStatusNoteOn:
		case kStatusKeyPressure:
		case kStatusControlChange:
		case kStatusPitchBend:
			return 2;
		case kStatusProgramChange:
		case kStatusChannelPressure:
			return 1;
	}
	switch (status) {
		case 0xf1: return 1;
		case 0xf2: return 2;
		case 0xf3: return 1;
		default:   return 0;
	}
}


// #pragma mark - BMediaConnection


BMediaConnection::BMediaConnection(media_connection_kinds kinds, const char* name)
	:
	fId(sNextId.fetch_add(1)),
	fName(name != NULL ? name : ""),
	fKinds(kinds),
	fOwner(NULL),
	fBinding(NULL),
	fAcceptedTypes(B_MEDIA_ANY_TYPE),
	fBufferSize(0),
	fConnected(false)
{
}


BMediaConnection::~BMediaConnection()
{
}


media_connection_id
BMediaConnection::Id() const
{
	return fId;
}


const char*
BMediaConnection::Name() const
{
	return fName.String();
}


BMediaClient*
BMediaConnection::Client() const
{
	return fOwner;
}


bool
BMediaConnection::IsConnected() const
{
	return fConnected;
}


bool
BMediaConnection::HasBinding() const
{
	return fBinding != NULL;
}


BMediaConnection*
BMediaConnection::Binding() const
{
	return fBinding;
}


const BMediaFormat&
BMediaConnection::Format() const
{
	return fFormat;
}


status_t
BMediaConnection::SetAcceptedFormat(const BMediaFormat& format)
{
	fAcceptedFormat = format;
	return B_OK;
}


media_type_mask
BMediaConnection::AcceptedTypes() const
{
	return fAcceptedTypes;
}


void
BMediaConnection::SetAcceptedTypes(media_type_mask types)
{
	fAcceptedTypes = types;
}


status_t
BMediaConnection::Disconnect()
{
	// Filled in when BMediaClient/BMediaGraph land.
	return B_NOT_SUPPORTED;
}


uint32
BMediaConnection::_GetNodeId() const
{
	if (fStream != NULL) {
		pw_stream* stream = (pw_stream*)fStream;
		return pw_stream_get_node_id(stream);
	}
	if (fFilterPort != NULL) {
		BMediaClient* client = Client();
		if (client == NULL)
			return 0;

		class BMediaUnit* unit = dynamic_cast<class BMediaUnit*>(client);
		if (unit == NULL)
			return 0;

		void* filter = unit->_GetFilter();
		if (filter == NULL)
			return 0;

		pw_filter* filterPtr = (pw_filter*)filter;
		return pw_filter_get_node_id(filterPtr);
	}
	return 0;
}


uint32
BMediaConnection::_GetPortId() const
{
	uint32 first = 0;
	if (_GetPortIds(&first, 1) == 0)
		return 0;
	return first;
}


uint32
BMediaConnection::_GetPortIds(uint32* outIds, uint32 maxCount) const
{
	if (fStream != NULL) {
		PipeWireBackend* backend = PipeWireBackend::GetInstance();
		if (backend == NULL)
			return 0;

		uint32 nodeId = _GetNodeId();
		if (nodeId == 0)
			return 0;

		pw_direction dir = (dynamic_cast<const BMediaOutput*>(this) != NULL)
			? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT;

		std::vector<uint32> ports;
		if (backend->ResolveNodePorts(nodeId, dir, ports) != B_OK)
			return 0;

		for (uint32 i = 0; i < ports.size() && i < maxCount; i++)
			outIds[i] = ports[i];
		return (uint32)ports.size();
	}

	if (fFilterPort != NULL) {
		pw_proxy* portProxy = (pw_proxy*)fFilterPort;
		uint32 id = pw_proxy_get_bound_id(portProxy);
		if (id == 0)
			return 0;
		if (maxCount > 0)
			outIds[0] = id;
		return 1;
	}

	return 0;
}


status_t
BMediaConnection::Release()
{
	delete this;
	return B_OK;
}


size_t
BMediaConnection::BufferSize() const
{
	return fBufferSize;
}


void
BMediaConnection::SetPreferredBufferSize(size_t size)
{
	fBufferSize = size;
}


void
BMediaConnection::BufferSizeChanged(size_t newSize)
{
}


void
BMediaConnection::Connected(const BMediaFormat& format)
{
	fFormat = format;
	fConnected = true;
}


void
BMediaConnection::Disconnected()
{
	fConnected = false;
}


// #pragma mark - BMediaInput


BMediaInput::BMediaInput(const char* name)
	:
	BMediaConnection(B_MEDIA_INPUT, name)
{
}


BMediaInput::~BMediaInput()
{
}


void
BMediaInput::HandleBuffer(void* /*buffer*/, size_t /*size*/,
	const BMediaFormat& /*format*/)
{
}


void
BMediaInput::Connected(const BMediaFormat& format)
{
	BMediaConnection::Connected(format);
}


void
BMediaInput::Disconnected()
{
	BMediaConnection::Disconnected();
}


// #pragma mark - BMediaOutput


BMediaOutput::BMediaOutput(const char* name)
	:
	BMediaConnection(B_MEDIA_OUTPUT, name),
	fEnabled(true),
	fFramesSent(0)
{
}


BMediaOutput::~BMediaOutput()
{
}


status_t
BMediaOutput::SendBuffer(void* /*buffer*/, size_t /*size*/)
{
	// Filled in once pw_filter port handling lands.
	return B_NOT_SUPPORTED;
}


void
BMediaOutput::Connected(const BMediaFormat& format)
{
	BMediaConnection::Connected(format);
}


void
BMediaOutput::Disconnected()
{
	BMediaConnection::Disconnected();
}


size_t
BMediaOutput::FillBuffer(void*, size_t)
{
	return 0;
}


BMidiInput::BMidiInput(const char* name)
	:
	BMediaConnection(B_MEDIA_INPUT, name),
	BMediaInput(name),
	fRunningStatus(0),
	fPendingCount(0),
	fInSystemExclusive(false),
	fSysExBuffer(new(std::nothrow) BMallocIO())
{
	fPendingData[0] = fPendingData[1] = 0;
	SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_MIDI));
}


BMidiInput::~BMidiInput()
{
	delete fSysExBuffer;
}


status_t
BMidiInput::AcceptFormat(BMediaFormat* format)
{
	if (format == NULL)
		return B_BAD_VALUE;
	if (format->Type() != B_MEDIA_MIDI)
		return B_MEDIA_BAD_FORMAT;
	return B_OK;
}


void
BMidiInput::HandleBuffer(void* buffer, size_t size, const BMediaFormat&)
{
	if (buffer == NULL || size == 0)
		return;

	struct spa_pod_sequence* seq = (struct spa_pod_sequence*)buffer;
	if (seq->pod.type != SPA_TYPE_Sequence)
		return;

	BMediaClient* client = Client();
	bigtime_t cycleStartTime = 0;
	uint32 sampleRate = 48000;

	if (client != NULL) {
		class BMediaUnit* unit = dynamic_cast<class BMediaUnit*>(client);
		if (unit != NULL) {
			cycleStartTime = unit->CycleStartTime();
			sampleRate = unit->SampleRate();
			if (sampleRate == 0)
				sampleRate = 48000;
		}
	}

	struct spa_pod_control* control;
	SPA_POD_SEQUENCE_FOREACH(seq, control) {
		if (control->type != SPA_CONTROL_Midi)
			continue;

		struct spa_pod_bytes* bytes = (struct spa_pod_bytes*)&control->value;
		if (bytes->pod.type != SPA_TYPE_Bytes)
			continue;

		const uint8* data = (const uint8*)SPA_POD_BODY(bytes);
		size_t dataSize = bytes->pod.size;

		uint32 offsetSamples = control->offset;
		bigtime_t time;
		if (cycleStartTime > 0 && sampleRate > 0) {
			time = cycleStartTime + (offsetSamples * 1000000LL / sampleRate);
		} else {
			time = system_time();
		}

		for (size_t i = 0; i < dataSize; i++)
			_ProcessByte(data[i], time);
	}
}


void
BMidiInput::_ProcessByte(uint8 byte, bigtime_t time)
{
	if (fInSystemExclusive) {
		if (byte == 0xf7) {
			fInSystemExclusive = false;
			if (fSysExBuffer != NULL) {
				SystemExclusive((void*)fSysExBuffer->Buffer(),
					fSysExBuffer->BufferLength(), time);
				fSysExBuffer->SetSize(0);
			}
			return;
		}
		if (byte >= kStatusSystemRealMin) {
			SystemRealTime(byte, time);
			return;
		}
		if (byte < 0x80) {
			if (fSysExBuffer != NULL)
				fSysExBuffer->Write(&byte, 1);
			return;
		}
		fInSystemExclusive = false;
		if (fSysExBuffer != NULL)
			fSysExBuffer->SetSize(0);
	}

	if (byte >= kStatusSystemRealMin) {
		SystemRealTime(byte, time);
		return;
	}
	if (byte == kStatusSystemExclusive) {
		fInSystemExclusive = true;
		if (fSysExBuffer != NULL)
			fSysExBuffer->SetSize(0);
		return;
	}
	if (byte & 0x80) {
		fRunningStatus = byte;
		fPendingCount = 0;
		if (data_bytes_for(byte) == 0)
			_Dispatch(byte, time);
		return;
	}

	if (fRunningStatus == 0)
		return;
	uint8 needed = data_bytes_for(fRunningStatus);
	if (needed == 0 || fPendingCount >= 2)
		return;
	fPendingData[fPendingCount++] = byte;
	if (fPendingCount < needed)
		return;

	_Dispatch(fRunningStatus, time);
	fPendingCount = 0;
	if (fRunningStatus >= kStatusSystemCommonMin
			&& fRunningStatus <= kStatusSystemCommonMax) {
		fRunningStatus = 0;
	}
}


void
BMidiInput::_Dispatch(uint8 status, bigtime_t time)
{
	uchar chan = status & 0x0f;
	switch (status & 0xf0) {
		case kStatusNoteOff:
			NoteOff(chan, fPendingData[0], fPendingData[1], time);
			return;
		case kStatusNoteOn:
			if (fPendingData[1] == 0)
				NoteOff(chan, fPendingData[0], 0, time);
			else
				NoteOn(chan, fPendingData[0], fPendingData[1], time);
			return;
		case kStatusKeyPressure:
			KeyPressure(chan, fPendingData[0], fPendingData[1], time);
			return;
		case kStatusControlChange:
			ControlChange(chan, fPendingData[0], fPendingData[1], time);
			return;
		case kStatusProgramChange:
			ProgramChange(chan, fPendingData[0], time);
			return;
		case kStatusChannelPressure:
			ChannelPressure(chan, fPendingData[0], time);
			return;
		case kStatusPitchBend:
			PitchBend(chan, fPendingData[0], fPendingData[1], time);
			return;
	}
	if (status >= kStatusSystemCommonMin && status <= kStatusSystemCommonMax) {
		uchar data2 = (status == 0xf2) ? fPendingData[1] : 0;
		SystemCommon(status, fPendingData[0], data2, time);
	}
}


void
BMidiInput::Connected(const BMediaFormat& format)
{
	BMediaInput::Connected(format);
}


void
BMidiInput::Disconnected()
{
	BMediaInput::Disconnected();
}


void BMidiInput::NoteOff(uchar, uchar, uchar, bigtime_t) {}
void BMidiInput::NoteOn(uchar, uchar, uchar, bigtime_t) {}
void BMidiInput::KeyPressure(uchar, uchar, uchar, bigtime_t) {}
void BMidiInput::ControlChange(uchar, uchar, uchar, bigtime_t) {}
void BMidiInput::ProgramChange(uchar, uchar, bigtime_t) {}
void BMidiInput::ChannelPressure(uchar, uchar, bigtime_t) {}
void BMidiInput::PitchBend(uchar, uchar, uchar, bigtime_t) {}
void BMidiInput::SystemExclusive(void*, size_t, bigtime_t) {}
void BMidiInput::SystemCommon(uchar, uchar, uchar, bigtime_t) {}
void BMidiInput::SystemRealTime(uchar, bigtime_t) {}


BMidiOutput::BMidiOutput(const char* name)
	:
	BMediaConnection(B_MEDIA_OUTPUT, name),
	BMediaOutput(name),
	fEventsDropped(0),
	fSysExDropped(0),
	fQueueLock(new(std::nothrow) BLocker("midi output queue"))
{
	memset(fNotesOn, 0, sizeof(fNotesOn));
	memset(fControllerSet, 0, sizeof(fControllerSet));
	for (int i = 0; i < 16; i++)
		fLastProgram[i] = -1;
	SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_MIDI));
}


BMidiOutput::~BMidiOutput()
{
	delete fQueueLock;
}


status_t
BMidiOutput::PrepareToConnect(BMediaFormat* format)
{
	if (format == NULL)
		return B_BAD_VALUE;
	*format = BMediaFormat();
	format->format.type = B_MEDIA_MIDI;
	return B_OK;
}


status_t
BMidiOutput::FormatProposal(BMediaFormat* format)
{
	if (format == NULL)
		return B_BAD_VALUE;
	if (format->Type() != B_MEDIA_MIDI)
		return B_MEDIA_BAD_FORMAT;
	return B_OK;
}


void
BMidiOutput::Connected(const BMediaFormat& format)
{
	BMediaOutput::Connected(format);
}


void
BMidiOutput::Disconnected()
{
	BMediaOutput::Disconnected();
}


bool
BMidiOutput::_PushEvent(const queued_event& event)
{
	size_t writeIdx = fEventWriteIndex.value.load(std::memory_order_relaxed);
	size_t readIdx = fEventReadIndex.value.load(std::memory_order_acquire);
	if (writeIdx - readIdx >= kEventRingCapacity) {
		fEventsDropped.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	fEventSlots[writeIdx % kEventRingCapacity] = event;
	fEventWriteIndex.value.store(writeIdx + 1, std::memory_order_release);
	return true;
}


bool
BMidiOutput::_PopEvent(queued_event* event)
{
	size_t readIdx = fEventReadIndex.value.load(std::memory_order_relaxed);
	size_t writeIdx = fEventWriteIndex.value.load(std::memory_order_acquire);
	if (readIdx == writeIdx)
		return false;
	*event = fEventSlots[readIdx % kEventRingCapacity];
	fEventReadIndex.value.store(readIdx + 1, std::memory_order_release);
	return true;
}


size_t
BMidiOutput::_PushSysEx(const uint8* data, size_t length)
{
	if (length == 0 || length > kSysExRingCapacity)
		return 0;

	size_t writeIdx = fSysExWriteIndex.value.load(std::memory_order_relaxed);
	size_t readIdx = fSysExReadIndex.value.load(std::memory_order_acquire);
	if (kSysExRingCapacity - (writeIdx - readIdx) < length) {
		fSysExDropped.fetch_add(1, std::memory_order_relaxed);
		return 0;
	}

	for (size_t i = 0; i < length; i++)
		fSysExBytes[(writeIdx + i) % kSysExRingCapacity] = data[i];
	fSysExWriteIndex.value.store(writeIdx + length, std::memory_order_release);
	return length;
}


size_t
BMidiOutput::_PopSysEx(uint8* buffer, size_t maxLength)
{
	size_t readIdx = fSysExReadIndex.value.load(std::memory_order_relaxed);
	size_t writeIdx = fSysExWriteIndex.value.load(std::memory_order_acquire);
	size_t available = writeIdx - readIdx;
	size_t count = available < maxLength ? available : maxLength;

	for (size_t i = 0; i < count; i++)
		buffer[i] = fSysExBytes[(readIdx + i) % kSysExRingCapacity];
	if (count > 0)
		fSysExReadIndex.value.store(readIdx + count, std::memory_order_release);
	return count;
}


status_t
BMidiOutput::SendBuffer(void* buffer, size_t size)
{
	if (buffer == NULL || size == 0)
		return B_BAD_VALUE;
	if (fQueueLock == NULL)
		return B_NO_MEMORY;
	BAutolock _(fQueueLock);
	size_t written = _PushSysEx((const uint8*)buffer, size);
	return written == size ? B_OK : B_WOULD_BLOCK;
}


status_t
BMidiOutput::_QueueEvent(const uint8* bytes, size_t length, bigtime_t time)
{
	if (fQueueLock == NULL)
		return B_NO_MEMORY;
	if (bytes == NULL || length == 0 || length > 3)
		return B_BAD_VALUE;

	BAutolock _(fQueueLock);
	queued_event event;
	event.time = time == 0 ? system_time() : time;
	event.length = length;
	memcpy(event.data, bytes, length);

	return _PushEvent(event) ? B_OK : B_WOULD_BLOCK;
}


status_t
BMidiOutput::SendNoteOff(uchar channel, uchar note, uchar velocity, bigtime_t time)
{
	channel &= 0x0f;
	note &= 0x7f;
	if (fQueueLock != NULL) {
		BAutolock _(fQueueLock);
		fNotesOn[channel][note] = false;
	}
	uint8 msg[3] = { (uint8)(kStatusNoteOff | channel), note, velocity };
	return _QueueEvent(msg, sizeof(msg), time);
}


status_t
BMidiOutput::SendNoteOn(uchar channel, uchar note, uchar velocity, bigtime_t time)
{
	channel &= 0x0f;
	note &= 0x7f;
	if (fQueueLock != NULL) {
		BAutolock _(fQueueLock);
		fNotesOn[channel][note] = velocity > 0;
	}
	uint8 msg[3] = { (uint8)(kStatusNoteOn | channel), note, velocity };
	return _QueueEvent(msg, sizeof(msg), time);
}


status_t
BMidiOutput::SendKeyPressure(uchar channel, uchar note, uchar pressure, bigtime_t time)
{
	uint8 msg[3] = { (uint8)(kStatusKeyPressure | (channel & 0x0f)), note, pressure };
	return _QueueEvent(msg, sizeof(msg), time);
}


status_t
BMidiOutput::SendControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	channel &= 0x0f;
	controlNumber &= 0x7f;
	if (fQueueLock != NULL) {
		BAutolock _(fQueueLock);
		fLastController[channel][controlNumber] = controlValue;
		fControllerSet[channel][controlNumber] = true;
	}
	uint8 msg[3] = { (uint8)(kStatusControlChange | channel),
		controlNumber, controlValue };
	return _QueueEvent(msg, sizeof(msg), time);
}


status_t
BMidiOutput::SendProgramChange(uchar channel, uchar programNumber, bigtime_t time)
{
	channel &= 0x0f;
	if (fQueueLock != NULL) {
		BAutolock _(fQueueLock);
		fLastProgram[channel] = programNumber;
	}
	uint8 msg[2] = { (uint8)(kStatusProgramChange | channel), programNumber };
	return _QueueEvent(msg, sizeof(msg), time);
}


status_t
BMidiOutput::AllNotesOff(bigtime_t time)
{
	if (fQueueLock == NULL)
		return B_NO_MEMORY;
	status_t result = B_OK;
	for (int channel = 0; channel < 16; channel++) {
		for (int note = 0; note < 128; note++) {
			bool on;
			{
				BAutolock _(fQueueLock);
				on = fNotesOn[channel][note];
			}
			if (on) {
				status_t status = SendNoteOff(channel, note, 0, time);
				if (status != B_OK)
					result = status;
			}
		}
	}
	return result;
}


status_t
BMidiOutput::ChaseState(bigtime_t time)
{
	if (fQueueLock == NULL)
		return B_NO_MEMORY;
	status_t result = B_OK;
	for (int channel = 0; channel < 16; channel++) {
		int16 program;
		{
			BAutolock _(fQueueLock);
			program = fLastProgram[channel];
		}
		if (program >= 0) {
			status_t status = SendProgramChange(channel, (uchar)program, time);
			if (status != B_OK)
				result = status;
		}
		for (int control = 0; control < 128; control++) {
			bool set;
			uint8 value;
			{
				BAutolock _(fQueueLock);
				set = fControllerSet[channel][control];
				value = fLastController[channel][control];
			}
			if (set) {
				status_t status = SendControlChange(channel, control, value, time);
				if (status != B_OK)
					result = status;
			}
		}
	}
	return result;
}


status_t
BMidiOutput::SendChannelPressure(uchar channel, uchar pressure, bigtime_t time)
{
	uint8 msg[2] = { (uint8)(kStatusChannelPressure | (channel & 0x0f)), pressure };
	return _QueueEvent(msg, sizeof(msg), time);
}


status_t
BMidiOutput::SendPitchBend(uchar channel, uchar lsb, uchar msb, bigtime_t time)
{
	uint8 msg[3] = { (uint8)(kStatusPitchBend | (channel & 0x0f)), lsb, msb };
	return _QueueEvent(msg, sizeof(msg), time);
}


status_t
BMidiOutput::SendSystemExclusive(const void* data, size_t length, bigtime_t)
{
	if (data == NULL && length > 0)
		return B_BAD_VALUE;
	if (fQueueLock == NULL)
		return B_NO_MEMORY;
	if (length + 2 > kSysExRingCapacity)
		return B_BAD_VALUE;

	uint8 framed[kSysExRingCapacity];
	framed[0] = kStatusSystemExclusive;
	if (length > 0)
		memcpy(framed + 1, data, length);
	framed[length + 1] = 0xf7;

	BAutolock _(fQueueLock);
	size_t written = _PushSysEx(framed, length + 2);
	return written == length + 2 ? B_OK : B_WOULD_BLOCK;
}


status_t
BMidiOutput::SendSystemCommon(uchar status, uchar data1, uchar data2, bigtime_t time)
{
	uint8 needed = data_bytes_for(status);
	uint8 msg[3] = { status, data1, data2 };
	return _QueueEvent(msg, 1 + needed, time);
}


status_t
BMidiOutput::SendSystemRealTime(uchar status, bigtime_t time)
{
	uint8 msg[1] = { status };
	return _QueueEvent(msg, sizeof(msg), time);
}


size_t
BMidiOutput::FillBuffer(void* buffer, size_t maxSize)
{
	if (buffer == NULL || maxSize == 0)
		return 0;

	BMediaClient* client = Client();
	bigtime_t cycleStartTime = 0;
	uint32 sampleRate = 48000;

	if (client != NULL) {
		class BMediaUnit* unit = dynamic_cast<class BMediaUnit*>(client);
		if (unit != NULL) {
			cycleStartTime = unit->CycleStartTime();
			sampleRate = unit->SampleRate();
			if (sampleRate == 0)
				sampleRate = 48000;
		}
	}

	struct spa_pod_builder b;
	uint8 builderData[2048];
	spa_pod_builder_init(&b, builderData, sizeof(builderData));

	struct spa_pod_frame f;
	spa_pod_builder_push_sequence(&b, &f, 0);

	size_t written = 0;

	queued_event event;
	while (written < (maxSize - 128) && _PopEvent(&event)) {
		if (event.length == 0 || event.length > 3)
			continue;

		uint32 offset = 0;
		if (cycleStartTime > 0 && event.time >= cycleStartTime) {
			bigtime_t timeDiff = event.time - cycleStartTime;
			offset = (uint32)(timeDiff * sampleRate / 1000000LL);
		}

		if (spa_pod_builder_control(&b, offset, SPA_CONTROL_Midi) < 0)
			continue;
		if (spa_pod_builder_bytes(&b, event.data, event.length) < 0)
			continue;
		written += sizeof(struct spa_pod_control) + SPA_POD_BODY_SIZE(event.length);
	}

	uint8 sysExChunk[256];
	size_t sysExLength = _PopSysEx(sysExChunk, sizeof(sysExChunk));
	if (sysExLength > 0 && written < (maxSize - 128)) {
		if (spa_pod_builder_control(&b, 0, SPA_CONTROL_Midi) >= 0
				&& spa_pod_builder_bytes(&b, sysExChunk, sysExLength) >= 0) {
			written += sizeof(struct spa_pod_control) + SPA_POD_BODY_SIZE(sysExLength);
		}
	}

	spa_pod_builder_pop(&b, &f);
	struct spa_pod* seq = (struct spa_pod*)b.data;
	if (seq == NULL)
		return 0;

	size_t seqSize = SPA_POD_SIZE(seq);
	if (seqSize > maxSize) {
		memset(buffer, 0, maxSize);
		return 0;
	}

	memcpy(buffer, seq, seqSize);

	return seqSize;
}
