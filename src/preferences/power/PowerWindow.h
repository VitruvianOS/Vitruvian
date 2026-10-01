/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef POWER_WINDOW_H
#define POWER_WINDOW_H


#include <Window.h>


class PowerWindow : public BWindow {
public:
								PowerWindow();
	virtual						~PowerWindow();

	virtual bool				QuitRequested();
};


#endif	// POWER_WINDOW_H
