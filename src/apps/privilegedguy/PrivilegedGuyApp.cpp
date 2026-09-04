/*
 * Copyright 2026, Vitruvian. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "PrivilegedGuyApp.h"

#include "MainWindow.h"


int
main(int, char**)
{
	PrivilegedGuyApp app;
	app.Run();
	return 0;
}


PrivilegedGuyApp::PrivilegedGuyApp()
	:
	BApplication("application/x-vnd.Vitruvian-PrivilegedGuy")
{
}


void
PrivilegedGuyApp::ReadyToRun()
{
	new MainWindow();
}
