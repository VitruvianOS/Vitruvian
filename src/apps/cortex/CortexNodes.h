/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CORTEX_NODES_H
#define _CORTEX_NODES_H


#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MidiUnit.h>
#include <media2/SimpleMediaUnit.h>


class CortexToneControllable;


class CortexToneOutput : public BMediaOutput {
public:
								CortexToneOutput(const char* name);

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format);
	virtual	status_t			FormatProposal(BMediaFormat* format);
};


// A sine generator: a simple unit whose cycle hook fills the output.
class CortexToneUnit : public BSimpleMediaUnit {
public:
								CortexToneUnit(const char* name);
	virtual						~CortexToneUnit();

			CortexToneControllable*	ControllableInstance() const;
			void				SetGain(float gain);

private:
	static	void				_Cycle(void* cookie, BSimpleMediaUnit* unit,
									uint32 frameCount);

			CortexToneOutput*	fOutput;
			CortexToneControllable*	fControllable;
			float				fGain;
			float				fPhase;
};


// The MIDI out node is a plain BMidiUnit hosting one BMidiOutput.
BMidiUnit*		CreateCortexMidiOut(const char* name);
status_t		SendCortexTestNote(BMidiUnit* unit);


BMediaClient** CreateCortexDemoClients(int32* outCount);


#endif
