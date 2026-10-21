/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef _CORTEX_NODES_H
#define _CORTEX_NODES_H


#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MidiUnit.h>


// The MIDI out node is a plain BMidiUnit hosting one BMidiOutput.
BMidiUnit*		CreateCortexMidiOut(const char* name);
status_t		SendCortexTestNote(BMidiUnit* unit);


#endif
