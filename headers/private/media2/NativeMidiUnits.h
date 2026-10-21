/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_NATIVE_MIDI_UNITS_H
#define _MEDIA2_NATIVE_MIDI_UNITS_H


// Native MIDI units: pass-through filters, sources and sinks built on
// BMidiUnit. Processing happens in the MIDI input hooks and the unit
// cycle; parameter values are atomics written from the app thread, so
// the realtime path never allocates or locks beyond the kit's own
// output ring.

#include <atomic>

#include <String.h>

#include <media2/Controllable.h>
#include <media2/MidiUnit.h>
#include <media2/ParameterWeb.h>


class BWindow;
class BMidiKeyboardWindow;
class BMidiMonitorWindow;


class BMidiNativeUnit : public BMidiUnit {
public:
	virtual						~BMidiNativeUnit();

			status_t			InitCheck() const;

			BControllable*		ControllableInstance() const;

			// App thread: push a value through the controllable so the
			// web, the change hook and the atomics stay in sync.
			status_t			SetParameter(int32 id, float value);

protected:
								BMidiNativeUnit(const char* name,
									media_client_kinds kinds);

			// App thread, during construction only.
			void				AddContinuousParam(int32 id, const char* name,
									float minValue, float maxValue,
									float defaultValue);
			void				AddDiscreteParam(int32 id, const char* name,
									int32 defaultValue, const char* const* items,
									int32 itemCount, const char* kind);
			void				AddTextParam(int32 id, const char* name,
									size_t maxBytes);
			// Builds the parameter web and installs the change hook.
			void				FinishParameters(const char* groupName);

			// Realtime readers.
			float				FloatParam(int32 id) const;
			int32				IntParam(int32 id) const;
			const char*			TextParam(int32 id) const;

			// App thread: ask the realtime side to release tracked notes
			// before the new values take effect on them.
			void				RequestFlush();

			bool				FlushPending() const;
			void				ConsumeFlush();

	// App thread, after the atomic store. Units that change note
	// identity call RequestFlush() from here.
	virtual	void				ParameterChanged(int32 id);

private:
	struct Param {
		BString					name;
		float					minValue;
		float					maxValue;
		float					defaultValue;
		std::atomic<float>		floatValue;
		std::atomic<int32>		intValue;
		BString					textValue;
		const char* const*		items;
		int32					itemCount;
		size_t					maxBytes;
		uint8					type;	// 0 continuous, 1 discrete, 2 text
	};

	static	void				_ChangeHook(int32 id, bigtime_t when,
									const void* value, size_t size,
									void* cookie);

			Param*				ParamAt(int32 id) const;

			Param*				fParams[32];
			int32				fParamCount;

protected:
			BControllable*		fControllable;

private:
			std::atomic<bool>	fFlushPending;

protected:
			status_t			fInitStatus;
};


// One MIDI input forwarding to one or two MIDI outputs. Handlers run on
// the realtime thread with events already decoded.
class BMidiThruUnit : public BMidiNativeUnit {
protected:
								BMidiThruUnit(const char* name,
									media_client_kinds kinds,
									int32 outputCount = 1,
									const char* const* outputNames = NULL);
	virtual						~BMidiThruUnit();

			BMidiOutput*		OutputAt(int32 index) const;

			// Called at the top of every handler; releases tracked notes
			// when a control changed.
			void				CheckFlush(bigtime_t time);

	virtual	void				ProcessCycle(uint32 frameCount) override;
	virtual	void				HandleFlush(bigtime_t time);

	virtual	void				HandleNoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time);
	virtual	void				HandleNoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time);
	virtual	void				HandleKeyPressure(uchar channel, uchar note,
									uchar pressure, bigtime_t time);
	virtual	void				HandleControlChange(uchar channel,
									uchar controlNumber, uchar controlValue,
									bigtime_t time);
	virtual	void				HandleProgramChange(uchar channel,
									uchar programNumber, bigtime_t time);
	virtual	void				HandleChannelPressure(uchar channel,
									uchar pressure, bigtime_t time);
	virtual	void				HandlePitchBend(uchar channel, uchar lsb,
									uchar msb, bigtime_t time);
	virtual	void				HandleSystemExclusive(void* data,
									size_t length, bigtime_t time);
	virtual	void				HandleSystemCommon(uchar status, uchar data1,
									uchar data2, bigtime_t time);
	virtual	void				HandleSystemRealTime(uchar status,
									bigtime_t time);

private:
	class ThruInput;

			ThruInput*			fThruInput;
			BMidiOutput*		fOutputs[2];
			int32				fOutputCount;
};


// Shifts note numbers by a semitone amount, clamped to 0-127. Note-offs
// follow the note number the matching note-on was sent with.
class BTransposeUnit : public BMidiThruUnit {
public:
								BTransposeUnit();

protected:
	virtual	void				ParameterChanged(int32 id) override;
	virtual	void				HandleFlush(bigtime_t time) override;
	virtual	void				HandleNoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				HandleNoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;

private:
			int16				fSentNote[16][128];
};


// Passes or blocks each channel and remaps one channel onto another.
class BChannelFilterUnit : public BMidiThruUnit {
public:
								BChannelFilterUnit();

protected:
	virtual	void				ParameterChanged(int32 id) override;
	virtual	void				HandleFlush(bigtime_t time) override;
	virtual	void				HandleNoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				HandleNoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				HandleKeyPressure(uchar channel, uchar note,
									uchar pressure, bigtime_t time) override;
	virtual	void				HandleControlChange(uchar channel,
									uchar controlNumber, uchar controlValue,
									bigtime_t time) override;
	virtual	void				HandleProgramChange(uchar channel,
									uchar programNumber, bigtime_t time) override;
	virtual	void				HandleChannelPressure(uchar channel,
									uchar pressure, bigtime_t time) override;
	virtual	void				HandlePitchBend(uchar channel, uchar lsb,
									uchar msb, bigtime_t time) override;

private:
			bool				_IsPassed(uchar channel) const;
			uchar				_MapChannel(uchar channel) const;
			void				_RebuildPassMask();

			std::atomic<uint32>	fPassMask;
			int8				fSentChannel[16][128];
};


// Notes below the split point go to output A, the rest to output B.
// Non-note events go to both.
class BKeySplitUnit : public BMidiThruUnit {
public:
								BKeySplitUnit();

protected:
	virtual	void				ParameterChanged(int32 id) override;
	virtual	void				HandleFlush(bigtime_t time) override;
	virtual	void				HandleNoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				HandleNoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				HandleKeyPressure(uchar channel, uchar note,
									uchar pressure, bigtime_t time) override;
	virtual	void				HandleControlChange(uchar channel,
									uchar controlNumber, uchar controlValue,
									bigtime_t time) override;
	virtual	void				HandleProgramChange(uchar channel,
									uchar programNumber, bigtime_t time) override;
	virtual	void				HandleChannelPressure(uchar channel,
									uchar pressure, bigtime_t time) override;
	virtual	void				HandlePitchBend(uchar channel, uchar lsb,
									uchar msb, bigtime_t time) override;

private:
			uint8				_RouteFor(uchar note) const;

			uint8				fRoute[16][128];
};


// Reshapes note-on velocity. Note-offs pass unchanged.
class BVelocityCurveUnit : public BMidiThruUnit {
public:
								BVelocityCurveUnit();

protected:
	virtual	void				HandleNoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;

private:
			uchar				_Transform(uchar velocity);

			uint32				fRandomState;
};


// Expands each held note into a chord of selectable intervals.
class BChordUnit : public BMidiThruUnit {
public:
								BChordUnit();

protected:
	virtual	void				HandleStop(bigtime_t performanceTime) override;
	virtual	void				ParameterChanged(int32 id) override;
	virtual	void				HandleFlush(bigtime_t time) override;
	virtual	void				HandleNoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				HandleNoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;

private:
			int32				_IntervalCount() const;
			int32				_IntervalAt(int32 index) const;

			uint8				fSentCount[16][128];
			int8				fSentNote[16][128][8];
};


// Remaps one controller number onto another and rescales its value.
class BCCMapperUnit : public BMidiThruUnit {
public:
								BCCMapperUnit();

protected:
	virtual	void				HandleControlChange(uchar channel,
									uchar controlNumber, uchar controlValue,
									bigtime_t time) override;
};


// MIDI clock, optional transport and note ticks at a set tempo. Driven
// by the unit cycle; Cortex Start/Stop emits the transport messages.
class BClockUnit : public BMidiNativeUnit {
public:
								BClockUnit();
	virtual						~BClockUnit();

protected:
	virtual	void				HandleStart(bigtime_t performanceTime) override;
	virtual	void				HandleStop(bigtime_t performanceTime) override;
	virtual	void				ProcessCycle(uint32 frameCount) override;

private:
			void				_SendTick(bigtime_t time, bool quarter);

			BMidiOutput*		fOutput;
			bigtime_t			fLastCycle;
			bigtime_t			fPhase;
			bigtime_t			fNoteOffDue;
			bool				fTickedNote;
};


// Turns held notes into an up/down/random pattern at a set rate.
class BArpeggiatorUnit : public BMidiThruUnit {
public:
								BArpeggiatorUnit();
	virtual						~BArpeggiatorUnit();

protected:
	virtual	void				HandleStop(bigtime_t performanceTime) override;
	virtual	void				ParameterChanged(int32 id) override;
	virtual	void				HandleFlush(bigtime_t time) override;
	virtual	void				ProcessCycle(uint32 frameCount) override;
	virtual	void				HandleNoteOff(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;
	virtual	void				HandleNoteOn(uchar channel, uchar note,
									uchar velocity, bigtime_t time) override;

private:
			void				_ReleasePattern(bigtime_t time);
			void				_AdvanceStep(bigtime_t time);

			bool				fHeld[128];
			uchar				fHeldNote[256];
			uchar				fHeldChannel[256];
			int32				fHeldCount;
			int32				fStepIndex;
			uint32				fRandomState;
			bigtime_t			fLastCycle;
			bigtime_t			fPhase;
			bool				fSounding;
			uchar				fSoundingNote;
			uchar				fSoundingChannel;
};


// Plays a Standard MIDI File out of the output port. The file is parsed
// on the app thread at Start and on path changes.
class BMidiFilePlayerUnit : public BMidiNativeUnit {
public:
								BMidiFilePlayerUnit();
	virtual						~BMidiFilePlayerUnit();

protected:
	virtual	void				HandleStart(bigtime_t performanceTime) override;
	virtual	void				HandleStop(bigtime_t performanceTime) override;
	virtual	void				HandleSeek(bigtime_t mediaTime,
									bigtime_t performanceTime) override;
	virtual	void				ParameterChanged(int32 id) override;
	virtual	void				ProcessCycle(uint32 frameCount) override;

private:
	struct Event {
		bigtime_t				time;
		uint8					data[3];
		uint8					length;
	};

			status_t			_LoadFile(const char* path);
			void				_Rewind(bigtime_t performanceTime);

			BMidiOutput*		fOutput;
			Event*				fEvents;
			int32				fEventCount;
			int32				fIndex;
			bigtime_t			fBaseTime;
			bool				fPlaying;
};


// Plain MIDI output source: a BMidiUnit hosting one BMidiOutput, the
// catalog version of the Cortex MIDI Out test node.
class BMidiOutUnit : public BMidiNativeUnit {
public:
								BMidiOutUnit();
};


// On-screen piano played with the mouse or the computer keyboard;
// channel, octave and velocity selectors, also exposed as parameters.
class BMidiKeyboardUnit : public BMidiNativeUnit {
public:
								BMidiKeyboardUnit();
	virtual						~BMidiKeyboardUnit();

			void				ShowWindow();
			void				HideWindow();

			// App thread (window or change hook). Safe from either.
			void				PlayNote(uchar note);
			void				StopNote(uchar note);
			void				ReleaseAll();

			uchar				Channel() const;
			int32				Octave() const;
			uchar				Velocity() const;

protected:
	virtual	void				HandleStart(bigtime_t performanceTime) override;
	virtual	void				HandleStop(bigtime_t performanceTime) override;
	virtual	void				ParameterChanged(int32 id) override;

private:
			BMidiOutput*		fOutput;
			BMidiKeyboardWindow*	fWindow;
			std::atomic<int32>	fChannel;
			std::atomic<int32>	fOctave;
			std::atomic<int32>	fVelocity;
			bool				fKeysDown[128];
};


// Logs incoming MIDI events into a window with pause and clear.
class BMidiMonitorUnit : public BMidiNativeUnit {
public:
								BMidiMonitorUnit();
	virtual						~BMidiMonitorUnit();

			void				ShowWindow();
			void				HideWindow();

			// Realtime: push a raw record into the ring.
			void				PushEvent(bigtime_t time, uint8 type,
									uchar channel, uchar data1, uchar data2);

			// App thread.
			int32				ReadEvents(int32 maxCount, bigtime_t* outTimes,
									uint8* outTypes, uchar* outChannels,
									uchar* outData1, uchar* outData2);
			void				SetPaused(bool paused);
			bool				IsPaused() const;
			bigtime_t			StartTime() const;

protected:
	virtual	void				HandleStart(bigtime_t performanceTime) override;
	virtual	void				HandleStop(bigtime_t performanceTime) override;

private:
	class MonitorInput;

			MonitorInput*		fInput;
			BMidiMonitorWindow*	fWindow;
			std::atomic<int32>	fPaused;
			bigtime_t			fStartTime;

			static const size_t	kRingCapacity = 512;
			struct Record {
				bigtime_t		time;
				uint8			type;
				uchar			channel;
				uchar			data1;
				uchar			data2;
			};
			Record				fRing[kRingCapacity];
			std::atomic<size_t>	fWriteIndex;
			std::atomic<size_t>	fReadIndex;
			std::atomic<uint64>	fDropped;
};


#endif // _MEDIA2_NATIVE_MIDI_UNITS_H
