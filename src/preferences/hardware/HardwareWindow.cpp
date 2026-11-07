/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "HardwareWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <GroupLayout.h>

#include "HardwareView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Hardware"


HardwareWindow::HardwareWindow()
	:
	BWindow(BRect(80, 80, 820, 620), B_TRANSLATE("Hardware"),
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
		| B_NOT_ZOOMABLE)
{
	fView = new HardwareView();
	SetLayout(new BGroupLayout(B_VERTICAL));
	AddChild(fView);

	CenterOnScreen();
}


bool
HardwareWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
