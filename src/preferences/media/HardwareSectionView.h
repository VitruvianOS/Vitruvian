/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// The Hardware page: profiles, routes, every PipeWire device param, and
// the raw ALSA mixer elements PipeWire does not surface.

#ifndef HARDWARE_SECTION_VIEW_H
#define HARDWARE_SECTION_VIEW_H


#include <GroupView.h>
#include <String.h>

#include <media2/MediaClientDefs.h>

#include <vector>

#include <media2/Controllable.h>

#include "AlsaControls.h"


class BListView;
class BPopUpMenu;
class BStringView;
class BScrollView;
class BSlider;
class BCheckBox;
class BView;


class HardwareSectionView : public BGroupView {
public:
							HardwareSectionView();
	virtual					~HardwareSectionView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

			void			RefreshDevices();
			void			RefreshProfiles();
			void			NotifyChanged(uint32 what, uint32 deviceId);

private:
	enum {
		kMsgAlsaValue = 'MSav',
		kMsgAlsaMute  = 'MSam'
	};

			void			_BuildPipeWireParams();
			void			_BuildAlsaControls();
			void			_ClearAlsaRows();

			BPopUpMenu*		fDeviceMenu;
			BListView*		fProfileList;
			BStringView*	fStatus;
			BScrollView*	fParamScroll;
			BView*			fParamView;
			BGroupView*		fAlsaGroup;
			BScrollView*	fAlsaScroll;

			media_client_id	fSelectedDevice;
			int32			fActiveProfile;
			std::vector<int32>	fProfileIndices;

			BControllable	fDeviceControllable;
			AlsaControls	fAlsa;
			std::vector<AlsaControls::Control>	fAlsaList;
};


#endif
