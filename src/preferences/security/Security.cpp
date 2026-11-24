/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SecurityReplicant.h"
#include "SecurityWindow.h"

#include <Application.h>
#include <Message.h>


class SecurityApp : public BApplication {
public:
								SecurityApp()
									:
									BApplication(kSecuritySignature),
									fWindow(NULL)
								{
								}

	virtual	void				ReadyToRun();

private:
			SecurityWindow*		fWindow;
};


void
SecurityApp::ReadyToRun()
{
	fWindow = new SecurityWindow();
	fWindow->Show();
}


int
main()
{
	SecurityApp app;
	app.Run();
	return 0;
}
