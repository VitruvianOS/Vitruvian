/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_SOUND_FONT_SYNTH_H
#define _MEDIA2_SOUND_FONT_SYNTH_H


#include <String.h>

#include <media2/MediaSynth.h>


class BSoundFontSynth : public BMediaSynth {
public:
								BSoundFontSynth(const char* name,
									uint32 channelCount = 2);
	virtual						~BSoundFontSynth();

			status_t			InitCheck() const;

			status_t			LoadInstrumentFile(const char* path);
			const char*			InstrumentFile() const;

			void				SetGain(float gain);
			float				Gain() const;

			void				SetInterpolation(int32 mode);
			int32				Interpolation() const;

			void				SetReverb(bool enabled);
			bool				IsReverbEnabled() const;

			void				SetPolyphony(int32 voices);
			int32				Polyphony() const;

			void				SetVolume(float volume);
			float				Volume() const;

protected:
	virtual	void				Render(float** outputs, uint32 channelCount,
									uint32 frameCount) override;

	virtual	status_t			SynthReset(uint32 sampleRate) override;
	virtual	void				SynthShutdown() override;

	virtual	void				NoteOn(uchar channel, uchar note, uchar velocity,
									uint32 offset) override;
	virtual	void				NoteOff(uchar channel, uchar note, uchar velocity,
									uint32 offset) override;
	virtual	void				KeyPressure(uchar channel, uchar note, uchar pressure,
									uint32 offset) override;
	virtual	void				ControlChange(uchar channel, uchar controlNumber,
									uchar controlValue, uint32 offset) override;
	virtual	void				ProgramChange(uchar channel, uchar programNumber,
									uint32 offset) override;
	virtual	void				ChannelPressure(uchar channel, uchar pressure,
									uint32 offset) override;
	virtual	void				PitchBend(uchar channel, uchar lsb, uchar msb,
									uint32 offset) override;
	virtual	void				SystemExclusive(void* data, size_t length,
									uint32 offset) override;

private:
			static	const char*	_FindDefaultSoundFont();
			void				_ApplySettingsToSynth();

			struct Fluid;
			Fluid*				fFluid;
			BString				fInstrumentFile;
			status_t			fInitStatus;
			float				fGain;
			int32				fInterpolation;
			bool				fReverb;
			int32				fPolyphony;
			float				fVolume;
};


#endif
