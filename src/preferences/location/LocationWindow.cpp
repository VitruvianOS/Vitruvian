/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "LocationWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Message.h>
#include <Messenger.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Location"


LocationWindow::LocationWindow()
	:
	BWindow(BRect(100, 100, 540, 600), B_TRANSLATE("Location"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fProvider(NULL),
	fSettingsView(NULL)
{
	fSettings.LoadSettings();

	fProvider = new LocationProvider(fSettings.Host().String(),
		fSettings.Port());
	fProvider->SetAccuracyLevel(fSettings.AccuracyLevel());
	fProvider->Run();

	// One page, no tab bar: there is nothing to switch to.
	fSettingsView = new LocationSettingsView("locationPage", fSettings,
		*fProvider);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fSettingsView)
	.End();

	CenterOnScreen();

	if (fSettings.Enabled())
		fProvider->Start();
}


LocationWindow::~LocationWindow()
{
	if (fProvider != NULL) {
		if (fSettingsView != NULL)
			fProvider->Unwatch(BMessenger(fSettingsView));
		// Quit() must run with the looper locked; it deletes the looper
		// on its own thread once teardown finishes.
		fProvider->Lock();
		fProvider->Quit();
		fProvider = NULL;
	}
}


void
LocationWindow::MessageReceived(BMessage* message)
{
	BWindow::MessageReceived(message);
}


bool
LocationWindow::QuitRequested()
{
	fSettings.SaveSettings();
	if (fProvider != NULL) {
		if (fSettingsView != NULL)
			fProvider->Unwatch(BMessenger(fSettingsView));
		fProvider->Lock();
		fProvider->Quit();
		fProvider = NULL;
	}
	return true;
}
