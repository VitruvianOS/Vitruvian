/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef CAMERA_WINDOW_H
#define CAMERA_WINDOW_H


#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ListView.h>
#include <Message.h>
#include <Slider.h>
#include <StringView.h>
#include <Window.h>

#include <media2/CameraSource.h>

#include "CameraPreviewView.h"


class CameraDeviceListItem;


class CameraWindow : public BWindow {
public:
								CameraWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_BuildInterface();
			void				RefreshDevices();
			void				_SelectDevice(media_client_id id);
			void				_UpdateControls();
			void				_SetEmptyState(bool empty);
			void				_SaveDeviceSettings();

			BListView*			fDeviceList;
			BScrollView*		fDeviceScroll;
			CameraPreviewView*	fPreview;
			BStringView*		fStatusLabel;
			BStringView*		fEmptyView;
			BBox*				fControlsBox;
			BButton*			fDefaultButton;
			BButton*			fResetButton;
			BCheckBox*			fMirrorCheck;
			BSlider*			fBrightnessSlider;
			BSlider*			fContrastSlider;
			BSlider*			fExposureSlider;
			BSlider*			fWhiteBalanceSlider;

			BCameraSource*		fCamera;
			media_client_id		fSelectedId;
			BString				fSelectedStableId;
};


#endif
