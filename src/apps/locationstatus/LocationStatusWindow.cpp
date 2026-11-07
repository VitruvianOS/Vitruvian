/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "LocationStatusWindow.h"
#include "LocationStatusView.h"

#include <Application.h>
#include <Catalog.h>
#include <Locale.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LocationStatusWindow"


LocationStatusWindow::LocationStatusWindow()
	:
	BWindow(BRect(100, 100, 280, 160), B_TRANSLATE("Location"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_ASYNCHRONOUS_CONTROLS)
{
	LocationStatusView* view = new LocationStatusView(Bounds(),
		B_FOLLOW_ALL, false);
	AddChild(view);

	CenterOnScreen();
}


LocationStatusWindow::~LocationStatusWindow()
{
}


bool
LocationStatusWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
