/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Joystick preferences application for V\OS.
 */

#include "JoystickWindow.h"

#include <Application.h>


static const char* kSignature = "application/x-vnd.Haiku-Joystick";


class Application : public BApplication {
public:
								Application();

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
	JoystickWindow* window = new JoystickWindow();
	if (window != NULL)
		window->Show();
	else
		PostMessage(B_QUIT_REQUESTED);
}


int
main()
{
	Application* app = new Application();
	app->Run();
	delete app;
	return 0;
}
