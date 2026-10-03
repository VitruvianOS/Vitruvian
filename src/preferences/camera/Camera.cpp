/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>
#include <Window.h>

#include "CameraWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Camera"


// Matches the Media/Bluetooth preflet signature pattern.
static const char* kSignature = "application/x-vnd.vos-Camera";


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
	CameraWindow* window = new CameraWindow();
	window->Show();
}


int
main(int /*argc*/, char** /*argv*/)
{
	Application* app = new Application();
	app->Run();
	delete app;
	return 0;
}
