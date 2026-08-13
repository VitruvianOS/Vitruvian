/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Locale.h>
#include <Window.h>

#include "BluetoothWindow.h"


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


// #pragma mark -


int
main()
{
	Application* app = new Application();
	app->Run();
	delete app;
	return 0;
}