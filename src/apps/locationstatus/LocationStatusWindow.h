/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef LOCATION_STATUS_WINDOW_H
#define LOCATION_STATUS_WINDOW_H


#include <Window.h>


class LocationStatusView;


class LocationStatusWindow : public BWindow {
public:
						LocationStatusWindow();
	virtual				~LocationStatusWindow();

	virtual	bool		QuitRequested();
};


#endif	// LOCATION_STATUS_WINDOW_H
