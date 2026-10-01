/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef LOCATION_WINDOW_H
#define LOCATION_WINDOW_H

#include <Window.h>

#include "LocationProvider.h"
#include "LocationSettings.h"
#include "LocationSettingsView.h"


class LocationWindow : public BWindow {
public:
								LocationWindow();
	virtual						~LocationWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			LocationSettings	fSettings;
			LocationProvider*	fProvider;
			LocationSettingsView* fSettingsView;
};


#endif	// LOCATION_WINDOW_H
