/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef __EQUALIZER_UNIT_H__
#define __EQUALIZER_UNIT_H__

#include <atomic>

#include <media2/Controllable.h>
#include <media2/MediaConnection.h>
#include <media2/MediaUnit.h>

#include "Equalizer.h"


class EqualizerInput : public BMediaInput {
public:
							EqualizerInput(const char* name)
								:
								// Virtual base is initialized by the
								// most-derived class.
								BMediaConnection(B_MEDIA_INPUT, name),
								BMediaInput(name)
							{}
protected:
	virtual	status_t		AcceptFormat(BMediaFormat*) { return B_OK; }
};


class EqualizerOutput : public BMediaOutput {
public:
							EqualizerOutput(const char* name)
								:
								BMediaConnection(B_MEDIA_OUTPUT, name),
								BMediaOutput(name)
							{}
protected:
	virtual	status_t		PrepareToConnect(BMediaFormat*) { return B_OK; }
	virtual	status_t		FormatProposal(BMediaFormat*) { return B_OK; }
};


class EqualizerUnit : public BMediaUnit {
public:
							EqualizerUnit(int32 channels = 2);
	virtual					~EqualizerUnit();

			status_t		InitCheck() const;

			// SPA prop ids for the published parameters.
	enum {
		kPropPreAmp	= 0x1000000 + 101,
		kPropMute	= 0x1000000 + 102,
		kPropByPass	= 0x1000000 + 103,
		kPropBands	= 0x1000000 + 300
	};

protected:
	virtual	void			ProcessCycle(uint32 frameCount) override;
	virtual	status_t		Start() override;

private:
			void			_ApplyParam(int32 id, const void* value, size_t size);
			void			_PublishAndBind();
	static	void			_ChangeHook(int32 id, bigtime_t when,
								const void* value, size_t size, void* cookie);

			int32			fChannelCount;
			Equalizer		fEqualizer[MAX_CHANNELS];
			EqualizerInput*	fInputs[MAX_CHANNELS];
			EqualizerOutput*	fOutputs[MAX_CHANNELS];
			BControllable*	fControllable;
			std::atomic<uint32>	fConfiguredRate;
			std::atomic<int32>	fMute;
			std::atomic<int32>	fByPass;
			status_t		fInitStatus;
};


#endif //__EQUALIZER_UNIT_H__
