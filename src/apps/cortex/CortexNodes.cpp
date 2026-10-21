/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexNodes.h"

#include <new>


BMidiUnit*
CreateCortexMidiOut(const char* name)
{
	BMidiUnit* unit = new(std::nothrow) BMidiUnit(name, B_MEDIA_PLAYER);
	if (unit == NULL)
		return NULL;

	BMidiOutput* output = new(std::nothrow) BMidiOutput("midi out");
	if (output != NULL) {
		BMediaFormat format;
		format.format.type = B_MEDIA_MIDI;
		output->SetAcceptedFormat(format);
		unit->RegisterOutput(output);
	}
	return unit;
}


status_t
SendCortexTestNote(BMidiUnit* unit)
{
	BMidiOutput* output = unit != NULL ? unit->MidiOutputAt(0) : NULL;
	if (output == NULL)
		return B_NO_INIT;

	status_t status = output->SendNoteOn(0, 60, 100, 0);
	if (status != B_OK)
		return status;
	return output->SendNoteOff(0, 60, 100, 500000);
}
