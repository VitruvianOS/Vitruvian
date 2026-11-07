/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PasswordsView.h"

#include <Application.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Passwords"


const char* kPasswordsSignature = "application/x-vnd.vos-Passwords";


int
main(int argc, char* argv[])
{
	BApplication app(kPasswordsSignature);

	BWindow* window = new BWindow(BRect(80, 80, 920, 640),
		B_TRANSLATE("Passwords"), B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE);
	BLayoutBuilder::Group<>(window, B_VERTICAL, 0)
		.Add(new PasswordsView());
	window->CenterOnScreen();
	window->Show();

	app.Run();
	return 0;
}
