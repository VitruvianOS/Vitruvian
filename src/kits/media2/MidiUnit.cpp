/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/MidiUnit.h>
#include <media2/MediaConnection.h>
#include <media2/MediaClientDefs.h>


BMidiUnit::BMidiUnit(const char* name, media_client_kinds kinds)
	:
	BMediaUnit(name, kinds)
{
}


BMidiUnit::~BMidiUnit()
{
}


void
BMidiUnit::HandleStop(bigtime_t performanceTime)
{
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMidiOutput* output = dynamic_cast<BMidiOutput*>(OutputAt(i));
		if (output != NULL)
			output->AllNotesOff(performanceTime);
	}
}


void
BMidiUnit::HandleSeek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMidiOutput* output = dynamic_cast<BMidiOutput*>(OutputAt(i));
		if (output != NULL)
			output->ChaseState(performanceTime);
	}
}


int32
BMidiUnit::CountMidiInputs() const
{
	int32 count = 0;
	for (int32 i = 0; i < CountInputs(); i++) {
		BMediaInput* input = InputAt(i);
		if (input != NULL && (input->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)))
			count++;
	}
	return count;
}


int32
BMidiUnit::CountMidiOutputs() const
{
	int32 count = 0;
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output != NULL && (output->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI)))
			count++;
	}
	return count;
}


BMidiInput*
BMidiUnit::MidiInputAt(int32 index) const
{
	int32 currentIndex = 0;
	for (int32 i = 0; i < CountInputs(); i++) {
		BMediaInput* input = InputAt(i);
		if (input != NULL && (input->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI))) {
			if (currentIndex == index)
				return dynamic_cast<BMidiInput*>(input);
			currentIndex++;
		}
	}
	return NULL;
}


BMidiOutput*
BMidiUnit::MidiOutputAt(int32 index) const
{
	int32 currentIndex = 0;
	for (int32 i = 0; i < CountOutputs(); i++) {
		BMediaOutput* output = OutputAt(i);
		if (output != NULL && (output->AcceptedTypes() & B_MEDIA_TYPE_BIT(B_MEDIA_MIDI))) {
			if (currentIndex == index)
				return dynamic_cast<BMidiOutput*>(output);
			currentIndex++;
		}
	}
	return NULL;
}
