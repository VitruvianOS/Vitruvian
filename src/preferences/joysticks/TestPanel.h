/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Live per-device test panel: axis meters, button indicators, hat
 * readout and a rumble trigger button.
 */
#ifndef _TEST_PANEL_H
#define _TEST_PANEL_H

#include <View.h>

#include <vector>

class BButton;
class BStringView;
class BJoystick;
class AxisMeterView;
class ButtonGridView;
class CalibrationSettings;
class RumbleTester;


class TestPanel : public BView {
public:
								TestPanel();

	virtual	void				MessageReceived(BMessage* message);

			void				SetDevice(BJoystick* joystick,
									const char* devicePath);
								// rebuild rows for the opened device

			void				Refresh(BJoystick* joystick,
									const CalibrationSettings& calibration);

private:
			void				_UpdateInfo(BJoystick* joystick);

			AxisMeterView*		fAxisView;
			ButtonGridView*		fButtonView;
			BStringView*		fInfoView;
			BStringView*		fHatView;
			BButton*			fRumbleButton;

			RumbleTester*		fRumble;
			std::vector<char>	fButtonStates;
			std::vector<float>	fAxisValues;
};

#endif // _TEST_PANEL_H
