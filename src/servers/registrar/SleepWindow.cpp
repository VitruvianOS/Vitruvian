/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "SleepWindow.h"

#include <Button.h>
#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SleepWindow"


SleepWindow::SleepWindow(bool hibernate)
	:
	BAlert()
{
	SetTitle(B_TRANSLATE("Sleep status"));
	SetWorkspaces(B_ALL_WORKSPACES);
	SetLook(B_TITLED_WINDOW_LOOK);
	SetFlags(Flags() | B_NOT_CLOSABLE | B_NOT_MINIMIZABLE | B_NOT_ZOOMABLE);
	SetType(B_INFO_ALERT);
	SetText(hibernate ? B_TRANSLATE("System is hibernating" B_UTF8_ELLIPSIS)
		: B_TRANSLATE("System is suspending" B_UTF8_ELLIPSIS));

	AddButton(B_TRANSLATE("OK"));
	ButtonAt(0)->Hide();
}


void
SleepWindow::MessageReceived(BMessage* message)
{
	if (message->what != kMsgSleepFailed) {
		BAlert::MessageReceived(message);
		return;
	}

	SetType(B_STOP_ALERT);
	SetText(message->GetString("text", ""));
	ButtonAt(0)->Show();
}
