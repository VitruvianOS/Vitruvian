/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NetworkShares.h"

#include "NetworkSharesWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NetworkShares"


NetworkShares::NetworkShares()
	:
	BApplication("application/x-vnd.vos-NetworkShares")
{
}


void
NetworkShares::ReadyToRun()
{
	NetworkSharesWindow* window = new NetworkSharesWindow();
	window->Show();
}


void
NetworkShares::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'NSAd':
		case 'NSOA':
		{
			// Tracker "Connect to server..." opens the Add dialog.
			for (int32 i = 0; i < CountWindows(); i++) {
				BMessenger messenger(NULL, WindowAt(i));
				messenger.SendMessage(message);
			}
			return;
		}

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


int
main()
{
	NetworkShares app;
	app.Run();
	return 0;
}
