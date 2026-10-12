/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _CORTEX_NODES_H
#define _CORTEX_NODES_H


#include <media2/MediaClient.h>
#include <media2/MediaConnection.h>
#include <media2/MediaUnit.h>
#include <media2/MidiUnit.h>


class CortexToneControllable;


class CortexToneOutput : public BMediaOutput {
public:
								CortexToneOutput(const char* name);

protected:
	virtual	status_t			PrepareToConnect(BMediaFormat* format);
	virtual	status_t			FormatProposal(BMediaFormat* format);
};


class CortexToneUnit : public BMediaUnit {
public:
								CortexToneUnit(const char* name);
	virtual						~CortexToneUnit();

			CortexToneControllable*	ControllableInstance() const;
			void				SetGain(float gain);

protected:
	virtual	void				ProcessCycle(uint32 frameCount);

private:
			CortexToneOutput*	fOutput;
			CortexToneControllable*	fControllable;
			float				fGain;
			float				fPhase;
};


class CortexMidiOutputUnit : public BMidiUnit {
public:
								CortexMidiOutputUnit(const char* name);
	virtual						~CortexMidiOutputUnit();

			status_t			SendTestNote();

private:
			BMidiOutput*		fOutput;
};


BMediaClient** CreateCortexDemoClients(int32* outCount);


#endif
