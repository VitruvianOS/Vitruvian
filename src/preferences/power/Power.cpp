/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Application.h>

#include "PowerWindow.h"


static const char* kAppSignature = "application/x-vnd.Haiku-Power";


int
main()
{
	BApplication app(kAppSignature);

	PowerWindow* window = new PowerWindow();
	window->Show();

	app.Run();
	return 0;
}
