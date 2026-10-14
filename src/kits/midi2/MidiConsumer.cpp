/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi2/MidiConsumer.h>
#include <midi2/MidiProducer.h>
#include <midi2/MidiRoster.h>

#include <new>
#include <utility>
#include <vector>
#include <string.h>

#include <Autolock.h>
#include <Locker.h>

#include <media2/MediaConnection.h>
#include <media2/MediaUnit.h>
#include <media2/MidiUnit.h>


// Queues RT-thread midi2 events and runs user hooks on the consumer's own
// delivery thread. Never hand a midi2 hook the PipeWire data thread.
class BMidiInputBridge : public BMidiInput {
public:
								BMidiInputBridge(BMidiLocalConsumer* owner);
	virtual						~BMidiInputBridge();

			void				DeliverPending();

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
			struct QueuedEvent {
				enum kind {
					kNoteOff,
					kNoteOn,
					kKeyPressure,
					kControlChange,
					kProgramChange,
					kChannelPressure,
					kPitchBend,
					kSystemExclusive,
					kSystemCommon,
					kSystemRealTime
				};

				kind			what;
				uchar			channel;
				uchar			data1;
				uchar			data2;
				uchar			status;
				bigtime_t		time;
				uint8			sysex[256];
				size_t			length;
			};

			void				_Enqueue(const QueuedEvent& event);
			void				_Kick();

			BMidiLocalConsumer*	fOwner;
			BLocker				fQueueLock;
			QueuedEvent			fEvents[256];
			int32				fHead;
			int32				fTail;
			int32				fCount;
};


static thread_id sDeliveryThread = -1;
static volatile bool sDeliveryRunning = false;
static sem_id sDeliveryWake = -1;
// Every local consumer is served by the one delivery thread.
static std::vector<std::pair<BMidiLocalConsumer*, BMidiInputBridge*> >
	sDeliveryList;
static BLocker sDeliveryLock("midi2 delivery state");


static int32
midi2_delivery_thread(void*)
{
	while (sDeliveryRunning) {
		if (sDeliveryWake >= 0)
			acquire_sem_etc(sDeliveryWake, 1, B_TIMEOUT, 2000);

		BAutolock lock(sDeliveryLock);
		std::vector<std::pair<BMidiLocalConsumer*, BMidiInputBridge*> >
			list = sDeliveryList;
		for (size_t i = 0; i < list.size(); i++) {
			list[i].second->DeliverPending();
			list[i].first->EventThread();
		}
	}
	return 0;
}


BMidiInputBridge::BMidiInputBridge(BMidiLocalConsumer* owner)
	:
	// Virtual base must be named by the most-derived class.
	BMediaConnection(B_MEDIA_INPUT, "midi in"),
	BMidiInput("midi in"),
	fOwner(owner),
	fQueueLock("midi2 consumer queue"),
	fHead(0),
	fTail(0),
	fCount(0)
{
}


BMidiInputBridge::~BMidiInputBridge()
{
}


void
BMidiInputBridge::_Kick()
{
	if (sDeliveryWake >= 0)
		release_sem(sDeliveryWake);
}


void
BMidiInputBridge::_Enqueue(const QueuedEvent& event)
{
	{
		BAutolock lock(fQueueLock);
		if (fCount >= (int32)(sizeof(fEvents) / sizeof(fEvents[0])))
			return;
		fEvents[fTail] = event;
		fTail = (fTail + 1) % (int32)(sizeof(fEvents) / sizeof(fEvents[0]));
		fCount++;
	}
	_Kick();
}


void
BMidiInputBridge::DeliverPending()
{
	BAutolock lock(fQueueLock);
	BMidiLocalConsumer* owner = fOwner;
	if (owner == NULL)
		return;

	while (fCount > 0) {
		QueuedEvent e = fEvents[fHead];
		fHead = (fHead + 1) % (int32)(sizeof(fEvents) / sizeof(fEvents[0]));
		fCount--;

		// Drop the lock around user hooks: they may block (snooze_until).
		fQueueLock.Unlock();
		switch (e.what) {
			case QueuedEvent::kNoteOff:
				owner->NoteOff(e.channel, e.data1, e.data2, e.time);
				break;
			case QueuedEvent::kNoteOn:
				owner->NoteOn(e.channel, e.data1, e.data2, e.time);
				break;
			case QueuedEvent::kKeyPressure:
				owner->KeyPressure(e.channel, e.data1, e.data2, e.time);
				break;
			case QueuedEvent::kControlChange:
				owner->ControlChange(e.channel, e.data1, e.data2, e.time);
				break;
			case QueuedEvent::kProgramChange:
				owner->ProgramChange(e.channel, e.data1, e.time);
				break;
			case QueuedEvent::kChannelPressure:
				owner->ChannelPressure(e.channel, e.data1, e.time);
				break;
			case QueuedEvent::kPitchBend:
				owner->PitchBend(e.channel, e.data1, e.data2, e.time);
				break;
			case QueuedEvent::kSystemExclusive:
				owner->SystemExclusive(e.sysex, e.length, e.time);
				break;
			case QueuedEvent::kSystemCommon:
				owner->SystemCommon(e.status, e.data1, e.data2, e.time);
				break;
			case QueuedEvent::kSystemRealTime:
				owner->SystemRealTime(e.status, e.time);
				break;
		}
		fQueueLock.Lock();
	}
}


void
BMidiInputBridge::NoteOff(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kNoteOff;
	e.channel = channel;
	e.data1 = note;
	e.data2 = velocity;
	e.time = time;
	_Enqueue(e);
}


void
BMidiInputBridge::NoteOn(uchar channel, uchar note, uchar velocity,
	bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kNoteOn;
	e.channel = channel;
	e.data1 = note;
	e.data2 = velocity;
	e.time = time;
	_Enqueue(e);
}


void
BMidiInputBridge::KeyPressure(uchar channel, uchar note, uchar pressure,
	bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kKeyPressure;
	e.channel = channel;
	e.data1 = note;
	e.data2 = pressure;
	e.time = time;
	_Enqueue(e);
}


void
BMidiInputBridge::ControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kControlChange;
	e.channel = channel;
	e.data1 = controlNumber;
	e.data2 = controlValue;
	e.time = time;
	_Enqueue(e);
}


void
BMidiInputBridge::ProgramChange(uchar channel, uchar programNumber,
	bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kProgramChange;
	e.channel = channel;
	e.data1 = programNumber;
	e.time = time;
	_Enqueue(e);
}


void
BMidiInputBridge::ChannelPressure(uchar channel, uchar pressure,
	bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kChannelPressure;
	e.channel = channel;
	e.data1 = pressure;
	e.time = time;
	_Enqueue(e);
}


void
BMidiInputBridge::PitchBend(uchar channel, uchar lsb, uchar msb,
	bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kPitchBend;
	e.channel = channel;
	e.data1 = lsb;
	e.data2 = msb;
	e.time = time;
	_Enqueue(e);
}


void
BMidiInputBridge::SystemExclusive(void* data, size_t length, bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kSystemExclusive;
	e.time = time;
	if (data != NULL && length > 0) {
		if (length > sizeof(e.sysex))
			length = sizeof(e.sysex);
		memcpy(e.sysex, data, length);
		e.length = length;
	}
	_Enqueue(e);
}


void
BMidiInputBridge::SystemCommon(uchar status, uchar data1, uchar data2,
	bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kSystemCommon;
	e.status = status;
	e.data1 = data1;
	e.data2 = data2;
	e.time = time;
	_Enqueue(e);
}


void
BMidiInputBridge::SystemRealTime(uchar status, bigtime_t time)
{
	QueuedEvent e;
	memset(&e, 0, sizeof(e));
	e.what = QueuedEvent::kSystemRealTime;
	e.status = status;
	e.time = time;
	_Enqueue(e);
}


BMidiConsumer::BMidiConsumer(const char *name)
	:
	BMidiEndpoint(name)
{
	fPort = -1;
	fLatency = 0;
	fInput = NULL;
	memset(_reserved, 0, sizeof(_reserved));
	fIsConsumer = true;
	fIsLocal = true;
}


BMidiConsumer::~BMidiConsumer()
{
	if (fInput != NULL && Unit() != NULL)
		Unit()->UnregisterInput(fInput);
	delete fInput;
	fInput = NULL;
}


bigtime_t
BMidiConsumer::Latency() const
{
	return fLatency;
}


void
BMidiConsumer::_Reserved1()
{
}


void
BMidiConsumer::_Reserved2()
{
}


void
BMidiConsumer::_Reserved3()
{
}


void
BMidiConsumer::_Reserved4()
{
}


void
BMidiConsumer::_Reserved5()
{
}


void
BMidiConsumer::_Reserved6()
{
}


void
BMidiConsumer::_Reserved7()
{
}


void
BMidiConsumer::_Reserved8()
{
}


BMidiLocalConsumer::BMidiLocalConsumer(const char *name)
	:
	BMidiConsumer(name)
{
	fTimeout = 0;
	fTimeoutData = NULL;
	fCurrentProducer = 0;
	fThread = -1;
	memset(_reserved, 0, sizeof(_reserved));

	BMidiUnit* unit = new(std::nothrow) BMidiUnit(
		name != NULL ? name : "midi2 consumer", B_MEDIA_RECORDER);
	if (unit == NULL)
		return;

	BMidiInputBridge* bridge = new(std::nothrow) BMidiInputBridge(this);
	if (bridge == NULL) {
		delete unit;
		return;
	}
	bridge->SetAcceptedTypes(B_MEDIA_TYPE_BIT(B_MEDIA_MIDI));

	if (unit->RegisterInput(bridge) != B_OK) {
		delete bridge;
		delete unit;
		return;
	}

	SetUnit(unit);
	SetInput(bridge);
	fIsLocal = true;
	fIsConsumer = true;

	{
		BAutolock lock(sDeliveryLock);
		sDeliveryList.push_back(std::make_pair(this, bridge));
	}

	if (sDeliveryThread < 0) {
		sDeliveryRunning = true;
		sDeliveryWake = create_sem(0, "midi2 consumer delivery");
		sDeliveryThread = spawn_thread(midi2_delivery_thread,
			"midi2 delivery", B_NORMAL_PRIORITY, NULL);
		if (sDeliveryThread >= 0)
			resume_thread(sDeliveryThread);
	}
}


BMidiLocalConsumer::~BMidiLocalConsumer()
{
	{
		BAutolock lock(sDeliveryLock);
		for (size_t i = 0; i < sDeliveryList.size(); i++) {
			if (sDeliveryList[i].first == this) {
				sDeliveryList.erase(sDeliveryList.begin() + i);
				break;
			}
		}
	}

	if (sDeliveryThread >= 0 && sDeliveryList.empty()) {
		sDeliveryRunning = false;
		if (sDeliveryWake >= 0)
			release_sem(sDeliveryWake);
		int32 result;
		wait_for_thread(sDeliveryThread, &result);
		sDeliveryThread = -1;
		if (sDeliveryWake >= 0) {
			delete_sem(sDeliveryWake);
			sDeliveryWake = -1;
		}
	}

	if (Unit() != NULL)
		Unit()->Stop();
}


void
BMidiLocalConsumer::SetLatency(bigtime_t latency)
{
	fLatency = latency;
}


int32
BMidiLocalConsumer::GetProducerID()
{
	return fCurrentProducer;
}


void
BMidiLocalConsumer::SetTimeout(bigtime_t when, void *data)
{
	fTimeout = when;
	fTimeoutData = data;
}


void
BMidiLocalConsumer::Timeout(void *)
{
}


void
BMidiLocalConsumer::Data(uchar *, size_t, bool, bigtime_t)
{
}


void
BMidiLocalConsumer::NoteOff(uchar, uchar, uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::NoteOn(uchar, uchar, uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::KeyPressure(uchar, uchar, uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::ControlChange(uchar, uchar, uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::ProgramChange(uchar, uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::ChannelPressure(uchar, uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::PitchBend(uchar, uchar, uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::SystemExclusive(void*, size_t, bigtime_t)
{
}


void
BMidiLocalConsumer::SystemCommon(uchar, uchar, uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::SystemRealTime(uchar, bigtime_t)
{
}


void
BMidiLocalConsumer::TempoChange(int32, bigtime_t)
{
}


void
BMidiLocalConsumer::AllNotesOff(bool, bigtime_t)
{
}


int32
BMidiLocalConsumer::EventThread()
{
	// Timeout hook runs on the delivery thread, never on RT.
	if (fTimeout != 0 && system_time() >= fTimeout) {
		fTimeout = 0;
		Timeout(fTimeoutData);
	}
	return 0;
}


void
BMidiLocalConsumer::_Reserved1()
{
}


void
BMidiLocalConsumer::_Reserved2()
{
}


void
BMidiLocalConsumer::_Reserved3()
{
}


void
BMidiLocalConsumer::_Reserved4()
{
}


void
BMidiLocalConsumer::_Reserved5()
{
}


void
BMidiLocalConsumer::_Reserved6()
{
}


void
BMidiLocalConsumer::_Reserved7()
{
}


void
BMidiLocalConsumer::_Reserved8()
{
}
