/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "StoreDefs.h"
#include "StoreWindow.h"

#include <Application.h>
#include <String.h>

#include <cstring>


class StoreApp : public BApplication {
public:
								StoreApp();
	virtual						~StoreApp();

	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				ReadyToRun();

private:
			bool				fDryRun;
};


StoreApp::StoreApp()
	:
	BApplication(kStoreSignature),
	fDryRun(false)
{
}


StoreApp::~StoreApp()
{
}


void
StoreApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--dry-run") == 0)
			fDryRun = true;
	}
}


void
StoreApp::ReadyToRun()
{
	BRect frame(120, 120, 1020, 780);
	StoreWindow* window = new StoreWindow(frame, fDryRun);
	window->Show();
}


int
main(int /*argc*/, char** /*argv*/)
{
	StoreApp app;
	app.Run();
	return 0;
}
