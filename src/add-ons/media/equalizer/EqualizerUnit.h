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


class EqualizerUnit : public BMediaUnit {
public:
							EqualizerUnit(int32 channels = 2);
	virtual					~EqualizerUnit();

			status_t		InitCheck() const;

			// Property ids for the published parameters.
	enum {
		kPropPreAmp	= B_MEDIA_PROP_CUSTOM_BASE + 101,
		kPropMute	= B_MEDIA_PROP_CUSTOM_BASE + 102,
		kPropByPass	= B_MEDIA_PROP_CUSTOM_BASE + 103,
		kPropBands	= B_MEDIA_PROP_CUSTOM_BASE + 300
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
			BMediaInput*	fInputs[MAX_CHANNELS];
			BMediaOutput*	fOutputs[MAX_CHANNELS];
			BControllable*	fControllable;
			std::atomic<uint32>	fConfiguredRate;
			std::atomic<int32>	fMute;
			std::atomic<int32>	fByPass;
			status_t		fInitStatus;
};


#endif //__EQUALIZER_UNIT_H__
