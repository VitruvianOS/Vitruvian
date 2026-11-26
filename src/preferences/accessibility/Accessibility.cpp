/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Accessibility preferences application entry point.
 */


#include <Application.h>

#include "AccessibilityWindow.h"


static const char* kAppSignature = "application/x-vnd.vos-Accessibility";


int
main()
{
	BApplication app(kAppSignature);

	AccessibilityWindow* window = new AccessibilityWindow();
	window->Show();

	app.Run();
	return 0;
}
