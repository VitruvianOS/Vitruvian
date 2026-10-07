/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MIDI2_MIDI_SCHEDULE_H
#define _MIDI2_MIDI_SCHEDULE_H

#include <List.h>
#include <Locker.h>
#include <OS.h>
#include <SupportDefs.h>

#include <midi2/MidiProducer.h>


// App-side time-ordered scheduling queue for BMidiLocalProducer (§9.3b).
// Holds future-timestamped events until their performance time arrives,
// then calls BMidiOutput::SendX(). Time conversion between bigtime_t
// performance time and sample offsets happens in exactly one place:
// BMidiOutput::FillBuffer. This queue only decides WHEN.
//
// Backpressure is applied here, on the caller's thread, when the queue
// is full: blocking an app thread is legitimate; blocking the RT thread
// is never acceptable.
class BMidiSchedule {
public:
								BMidiSchedule(BMidiLocalProducer* producer);
								~BMidiSchedule();

			void				SetOutput(BMidiOutput* output);

			// Enqueue and optionally wait for due delivery. time==0 means now.
			status_t			Enqueue(const void* data, size_t length,
									bigtime_t time, bool sysex);

			// Drain events whose time has arrived into the output ring.
			void				Drain();

			// Wake the scheduler thread (destructor, Stop, etc.).
			void				Kick();

			bool				IsEmpty() const;

private:
			struct Event {
				bigtime_t		time;
				uint8			data[4];
				uint8			sysEx[256];
				size_t			length;
				bool			sysex;
			};

			static	int32		_SchedulerThread(void* data);
			int32				_SchedulerLoop();

			mutable BLocker		fLock;
			BList				fQueue;
			BMidiOutput*		fOutput;
			BMidiLocalProducer*	fProducer;
			thread_id			fThread;
			volatile bool		fRunning;
			sem_id				fWake;
			static const int32	kMaxQueued = 1024;
};

#endif // _MIDI2_MIDI_SCHEDULE_H
