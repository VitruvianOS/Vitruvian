/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PowerWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Screen.h>
#include <StringView.h>

#include "PowerView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PowerWindow"


PowerWindow::PowerWindow()
	:
	BWindow(BRect(100, 100, 450, 400), B_TRANSLATE("Power"),
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_NOT_RESIZABLE
		| B_AUTO_UPDATE_SIZE_LIMITS)
{
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(PowerView::Create())
		.SetInsets(B_USE_WINDOW_SPACING);

	CenterOnScreen();
}


PowerWindow::~PowerWindow()
{
}


bool
PowerWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
