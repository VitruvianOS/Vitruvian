/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Double-click entry point: a small window hosting the replicant view.
// The Deskbar loads the same binary through instantiate_deskbar_item().

#include <Application.h>
#include <Window.h>

#include "PhoneSignalView.h"


int
main()
{
	BApplication* app = new BApplication(PhoneSignalView::kAppSignature);

	BWindow* window = new BWindow(BRect(120, 120, 360, 180),
		"Phone signal", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS);
	window->AddChild(new PhoneSignalView(window->Bounds(),
		B_FOLLOW_ALL_SIDES, false));
	window->Show();

	app->Run();
	delete app;
	return 0;
}
