/*
 * Copyright 2026, Vitruvian Project.
 * Popup window shown on left-click of AudioMixer replicant.
 * Distributed under the terms of the MIT License.
 */

#ifndef VOLUME_POPUP_VIEW_H
#define VOLUME_POPUP_VIEW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BCheckBox;
class BSlider;
class AudioMixerView;


class VolumePopupView : public BWindow {
public:
							VolumePopupView(AudioMixerView* target,
								BPoint anchor, float initialVolume,
								bool initialMute, const BString& deviceName);
	virtual					~VolumePopupView();

	virtual	void			MessageReceived(BMessage* message);
	virtual	bool			QuitRequested();
	virtual	void			WindowActivated(bool active);

private:




		BMessenger		fTarget;
		BSlider*		fSlider;
		BCheckBox*		fMuteBox;
		BString			fDeviceName;
		bool			fDragging;
		int32			fUpdatedCount;




		bool			fClosePending;
		void			_RequestClose();
};


#endif
