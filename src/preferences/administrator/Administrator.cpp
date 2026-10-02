/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>

#include "AdministratorWindow.h"


class AdministratorApp : public BApplication {
public:
	AdministratorApp()
		:
		BApplication("application/x-vnd.vos-Administrator")
	{
	}

	virtual void ReadyToRun()
	{
		new AdministratorWindow();
	}
};


int
main()
{
	AdministratorApp app;
	app.Run();
	return 0;
}
