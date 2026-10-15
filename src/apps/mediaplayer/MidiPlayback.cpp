/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MidiPlayback.h"

#include "MediaMessages.h"

#include <OS.h>
#include <Path.h>
#include <String.h>

#include <FindDirectory.h>
#include <midi/MidiSynthFile.h>


MidiPlayback* MidiPlayback::sHookOwner = NULL;


MidiPlayback::MidiPlayback()
	:
	fSynth(NULL),
	fInitErr(B_NO_INIT),
	fPlaying(false),
	fStarted(0),
	fPosition(0),
	fDuration(0),
	fVolume(1.0f)
{
}


MidiPlayback::~MidiPlayback()
{
	// Detach the song hook before the store's Stop() joins its thread.
	if (sHookOwner == this)
		sHookOwner = NULL;
	if (fSynth != NULL)
		fSynth->SetFileHook(NULL, 0);
	delete fSynth;
}


status_t
MidiPlayback::InitCheck() const
{
	return fInitErr;
}


status_t
MidiPlayback::SetTo(const entry_ref& ref)
{
	if (ref.name == NULL)
		return B_BAD_VALUE;
	// No extension gate: the Controller also routes by SMF header so
	// extensionless audio/midi files land here, and LoadFile reports the
	// real parse error on anything that is not MIDI.
	delete fSynth;
	fSynth = new BMidiSynthFile();
	if (fSynth == NULL)
		return fInitErr = B_NO_MEMORY;
	fPlaying = false;
	fPosition = 0;
	fDuration = 0;
	status_t err = fSynth->LoadFile(&ref);
	if (err != B_OK)
		return fInitErr = err;
	fDuration = (bigtime_t)fSynth->Duration() * 1000;
	fSynth->SetVolume(fVolume);
	sHookOwner = this;
	fSynth->SetFileHook(&_SongHook, 0);
	return fInitErr = B_OK;
}


status_t
MidiPlayback::SetTo(const BUrl& url)
{
	return _Load(url);
}


status_t
MidiPlayback::_Load(const BUrl& url)
{
	BPath path(url.Path().String());
	entry_ref ref;
	status_t err = get_ref_for_path(path.Path(), &ref);
	return err == B_OK ? SetTo(ref) : err;
}


void
MidiPlayback::SetFinishedTarget(const BMessenger& target)
{
	fFinishedTarget = target;
}


status_t
MidiPlayback::Play()
{
	if (fInitErr != B_OK || fSynth == NULL)
		return fInitErr;
	status_t err = fPlaying ? B_OK : fSynth->Start();
	if (err != B_OK)
		return err;
	fStarted = system_time() - fPosition;
	fPlaying = true;
	return B_OK;
}


status_t
MidiPlayback::Pause()
{
	if (fPlaying && fSynth != NULL) {
		fPosition = Position();
		// The store thread exits with its current event intact; Play()
		// resumes from there.
		fSynth->Stop();
	}
	fPlaying = false;
	return B_OK;
}


status_t
MidiPlayback::Stop()
{
	if (fSynth != NULL) {
		fSynth->Stop();
		fSynth->Seek(0);
	}
	fPlaying = false;
	fPosition = 0;
	return B_OK;
}


status_t
MidiPlayback::SeekTo(bigtime_t position)
{
	if (position < 0)
		return B_BAD_VALUE;
	if (fInitErr != B_OK || fSynth == NULL)
		return fInitErr;
	if (fDuration > 0 && position > fDuration)
		position = fDuration;
	fPosition = position;
	fSynth->Seek((int32)(position / 1000));
	if (fPlaying)
		fStarted = system_time() - fPosition;
	return B_OK;
}


bigtime_t
MidiPlayback::Position() const
{
	if (!fPlaying || fSynth == NULL)
		return fPosition;
	bigtime_t position = system_time() - fStarted;
	if (position < 0)
		position = 0;
	if (fDuration > 0 && position > fDuration)
		position = fDuration;
	return position;
}


bigtime_t
MidiPlayback::Duration() const
{
	return fDuration;
}


status_t
MidiPlayback::SetVolume(float volume)
{
	if (volume < 0.0f)
		volume = 0.0f;
	if (volume > 1.0f)
		volume = 1.0f;
	fVolume = volume;
	if (fSynth != NULL)
		fSynth->SetVolume(volume);
	return B_OK;
}


bool
MidiPlayback::IsPlaying() const
{
	return fPlaying;
}


/* static */
void
MidiPlayback::_SongHook(int32)
{
	MidiPlayback* self = sHookOwner;
	if (self == NULL)
		return;
	self->_OnSongFinished();
}


void
MidiPlayback::_OnSongFinished()
{
	fPlaying = false;
	fPosition = fDuration;
	if (fFinishedTarget.IsValid())
		fFinishedTarget.SendMessage(new BMessage(MSG_CONTROLLER_FINISHED));
}
