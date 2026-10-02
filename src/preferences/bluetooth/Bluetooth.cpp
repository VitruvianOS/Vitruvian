/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Application.h>
#include <Window.h>

#include "BluetoothWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Bluetooth"


// Existing signature; do not change.
static const char* kSignature = "application/x-vnd.Haiku-Bluetooth";


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
	BluetoothWindow* window = new BluetoothWindow();
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
