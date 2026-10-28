/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "MainApp.h"

#include <Rect.h>

#include "MainWin.h"


MainApp::MainApp()
	:
	BApplication("application/x-vnd.vos-TV")
{
	BRect rect(200, 200, 750, 400);
	fMainWindow = new MainWin(rect);
	fMainWindow->Show();
}


MainApp::~MainApp()
{
}


int
main()
{
	MainApp app;
	app.Run();
	return 0;
}
