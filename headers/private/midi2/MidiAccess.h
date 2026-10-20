/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MIDI_ACCESS_H
#define _MIDI_ACCESS_H

#include <midi/Midi.h>
#include <MidiProducer.h>


class BMidiOutput;

namespace BPrivate {

// In-tree access to the media2 objects behind the legacy MIDI classes.
class MidiAccess {
public:
	static BMidiLocalProducer* Producer(const BMidi* midi)
		{ return midi->fProducer; }
	static BMidiOutput* Output(const BMidiProducer* producer)
		{ return producer->fOutput; }
};

}	// namespace BPrivate

#endif	// _MIDI_ACCESS_H
