/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Printers preflet on libcups. No Haiku print_server on V\OS.
 */

#include "PrintersApp.h"
#include "PrintersWindow.h"

#include <AppDefs.h>

#include "pr_server.h"


int
main()
{
	PrintersApp app;
	app.Run();
	return 0;
}


PrintersApp::PrintersApp()
	:
	BApplication(PRNT_SIGNATURE_TYPE)
{
}


void
PrintersApp::ReadyToRun()
{
	PrintersWindow* window = new PrintersWindow();
	window->Show();
}


void
PrintersApp::MessageReceived(BMessage* message)
{
	if (message->what == B_PRINTER_CHANGED
		|| message->what == PRINTERS_ADD_PRINTER) {
		for (int32 i = 0; i < CountWindows(); i++) {
			BMessenger messenger(NULL, WindowAt(i));
			messenger.SendMessage(message);
		}
		return;
	}

	BApplication::MessageReceived(message);
}
