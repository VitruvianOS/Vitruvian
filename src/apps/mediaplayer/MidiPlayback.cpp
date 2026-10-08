/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MidiPlayback.h"

#include <Path.h>
#include <String.h>

#include <media2/MediaDefs.h>


// The planned chain is BMidiStore (SMF parse) to the midi2 facade
// scheduler to BMidiOutput to BSoundFontSynth. Every entry point fails
// with a stated reason until that chain is wired in.


MidiPlayback::MidiPlayback()
	:
	fInitErr(B_OK),
	fPlaying(false),
	fPosition(0),
	fDuration(0),
	fVolume(1.0f)
{
}


MidiPlayback::~MidiPlayback()
{
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
	BPath path(&ref);
	if (path.InitCheck() != B_OK)
		return path.InitCheck();
	BUrl url(path);
	return _Load(url);
}


status_t
MidiPlayback::SetTo(const BUrl& url)
{
	return _Load(url);
}


status_t
MidiPlayback::_Load(const BUrl& url)
{
	// The synth stack is not linked into this app yet; fail loudly so
	// the UI can say so instead of hanging or going silent.
	(void)url;
	fPlaying = false;
	fPosition = 0;
	fDuration = 0;
	fInitErr = B_NOT_SUPPORTED;
	return fInitErr;
}


status_t
MidiPlayback::Play()
{
	if (fInitErr != B_OK)
		return fInitErr;
	fPlaying = true;
	return B_OK;
}


status_t
MidiPlayback::Pause()
{
	fPlaying = false;
	return B_OK;
}


status_t
MidiPlayback::Stop()
{
	fPlaying = false;
	fPosition = 0;
	return B_OK;
}


status_t
MidiPlayback::SeekTo(bigtime_t position)
{
	if (position < 0)
		return B_BAD_VALUE;
	fPosition = position;
	return B_OK;
}


bigtime_t
MidiPlayback::Position() const
{
	return fPosition;
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
