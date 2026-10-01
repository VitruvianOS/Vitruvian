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
#include <TabView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Location"


LocationWindow::LocationWindow()
	:
	BWindow(BRect(80, 80, 480, 620), B_TRANSLATE("Location"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fProvider(NULL),
	fSettingsView(NULL)
{
	fSettings.LoadSettings();

	fProvider = new LocationProvider(fSettings.Host().String(),
		fSettings.Port());
	fProvider->SetAccuracyLevel(fSettings.AccuracyLevel());
	fProvider->Run();

	fSettingsView = new LocationSettingsView("locationPage", fSettings,
		*fProvider);
	fSettingsView->SetName(B_TRANSLATE("Location"));

	BTabView* tabView = new BTabView("tabs", B_WIDTH_FROM_LABEL);
	tabView->AddTab(fSettingsView);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(tabView)
	.End();

	if (fSettings.Enabled())
		fProvider->Start();
}


LocationWindow::~LocationWindow()
{
	if (fProvider != NULL) {
		fProvider->Quit();
		// Quit() deletes the looper on its own thread; drop our ref only
		// after it has finished tearing down.
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
		fProvider->Stop();
		fProvider->Quit();
		fProvider = NULL;
	}
	return true;
}
