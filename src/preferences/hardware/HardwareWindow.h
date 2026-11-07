/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HARDWARE_WINDOW_H
#define _HARDWARE_WINDOW_H


#include <Window.h>


class HardwareView;


class HardwareWindow : public BWindow {
public:
							HardwareWindow();

	virtual	bool			QuitRequested();

private:
			HardwareView*	fView;
};


#endif	// _HARDWARE_WINDOW_H
