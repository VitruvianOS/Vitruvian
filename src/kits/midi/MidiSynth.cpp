/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <midi/MidiSynth.h>
#include <midi/Synth.h>
#include <midi2/MidiConsumer.h>
#include <midi2/MidiProducer.h>
#include <midi2/MidiRoster.h>
#include <media2/MediaConnection.h>
#include <media2/MediaGraph.h>
#include <media2/SoundFontSynth.h>

#include <string.h>


BMidiSynth::BMidiSynth()
	:
	BMidi()
{
	fSongVariables = NULL;
	fPerformanceVariables = NULL;
	fMidiQueue = true;
	fCreationTime = system_time();
	fTranspose = 0;
	fInputEnabled = false;
}


BMidiSynth::~BMidiSynth()
{
	Stop();
}


extern void _midi_link_synth_to_device(BSoundFontSynth* synth);


status_t
BMidiSynth::EnableInput(bool enable, bool loadInstruments)
{
	fInputEnabled = enable;
	if (!enable)
		return B_OK;

	if (be_synth == NULL || be_synth->SoundFont() == NULL)
		return B_NO_INIT;
	BSoundFontSynth* synth = be_synth->SoundFont();
	if (synth->InitCheck() != B_OK)
		return synth->InitCheck();
	if (loadInstruments && synth->InstrumentFile() == NULL)
		return B_BAD_INSTRUMENT;

	_midi_link_synth_to_device(synth);

	// Wire this façade's midi2 producer into the synth unit's MIDI input
	// so events travel the graph, not a private engine call.
	BMidiLocalProducer* producer = Producer();
	if (producer != NULL && producer->Output() != NULL
		&& synth->MidiInput() != NULL) {
		BMediaClient* client = producer->Output()->Client();
		if (client != NULL && !client->IsStarted())
			client->Start();
		MediaGraph()->Connect(producer->Output(), synth->MidiInput());
	}
	return B_OK;
}


bool
BMidiSynth::IsInputEnabled() const
{
	return fInputEnabled;
}


void
BMidiSynth::SetVolume(double volume)
{
	if (be_synth != NULL && be_synth->SoundFont() != NULL)
		be_synth->SoundFont()->SetVolume((float)volume);
}


double
BMidiSynth::Volume() const
{
	return be_synth != NULL && be_synth->SoundFont() != NULL
		? be_synth->SoundFont()->Volume() : 0.0;
}


void
BMidiSynth::SetTransposition(int16 offset)
{
	fTranspose = offset;
}


int16
BMidiSynth::Transposition() const
{
	return fTranspose;
}


void
BMidiSynth::MuteChannel(int16 channel, bool do_mute)
{
	if (!do_mute)
		return;
	// All-notes-off is a control change; spray it through the façade.
	SprayControlChange((uchar)channel, B_ALL_NOTES_OFF, 0, B_NOW);
}


void
BMidiSynth::GetMuteMap(char* pChannels) const
{
	if (pChannels != NULL)
		memset(pChannels, 0, 16);
}


void
BMidiSynth::SoloChannel(int16, bool)
{
}


void
BMidiSynth::GetSoloMap(char* pChannels) const
{
	if (pChannels != NULL)
		memset(pChannels, 0, 16);
}


status_t
BMidiSynth::LoadInstrument(int16 instrument)
{
	if (instrument < 0 || instrument > 127)
		return B_BAD_INSTRUMENT;
	return B_OK;
}


status_t
BMidiSynth::UnloadInstrument(int16)
{
	return B_OK;
}


status_t
BMidiSynth::RemapInstrument(int16, int16)
{
	return B_OK;
}


void
BMidiSynth::FlushInstrumentCache(bool)
{
}


uint32
BMidiSynth::Tick() const
{
	return (uint32)((system_time() - fCreationTime) / 1000);
}


void
BMidiSynth::NoteOff(uchar channel, uchar note, uchar velocity, uint32 time)
{
	// Path is spray into the midi2 façade; the graph carries it to the
	// BSoundFontSynth unit. No direct engine call from here.
	BMidi::NoteOff(channel, note, velocity, time);
}


void
BMidiSynth::NoteOn(uchar channel, uchar note, uchar velocity, uint32 time)
{
	BMidi::NoteOn(channel, note, velocity, time);
}


void
BMidiSynth::KeyPressure(uchar channel, uchar note, uchar pressure, uint32 time)
{
	BMidi::KeyPressure(channel, note, pressure, time);
}


void
BMidiSynth::ControlChange(uchar channel, uchar controlNumber,
	uchar controlValue, uint32 time)
{
	BMidi::ControlChange(channel, controlNumber, controlValue, time);
}


void
BMidiSynth::ProgramChange(uchar channel, uchar programNumber, uint32 time)
{
	BMidi::ProgramChange(channel, programNumber, time);
}


void
BMidiSynth::ChannelPressure(uchar channel, uchar pressure, uint32 time)
{
	BMidi::ChannelPressure(channel, pressure, time);
}


void
BMidiSynth::PitchBend(uchar channel, uchar lsb, uchar msb, uint32 time)
{
	BMidi::PitchBend(channel, lsb, msb, time);
}


void
BMidiSynth::AllNotesOff(bool justChannel, uint32 time)
{
	BMidi::AllNotesOff(justChannel, time);
}


void
BMidiSynth::Run()
{
	while (KeepRunning())
		snooze(20000);
}


void
BMidiSynth::_ReservedMidiSynth1()
{
}


void
BMidiSynth::_ReservedMidiSynth2()
{
}


void
BMidiSynth::_ReservedMidiSynth3()
{
}


void
BMidiSynth::_ReservedMidiSynth4()
{
}
