/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <string.h>

#include <Application.h>

#include "AccountsPreflet.h"
#include "AccountsWindow.h"


class AccountsApplication : public BApplication {
public:
								AccountsApplication();

	virtual	void				ReadyToRun();
	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				MessageReceived(BMessage* message);

private:
			AccountsWindow*		fWindow;
};


AccountsApplication::AccountsApplication()
	:
	BApplication(kAccountsPreferencesSignature),
	fWindow(NULL)
{
	fWindow = new AccountsWindow();
}


void
AccountsApplication::ReadyToRun()
{
	fWindow->Show();
}


void
AccountsApplication::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i + 1 < argc; i++) {
		if (strcmp(argv[i], "--add") != 0)
			continue;
		BMessage add(kMsgAccountsAdd);
		add.AddString(kAccountsTypeField, argv[i + 1]);
		fWindow->PostMessage(&add);
		break;
	}
}


void
AccountsApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAccountsAdd:
			fWindow->PostMessage(message);
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


int
main()
{
	AccountsApplication application;
	application.Run();
	return 0;
}
