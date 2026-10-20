/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _MIDI_PRODUCER_ACCESS_H
#define _MIDI_PRODUCER_ACCESS_H

#include <MidiProducer.h>


class BMidiOutput;

namespace BPrivate {

// In-tree access to the media2 output behind a midi2 producer.
class MidiProducerAccess {
public:
	static BMidiOutput* Output(const BMidiProducer* producer)
		{ return producer->fOutput; }
};

}	// namespace BPrivate

#endif	// _MIDI_PRODUCER_ACCESS_H
