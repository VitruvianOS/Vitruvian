/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MIDI_UNIT_H
#define _MEDIA2_MIDI_UNIT_H


#include <media2/MediaConnection.h>
#include <media2/MediaFormat.h>
#include <media2/MediaUnit.h>


class BMidiUnit : public BMediaUnit {
public:
									BMidiUnit(const char* name,
										media_client_kinds kinds);
	virtual							~BMidiUnit();

	virtual	void					HandleStop(bigtime_t performanceTime) override;
	virtual	void					HandleSeek(bigtime_t mediaTime,
										bigtime_t performanceTime) override;

			BMidiInput*				MidiInputAt(int32 index) const;
			BMidiOutput*			MidiOutputAt(int32 index) const;
			int32					CountMidiInputs() const;
			int32					CountMidiOutputs() const;
};


#endif
