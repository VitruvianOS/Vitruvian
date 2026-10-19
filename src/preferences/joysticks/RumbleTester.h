/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Force-feedback rumble test over the evdev node. The tree ships no
 * hidraw layer, so the kernel's FF upload/play ioctls are the only
 * path; they cover USB and Bluetooth gamepads alike.
 */
#ifndef _RUMBLE_TESTER_H
#define _RUMBLE_TESTER_H


#include <SupportDefs.h>


class RumbleTester {
public:
								RumbleTester();
								~RumbleTester();

			status_t		SetDevice(const char* devicePath);
			void			UnsetDevice();

			bool			IsSupported() const;

			status_t		Trigger(uint32 durationMs, uint16 strong,
									uint16 weak);

private:
			int				fFD;
			int32			fEffectID;
};


#endif // _RUMBLE_TESTER_H
