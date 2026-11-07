/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Raw ALSA mixer enumeration for the Hardware page. This is the
// documented fallback for controls PipeWire does not surface through
// device params; everything shown through it is labelled as raw.

#ifndef ALSA_CONTROLS_H
#define ALSA_CONTROLS_H


#include <String.h>

#include <vector>


class AlsaControls {
public:
	struct Control {
		int			cardIndex;
		BString		cardName;
		BString		name;
		bool		isEnum;
		bool		isBoolean;
		int32		enumIndex;
		std::vector<BString> enumNames;
		float		value;		// 0..1 across the element range
		bool		muted;
		bool		isPlayback;
	};

			status_t		Fetch(std::vector<Control>& outControls);
			status_t		Apply(const Control& control, float value,
								bool mute);
};


#endif
