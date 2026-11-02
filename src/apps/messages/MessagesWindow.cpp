/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MessagesWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <LayoutBuilder.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MessagesWindow"


MessagesWindow::MessagesWindow(const BRect& frame, MessageStore* store)
	:
	BWindow(frame, B_TRANSLATE("Messages"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS)
{
	fMessagesView = new MessagesView("messages", store);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMessagesView);
	CenterOnScreen();
}


bool
MessagesWindow::QuitRequested()
{
	// Forward so the whole Messages team exits.
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MessagesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStoreChanged:
		case kMsgLinesChanged:
			PostMessage(message, fMessagesView);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
MessagesWindow::Refresh()
{
	if (LockLooper()) {
		fMessagesView->Refresh();
		UnlockLooper();
	}
}
