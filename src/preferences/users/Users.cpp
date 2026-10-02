/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>

#include "UsersWindow.h"


class UsersApp : public BApplication {
public:
	UsersApp()
		:
		BApplication("application/x-vnd.vos-Users")
	{
	}

	virtual void ReadyToRun()
	{
		new UsersWindow();
	}
};


int
main()
{
	UsersApp app;
	app.Run();
	return 0;
}
