/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>. Distributed under the terms of the
 * MIT License.
 */

#include <Application.h>

#include "UserWindow.h"


class UserApp : public BApplication {
public:
	UserApp()
		:
		BApplication("application/x-vnd.Vitruvian-User")
	{
	}

	virtual void ReadyToRun()
	{
		new UserWindow();
	}
};


int
main()
{
	UserApp app;
	app.Run();
	return 0;
}
