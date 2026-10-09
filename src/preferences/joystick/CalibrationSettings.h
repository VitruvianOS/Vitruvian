/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Per-device dead zone and stick curve, stored under the user settings
 * directory. evdev already normalizes axis ranges, so all the classic
 * game-port calibration had left to offer is this pair of shaping knobs.
 */
#ifndef _CALIBRATION_SETTINGS_H
#define _CALIBRATION_SETTINGS_H


#include <SupportDefs.h>


struct CalibrationSettings {
	float			deadZone;
								// 0.0 .. 0.5, fraction of the axis range
	float			curve;
								// 0.0 .. 1.0, linear to cubic blend

								CalibrationSettings();

			status_t		Load(const char* devicePath);
			status_t		Save(const char* devicePath) const;

			float			Apply(int16 raw) const;
								// raw [-32768,32767] to [-1, 1] shaped
};


#endif // _CALIBRATION_SETTINGS_H
