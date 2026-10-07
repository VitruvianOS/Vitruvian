/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_MEDIA_SYNTH_H
#define _MEDIA2_MEDIA_SYNTH_H


#include <atomic>

#include <media2/MediaUnit.h>


class BMidiInput;


class BMediaSynth : public BMediaUnit {
public:
								BMediaSynth(const char* name,
									uint32 channelCount = 2);
	virtual						~BMediaSynth();

			status_t			SetChannelCount(uint32 count);
			uint32				ChannelCount() const;
			BMidiInput*			MidiInput() const;

	virtual	status_t			Start() override;
	virtual	status_t			Stop() override;

protected:
	// Realtime. Called once per span between MIDI events in the cycle, so a
	// subclass never sees a POD sequence and gets sample-accurate timing for
	// free. outputs[] holds ChannelCount() mono float32 buffers.
	virtual	void				Render(float** outputs, uint32 channelCount,
									uint32 frameCount) = 0;

	// App thread. Called at Start() and again whenever the graph rate changes;
	// the engine is rebuilt here, never in Render().
	virtual	status_t			SynthReset(uint32 sampleRate);
	virtual	void				SynthShutdown();

	// Per-event hooks, offset in frames from the start of the current span.
	virtual	void				NoteOn(uchar channel, uchar note, uchar velocity,
									uint32 offset);
	virtual	void				NoteOff(uchar channel, uchar note, uchar velocity,
									uint32 offset);
	virtual	void				KeyPressure(uchar channel, uchar note, uchar pressure,
									uint32 offset);
	virtual	void				ControlChange(uchar channel, uchar controlNumber,
									uchar controlValue, uint32 offset);
	virtual	void				ProgramChange(uchar channel, uchar programNumber,
									uint32 offset);
	virtual	void				ChannelPressure(uchar channel, uchar pressure,
									uint32 offset);
	virtual	void				PitchBend(uchar channel, uchar lsb, uchar msb,
									uint32 offset);
	virtual	void				SystemExclusive(void* data, size_t length,
									uint32 offset);

	// Final in effect. A subclass that overrides this loses segmented render,
	// rate-change silence, panic/reset and event dispatch together.
	virtual	void				ProcessCycle(uint32 frameCount) override;

private:
			class MidiHook;
			class AudioOutput;

			void				_DispatchNoteOn(uchar channel, uchar note,
									uchar velocity, uint32 offset);
			void				_DispatchNoteOff(uchar channel, uchar note,
									uchar velocity, uint32 offset);
			void				_Advance(float** outputs, uint32 frames);
			void				_ZeroTail(float** outputs, uint32 frames);

			uint32				fChannelCount;
			BMidiInput*			fMidiInput;
			MidiHook*			fMidiHook;
			bool				fEngineReady;
			uint32				fLastSampleRate;
			std::atomic<bool>	fRateChangePending;
			float*				fScratch[8];
};


#endif
