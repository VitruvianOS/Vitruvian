/*
 * Copyright 2015-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_CONNECTION_H
#define _MEDIA2_MEDIA_CONNECTION_H


#include <atomic>

#include <String.h>

#include <media2/MediaClientDefs.h>
#include <media2/MediaFormat.h>


class BMediaClient;
class BLocker;
class BMallocIO;
class BMediaGraph;
class BMediaUnit;

class BMediaConnection {
public:
			media_connection_id		Id() const;
			const char*				Name() const;
			BMediaClient*			Client() const;

			bool					IsConnected() const;
			bool					HasBinding() const;
			BMediaConnection*		Binding() const;

			const BMediaFormat&		Format() const;
			status_t				SetAcceptedFormat(const BMediaFormat& format);

			media_type_mask			AcceptedTypes() const;
			void					SetAcceptedTypes(media_type_mask types);

			status_t				Disconnect();
			status_t				Release();

			size_t					BufferSize() const;
			void					SetBufferSize(size_t size);

			void					SetPreferredBufferSize(size_t size);

protected:
									BMediaConnection(media_connection_kinds kinds,
										const char* name = NULL);
	virtual							~BMediaConnection();

	virtual	void					Connected(const BMediaFormat& format);
	virtual	void					Disconnected();

	virtual	void					BufferSizeChanged(size_t newSize);

private:
			media_connection_id		fId;
			BString					fName;
			media_connection_kinds	fKinds;
			BMediaClient*			fOwner;
			BMediaConnection*		fBinding;
			BMediaFormat			fFormat;
			BMediaFormat			fAcceptedFormat;
			media_type_mask			fAcceptedTypes;
			size_t					fBufferSize;
			bool					fConnected;

			void*					fStream;
			void*					fFilterPort;

			void*					_GetStream() const { return fStream; }
			void*					_GetFilterPort() const { return fFilterPort; }
			uint32					_GetNodeId() const;
			uint32					_GetPortId() const;
			void					_SetFilterPort(void* port) { fFilterPort = port; }

			uint32					_GetPortIds(uint32* outIds,
										uint32 maxCount) const;

	friend class BMediaClient;
	friend class BMediaNode;
	friend class BMediaUnit;
	friend class BMidiUnit;
	friend class BSimpleMediaNode;
	friend class BMediaGraph;
};


class BMediaInput : public virtual BMediaConnection {
public:
									BMediaInput(const char* name = NULL);
	virtual							~BMediaInput();

			size_t					BufferSize() const;

protected:
	virtual	status_t				AcceptFormat(BMediaFormat* format) = 0;
	virtual	void					HandleBuffer(void* buffer, size_t size,
										const BMediaFormat& format);
	virtual	void					Connected(const BMediaFormat& format) override;
	virtual	void					Disconnected() override;

	friend class BMediaClient;
	friend class BMediaUnit;
};


class BMediaOutput : public virtual BMediaConnection {
public:
									BMediaOutput(const char* name = NULL);
	virtual							~BMediaOutput();

	virtual	status_t				SendBuffer(void* buffer, size_t size);

			size_t					BufferSize() const;

protected:
	virtual	status_t				PrepareToConnect(BMediaFormat* format) = 0;
	virtual	status_t				FormatProposal(BMediaFormat* format) = 0;
	virtual	void					Connected(const BMediaFormat& format) override;
	virtual	void					Disconnected() override;

	virtual	size_t					FillBuffer(void* buffer, size_t maxSize);

private:
			bool					fEnabled;
			uint64					fFramesSent;

	friend class BMediaClient;
	friend class BMediaUnit;
};


enum midi_event_type {
	B_MIDI_NOTE_OFF = 1,
	B_MIDI_NOTE_ON,
	B_MIDI_KEY_PRESSURE,
	B_MIDI_CONTROL_CHANGE,
	B_MIDI_PROGRAM_CHANGE,
	B_MIDI_CHANNEL_PRESSURE,
	B_MIDI_PITCH_BEND,
	B_MIDI_SYSTEM_EXCLUSIVE,
	B_MIDI_SYSTEM_COMMON,
	B_MIDI_SYSTEM_REAL_TIME
};


class BMidiInput : public virtual BMediaInput {
public:
									BMidiInput(const char* name = NULL);
	virtual							~BMidiInput();

protected:
	virtual	status_t				AcceptFormat(BMediaFormat* format) override;

	virtual	void					HandleBuffer(void* buffer, size_t size,
										const BMediaFormat& format) override;
	virtual	void					Connected(const BMediaFormat& format) override;
	virtual	void					Disconnected() override;

	virtual	void					NoteOff(uchar channel, uchar note,
										uchar velocity, bigtime_t time);
	virtual	void					NoteOn(uchar channel, uchar note,
										uchar velocity, bigtime_t time);
	virtual	void					KeyPressure(uchar channel, uchar note,
										uchar pressure, bigtime_t time);
	virtual	void					ControlChange(uchar channel,
										uchar controlNumber, uchar controlValue,
										bigtime_t time);
	virtual	void					ProgramChange(uchar channel,
										uchar programNumber, bigtime_t time);
	virtual	void					ChannelPressure(uchar channel,
										uchar pressure, bigtime_t time);
	virtual	void					PitchBend(uchar channel, uchar lsb,
										uchar msb, bigtime_t time);
	virtual	void					SystemExclusive(void* data, size_t length,
										bigtime_t time);
	virtual	void					SystemCommon(uchar status, uchar data1,
										uchar data2, bigtime_t time);
	virtual	void					SystemRealTime(uchar status, bigtime_t time);

private:
			void					_ProcessByte(uint8 byte, bigtime_t time);
			void					_Dispatch(uint8 status, bigtime_t time);

			uint8					fRunningStatus;
			uint8					fPendingData[2];
			uint8					fPendingCount;
			bool					fInSystemExclusive;
			BMallocIO*				fSysExBuffer;
};


class BMidiOutput : public virtual BMediaOutput {
public:
									BMidiOutput(const char* name = NULL);
	virtual							~BMidiOutput();

			status_t				SendNoteOff(uchar channel, uchar note,
										uchar velocity, bigtime_t time = 0);
			status_t				SendNoteOn(uchar channel, uchar note,
										uchar velocity, bigtime_t time = 0);
			status_t				SendKeyPressure(uchar channel, uchar note,
										uchar pressure, bigtime_t time = 0);
			status_t				SendControlChange(uchar channel,
										uchar controlNumber, uchar controlValue,
										bigtime_t time = 0);
			status_t				SendProgramChange(uchar channel,
										uchar programNumber, bigtime_t time = 0);
			status_t				SendChannelPressure(uchar channel,
										uchar pressure, bigtime_t time = 0);
			status_t				SendPitchBend(uchar channel, uchar lsb,
										uchar msb, bigtime_t time = 0);
			status_t				SendSystemExclusive(const void* data,
										size_t length, bigtime_t time = 0);
			status_t				SendSystemCommon(uchar status, uchar data1,
										uchar data2, bigtime_t time = 0);
			status_t				SendSystemRealTime(uchar status,
										bigtime_t time = 0);

			status_t				AllNotesOff(bigtime_t time = 0);

			status_t				ChaseState(bigtime_t time = 0);

	virtual	status_t				SendBuffer(void* buffer, size_t size) override;

protected:
	virtual	status_t				PrepareToConnect(BMediaFormat* format) override;
	virtual	status_t				FormatProposal(BMediaFormat* format) override;
	virtual	void					Connected(const BMediaFormat& format) override;
	virtual	void					Disconnected() override;

	virtual	size_t					FillBuffer(void* buffer, size_t maxSize) override;

private:
			status_t				_QueueEvent(const uint8* bytes, size_t length,
									bigtime_t time);

			struct queued_event {
				bigtime_t		time;
				uint8			data[3];
				size_t			length;
			};

			struct alignas(64) ring_index {
				std::atomic<size_t>	value { 0 };
			};

			static const size_t		kEventRingCapacity = 256;

			queued_event			fEventSlots[kEventRingCapacity];
			ring_index				fEventWriteIndex;
			ring_index				fEventReadIndex;
			std::atomic<uint64>		fEventsDropped;

			bool					_PushEvent(const queued_event& event);
			bool					_PopEvent(queued_event* event);

			static const size_t		kSysExRingCapacity = 8192;

			uint8					fSysExBytes[kSysExRingCapacity];
			ring_index				fSysExWriteIndex;
			ring_index				fSysExReadIndex;
			std::atomic<uint64>		fSysExDropped;

			size_t					_PushSysEx(const uint8* data, size_t length);
			size_t					_PopSysEx(uint8* buffer, size_t maxLength);

			BLocker*				fQueueLock;

			bool					fNotesOn[16][128];
			int16					fLastProgram[16];
			uint8					fLastController[16][128];
			bool					fControllerSet[16][128];
};


#endif
