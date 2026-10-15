/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi/MidiSynthFile.h>
#include <midi/MidiStore.h>
#include <midi/Synth.h>
#include <media2/SoundFontSynth.h>

#include <string.h>


BMidiSynthFile::BMidiSynthFile()
	:
	BMidiSynth()
{
	fStore = NULL;
	memset(_reserved, 0, sizeof(_reserved));
}


BMidiSynthFile::~BMidiSynthFile()
{
	Stop();
	UnloadFile();
}


status_t
BMidiSynthFile::LoadFile(const entry_ref* midi_entry_ref)
{
	if (midi_entry_ref == NULL)
		return B_BAD_VALUE;
	if (be_synth == NULL || be_synth->SoundFont() == NULL)
		return B_NO_INIT;
	if (be_synth->SoundFont()->InitCheck() != B_OK)
		return be_synth->SoundFont()->InitCheck();

	UnloadFile();
	fStore = new(std::nothrow) BMidiStore();
	if (fStore == NULL)
		return B_NO_MEMORY;

	status_t status = fStore->Import(midi_entry_ref);
	if (status != B_OK) {
		delete fStore;
		fStore = NULL;
		return status;
	}

	// Wire the store into this synth: store sprays, synth receives through
	// the BMidi pair that BMidiSynth already owns.
	fStore->Connect(this);
	return B_OK;
}


void
BMidiSynthFile::UnloadFile()
{
	if (fStore != NULL) {
		fStore->Stop();
		fStore->Disconnect(this);
		delete fStore;
		fStore = NULL;
	}
}


status_t
BMidiSynthFile::Start()
{
	if (fStore == NULL)
		return B_NO_SONG_PLAYING;
	EnableInput(true, true);
	return fStore->Start();
}


void
BMidiSynthFile::Stop()
{
	if (fStore != NULL)
		fStore->Stop();
	BMidiSynth::Stop();
}


void
BMidiSynthFile::Fade()
{
}


void
BMidiSynthFile::Pause()
{
	// fPaused on the store is private; stop/start is the portable pause.
	Stop();
}


void
BMidiSynthFile::Resume()
{
	Start();
}


int32
BMidiSynthFile::Duration() const
{
	return fStore != NULL ? (int32)fStore->DurationMs() : 0;
}


int32
BMidiSynthFile::Position(int32) const
{
	if (fStore == NULL)
		return 0;

	int32 duration = (int32)fStore->DurationMs();
	if (!fStore->fFinished && fStore->fStartTime != 0) {
		int32 position = (int32)((uint32)B_NOW - fStore->fStartTime);
		if (position < 0)
			position = 0;
		if (duration > 0 && position > duration)
			position = duration;
		return position;
	}

	// Not running: report the resume point.
	const BMidiEvent* event = fStore->EventAt(fStore->fCurrentEvent);
	return event != NULL ? (int32)fStore->GetEventTime(event) : 0;
}


int32
BMidiSynthFile::Seek(int32 msecs)
{
	if (fStore == NULL)
		return -1;
	if (msecs < 0)
		msecs = 0;
	int32 duration = (int32)fStore->DurationMs();
	if (duration > 0 && msecs > duration)
		msecs = duration;

	// The run thread reads fCurrentEvent at start; a running seek
	// restarts it from the new event with the timeline rebased.
	bool running = fStore->IsRunning();
	if (running)
		fStore->Stop();
	fStore->SetCurrentEvent(fStore->EventAtDelta((uint32)msecs));
	if (running)
		fStore->Start();
	return msecs;
}


status_t
BMidiSynthFile::GetPatches(int16* pArray768, int16* pReturnedCount) const
{
	if (pArray768 == NULL || pReturnedCount == NULL)
		return B_BAD_VALUE;
	memset(pArray768, 0, 768 * sizeof(int16));
	*pReturnedCount = 0;
	return B_OK;
}


void
BMidiSynthFile::SetFileHook(synth_file_hook pSongHook, int32 arg)
{
	if (fStore == NULL)
		return;
	fStore->fHookFunc = pSongHook;
	fStore->fHookArg = arg;
}


bool
BMidiSynthFile::IsFinished() const
{
	return fStore != NULL ? fStore->fFinished : true;
}


void
BMidiSynthFile::ScaleTempoBy(double)
{
}


void
BMidiSynthFile::SetTempo(int32 newTempoBPM)
{
	if (fStore != NULL)
		fStore->SetTempo(newTempoBPM);
}


int32
BMidiSynthFile::Tempo() const
{
	return fStore != NULL ? fStore->Tempo() : 0;
}


void
BMidiSynthFile::EnableLooping(bool loop)
{
	(void)loop;
}


void
BMidiSynthFile::MuteTrack(int16, bool)
{
}


void
BMidiSynthFile::GetMuteMap(char* pTracks) const
{
	if (pTracks != NULL)
		memset(pTracks, 0, 256);
}


void
BMidiSynthFile::SoloTrack(int16, bool)
{
}


void
BMidiSynthFile::GetSoloMap(char* pTracks) const
{
	if (pTracks != NULL)
		memset(pTracks, 0, 256);
}


void
BMidiSynthFile::_ReservedMidiSynthFile1()
{
}


void
BMidiSynthFile::_ReservedMidiSynthFile2()
{
}


void
BMidiSynthFile::_ReservedMidiSynthFile3()
{
}
