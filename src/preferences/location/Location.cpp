/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Location"


#include <Application.h>
#include <Catalog.h>
#include <Locale.h>
#include <Window.h>

#include "LocationWindow.h"


static const char* kSignature = "application/x-vnd.vos-Location";


class Application : public BApplication {
public:
								Application();

public:
	virtual	void				ReadyToRun();
};


Application::Application()
	:
	BApplication(kSignature)
{
}


void
Application::ReadyToRun()
{
	LocationWindow* window = new LocationWindow();
	window->Show();
}


// #pragma mark -


int
main()
{
	Application* app = new Application();
	app->Run();
	delete app;
	return 0;
}
