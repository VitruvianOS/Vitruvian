/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>

#include "HardwareWindow.h"


static const char* kAppSignature = "application/x-vnd.vos-hardware-prefs";


int
main()
{
	BApplication app(kAppSignature);

	HardwareWindow* window = new HardwareWindow();
	window->Show();

	app.Run();
	return 0;
}
