/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "DialerWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <LayoutBuilder.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DialerWindow"


DialerWindow::DialerWindow(const BRect& frame)
	:
	BWindow(frame, B_TRANSLATE("Phone"), B_TITLED_WINDOW,
		B_AVOID_FRONT | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	fDialerView = new DialerView("dialer");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fDialerView);

	ResizeToPreferred();
	CenterOnScreen();
}


bool
DialerWindow::QuitRequested()
{
	// Forward so the whole Phone team exits.
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
DialerWindow::AppendKeypadKey(char key)
{
	fDialerView->AppendKeypadKey(key);
}


void
DialerWindow::RefreshLines()
{
	fDialerView->RefreshLines();
}


const char*
DialerWindow::SelectedLine()
{
	return fDialerView->SelectedLine();
}
