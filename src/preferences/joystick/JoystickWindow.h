/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Main window: device list on the left, live test panel on the right,
 * calibration sliders below. All polling happens on this window's
 * looper through a BMessageRunner.
 */
#ifndef _JOYSTICK_WINDOW_H
#define _JOYSTICK_WINDOW_H

#include <Joystick.h>
#include <String.h>
#include <Window.h>

#include "CalibrationSettings.h"

class BButton;
class BListView;
class BMessageRunner;
class BSlider;
class BStringView;
class TestPanel;


class JoystickWindow : public BWindow {
public:
								JoystickWindow();
	virtual						~JoystickWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
	virtual	void				Show();

private:
			void				_ScanDevices();
			void				_SelectDevice(const char* path);
			void				_SetStatus(const char* text);
			void				_Poll();
			void				_Notify(const char* title,
									const char* content);
			void				_UpdateCalibrationControls();

			BJoystick			fJoystick;
			CalibrationSettings	fCalibration;

			BListView*			fDeviceList;
			BStringView*		fStatusView;
			TestPanel*			fTestPanel;
			BSlider*			fDeadZoneSlider;
			BSlider*			fCurveSlider;
			BButton*			fRefreshButton;
			BMessageRunner*		fPollRunner;

			BString				fDevicePath;
			bool				fConnected;
			bool				fIgnoreSliders;
			bigtime_t			fLastReconnect;
};

#endif // _JOYSTICK_WINDOW_H
