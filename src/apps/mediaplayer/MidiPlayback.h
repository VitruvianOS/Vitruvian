/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MIDI_PLAYBACK_H
#define _MIDI_PLAYBACK_H


#include <Entry.h>
#include <SupportDefs.h>
#include <Url.h>


class BMidiSynthFile;


// MIDI playback through BMidiSynthFile (SMF store into the SoundFont synth).
class MidiPlayback {
public:
								MidiPlayback();
								~MidiPlayback();

			status_t			InitCheck() const;

			status_t			SetTo(const entry_ref& ref);
			status_t			SetTo(const BUrl& url);

			status_t			Play();
			status_t			Pause();
			status_t			Stop();

			status_t			SeekTo(bigtime_t position);
			bigtime_t			Position() const;
			bigtime_t			Duration() const;

			status_t			SetVolume(float volume);
			bool				IsPlaying() const;

private:
			status_t			_Load(const BUrl& url);

private:
			BMidiSynthFile*		fSynth;
			status_t			fInitErr;
			bool				fPlaying;
			bigtime_t			fStarted;
			bigtime_t			fPosition;
			bigtime_t			fDuration;
			float				fVolume;
};


#endif // _MIDI_PLAYBACK_H
