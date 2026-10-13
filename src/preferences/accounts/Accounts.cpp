/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>

#include "AccountsWindow.h"


class AccountsApplication : public BApplication {
public:
								AccountsApplication();

	virtual	void				ReadyToRun();

private:
			AccountsWindow*		fWindow;
};


AccountsApplication::AccountsApplication()
	:
	BApplication("application/x-vnd.vos-Accounts"),
	fWindow(NULL)
{
	fWindow = new AccountsWindow();
}


void
AccountsApplication::ReadyToRun()
{
	fWindow->Show();
}


int
main()
{
	AccountsApplication application;
	application.Run();
	return 0;
}
