/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MidiPlay.h"

#include <Entry.h>
#include <List.h>
#include <ObjectList.h>
#include <OS.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <media2/MediaConnection.h>
#include <media2/MediaGraph.h>
#include <media2/MediaUnit.h>
#include <media2/SoundFontSynth.h>
#include <midi/MidiStore.h>
#include <midi/Synth.h>
#include <midi2/MidiProducer.h>

#include <midi2/MidiProducerAccess.h>


static volatile bool sMidiInterrupt = false;


static void
midi_keyb_int(int)
{
	sMidiInterrupt = true;
}


// Link a synth unit's audio outs to the default device's input ports.
static void
link_synth_to_device(BSoundFontSynth* synth)
{
	media_client_id deviceId = 0;
	if (MediaGraph()->GetDefaultAudioOutput(&deviceId) != B_OK
		|| deviceId == 0)
		return;

	BObjectList<BMediaGraph::ForeignPort, true> ports;
	if (MediaGraph()->GetForeignPorts((uint32)deviceId, &ports) != B_OK)
		return;

	for (int32 i = 0; i < synth->CountOutputs(); i++) {
		BMediaOutput* output = synth->OutputAt(i);
		if (output == NULL)
			continue;
		for (int32 p = 0; p < ports.CountItems(); p++) {
			const BMediaGraph::ForeignPort* port = ports.ItemAt(p);
			if (port == NULL)
				continue;
			if (port->direction != "in")
				continue;
			if (MediaGraph()->Connect(output, port) == B_OK)
				break;
		}
	}
}


// Acceptance path (§12.N): BMidiStore parses the SMF, the midi2 façade
// schedules, BMidiOutput's ring crosses the RT boundary, BSoundFontSynth
// renders, and the graph links out to a device. No GStreamer midiparse,
// no fluid_player, no private parser in this file.
int
media_midi_play(const char* uri)
{
	if (uri == NULL)
		return 2;

	entry_ref ref;
	if (get_ref_for_path(uri, &ref) != B_OK) {
		fprintf(stderr, "media_midi_play: cannot resolve \"%s\"\n", uri);
		return 2;
	}

	// Synth node. InitCheck fails when no soundfont is found.
	BSoundFontSynth* synth = new BSoundFontSynth("midi synth", 2);
	if (synth == NULL) {
		fprintf(stderr, "media_midi_play: out of memory\n");
		return 2;
	}
	if (synth->InitCheck() != B_OK) {
		fprintf(stderr, "media_midi_play: no soundfont found, "
			"synth refuses to start\n");
		delete synth;
		return 2;
	}

	// Check Start()'s return value; an unchecked failure falling through to
	// a blocking wait is exactly the hang bug the audio path already had.
	status_t status = synth->Start();
	if (status != B_OK) {
		fprintf(stderr, "media_midi_play: couldn't start synth: %d\n",
			(int)status);
		delete synth;
		return 2;
	}

	link_synth_to_device(synth);

	BMidiStore* store = new BMidiStore();
	if (store == NULL) {
		synth->Stop();
		delete synth;
		return 2;
	}

	status = store->Import(&ref);
	if (status != B_OK) {
		fprintf(stderr, "media_midi_play: cannot parse \"%s\": %d\n",
			uri, (int)status);
		delete store;
		synth->Stop();
		delete synth;
		return 2;
	}

	// Link the store's midi2 producer output into the synth's MIDI input.
	// That is the §12.N chain: store timeline -> façade scheduler ->
	// BMidiOutput ring -> PipeWire -> BSoundFontSynth.
	BMidiLocalProducer* producer = store->Producer();
	BMidiOutput* output = producer != NULL
		? BPrivate::MidiProducerAccess::Output(producer) : NULL;
	if (output == NULL || synth->MidiInput() == NULL) {
		fprintf(stderr, "media_midi_play: cannot build MIDI path\n");
		delete store;
		synth->Stop();
		delete synth;
		return 2;
	}

	status = MediaGraph()->Connect(output, synth->MidiInput());
	if (status != B_OK) {
		fprintf(stderr, "media_midi_play: cannot link MIDI source to "
			"synth: %d\n", (int)status);
		delete store;
		synth->Stop();
		delete synth;
		return 2;
	}

	signal(SIGINT, midi_keyb_int);

	printf("Playing MIDI file through media2...\n");
	printf("  store events: %u\n", store->CountEvents());

	status = store->Start();
	if (status != B_OK) {
		fprintf(stderr, "media_midi_play: couldn't start MIDI playback: %d\n",
			(int)status);
		delete store;
		synth->Stop();
		delete synth;
		return 2;
	}

	// Wait for the run thread or interrupt. Never hang silently.
	while (store->IsRunning() && !sMidiInterrupt)
		snooze(50000);

	if (sMidiInterrupt)
		printf("Interrupted\n");

	store->Stop();
	printf("MIDI playback finished.\n");

	delete store;
	synth->Stop();
	delete synth;
	return 0;
}
