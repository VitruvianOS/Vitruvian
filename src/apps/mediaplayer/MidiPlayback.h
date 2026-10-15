/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MIDI_PLAYBACK_H
#define _MIDI_PLAYBACK_H


#include <Entry.h>
#include <Messenger.h>
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

			// Where the end-of-song MSG_CONTROLLER_FINISHED lands.
			void				SetFinishedTarget(const BMessenger& target);

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

			static	void		_SongHook(int32 arg);
			void				_OnSongFinished();

private:
			// The file hook is a plain C callback; MediaPlayer plays one
			// MIDI file at a time, so one active owner is enough.
			static	MidiPlayback*	sHookOwner;

			BMidiSynthFile*		fSynth;
			BMessenger			fFinishedTarget;
			status_t			fInitErr;
			bool				fPlaying;
			bigtime_t			fStarted;
			bigtime_t			fPosition;
			bigtime_t			fDuration;
			float				fVolume;
};


#endif // _MIDI_PLAYBACK_H
