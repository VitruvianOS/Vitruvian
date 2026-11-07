/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// The Mixer page: one channel strip per output device, input device and
// application stream, in the spirit of the legacy mixer.

#ifndef MIXER_SECTION_VIEW_H
#define MIXER_SECTION_VIEW_H


#include <GroupView.h>

#include <vector>


class ChannelStripView;
class BScrollView;
class BGroupView;


class MixerSectionView : public BGroupView {
public:
							MixerSectionView();
	virtual					~MixerSectionView();

	virtual	void			MessageReceived(BMessage* message);

			void			Refresh();
			void			HintAll();

private:
			void			_ClearStrips();

			BScrollView*	fScroller;
			BGroupView*		fRow;
			std::vector<ChannelStripView*>	fStrips;
};


#endif
