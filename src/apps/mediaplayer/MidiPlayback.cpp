/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MidiPlayback.h"

#include <OS.h>
#include <Path.h>
#include <String.h>

#include <FindDirectory.h>
#include <midi/MidiSynthFile.h>


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
	BString name(ref.name);
	name.ToLower();
	if (!name.EndsWith(".mid") && !name.EndsWith(".midi"))
		return B_BAD_VALUE;
	delete fSynth;
	fSynth = new BMidiSynthFile();
	if (fSynth == NULL)
		return fInitErr = B_NO_MEMORY;
	fPlaying = false;
	fPosition = 0;
	fDuration = 0;
	return fInitErr = fSynth->LoadFile(&ref);
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
		fPosition = system_time() - fStarted;
		fSynth->Stop();
	}
	fPlaying = false;
	return B_OK;
}


status_t
MidiPlayback::Stop()
{
	if (fSynth != NULL)
		fSynth->Stop();
	fPlaying = false;
	fPosition = 0;
	return B_OK;
}


status_t
MidiPlayback::SeekTo(bigtime_t position)
{
	if (position < 0)
		return B_BAD_VALUE;
	// The store only restarts from the top; other seeks keep playing.
	if (position == 0 && fSynth != NULL && fInitErr == B_OK) {
		bool wasPlaying = fPlaying;
		fSynth->Stop();
		fPlaying = false;
		fPosition = 0;
		return wasPlaying ? Play() : B_OK;
	}
	return B_NOT_SUPPORTED;
}


bigtime_t
MidiPlayback::Position() const
{
	return fPlaying ? system_time() - fStarted : fPosition;
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
	return B_OK;
}


bool
MidiPlayback::IsPlaying() const
{
	return fPlaying;
}
