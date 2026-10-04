/*
 * Copyright 2003-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval, jerome.duval@free.fr
 *		Michael Phipps
 */


#include <Application.h>
#include <Entry.h>
#include <Path.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ScreenSaverWindow.h"


class ScreenSaverApp : public BApplication {
public:
	ScreenSaverApp();

private:
	BWindow* fScreenSaverWindow;
};


//	#pragma mark - ScreenSaverApp


ScreenSaverApp::ScreenSaverApp()
	:
	BApplication("application/x-vnd.vos-ScreenSaver")
{
	fScreenSaverWindow = new ScreenSaverWindow();
	fScreenSaverWindow->Show();
}


//	#pragma mark - main()


int
main()
{
	ScreenSaverApp app;
	app.Run();
	return 0;
}
