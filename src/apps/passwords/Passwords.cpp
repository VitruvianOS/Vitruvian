/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PasswordsView.h"

#include <Application.h>
#include <Window.h>


const char* kPasswordsSignature = "application/x-vnd.Vitruvian-Passwords";


int
main(int argc, char* argv[])
{
	BApplication app(kPasswordsSignature);

	BWindow* window = new BWindow(BRect(80, 80, 920, 640), "Passwords",
		B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE);
	window->AddChild(new PasswordsView());
	window->Show();

	app.Run();
	return 0;
}
