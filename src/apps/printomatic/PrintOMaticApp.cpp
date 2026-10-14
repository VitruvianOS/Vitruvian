/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * PrintOMatic: the V\OS scan app. Scan, then save, print or copy.
 */

#include "PrintOMaticApp.h"

#include <stdio.h>

#include "PrintOMaticWindow.h"


int
main()
{
	PrintOMaticApp app;
	app.Run();
	return 0;
}


PrintOMaticApp::PrintOMaticApp()
	:
	BApplication("application/x-vnd.vos-PrintOMatic"),
	fWindow(NULL)
{
}


void
PrintOMaticApp::ReadyToRun()
{
	// Reuse an existing window when launched again with a scanner name.
	if (fWindow != NULL) {
		fWindow->Show();
		fWindow->Activate();
		return;
	}

	fWindow = new PrintOMaticWindow(BRect(80, 80, 800, 620), "");
	fWindow->Show();
}


void
PrintOMaticApp::ArgvReceived(int32 argc, char** argv)
{
	if (argc < 2 || argv[1] == NULL)
		return;
	_OpenWithScanner(argv[1]);
}


void
PrintOMaticApp::RefsReceived(BMessage* /*message*/)
{
	// Files dropped on the icon are not meaningful; ignore.
}


void
PrintOMaticApp::_OpenWithScanner(const char* name)
{
	if (fWindow == NULL)
		fWindow = new PrintOMaticWindow(BRect(80, 80, 800, 620), name);
	else
		fWindow->SelectScanner(name);

	fWindow->Show();
	fWindow->Activate();
}
