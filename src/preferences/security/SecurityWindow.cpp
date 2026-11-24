/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SecurityWindow.h"

#include "FirewallView.h"
#include "SecurityMessages.h"

#include <Catalog.h>
#include <Application.h>
#include <LayoutBuilder.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Security"


SecurityWindow::SecurityWindow()
	:
	BWindow(BRect(80, 80, 640, 620), B_TRANSLATE("Security"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fFirewallView(NULL)
{
	_InitWindow();
	CenterOnScreen();
}


bool
SecurityWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
SecurityWindow::_InitWindow()
{
	fFirewallView = new FirewallView();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fFirewallView);
}
