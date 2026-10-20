/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CortexApp.h"

#include <stdio.h>
#include <string.h>

#include "CortexDump.h"
#include "CortexWindow.h"


const char kCortexSignature[] = "application/x-vnd.vos-Cortex";


CortexApp::CortexApp()
	:
	BApplication(kCortexSignature),
	fDumpRequested(false)
{
}


void
CortexApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--dump") == 0)
			fDumpRequested = true;
	}
}


void
CortexApp::ReadyToRun()
{
	if (fDumpRequested) {
		BString text;
		status_t status = RunGraphDump(text);
		if (status == B_OK)
			printf("%s", text.String());
		else
			fprintf(stderr, "cortex: graph dump failed: %s\n",
				strerror(status));
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	CortexWindow* window = new(std::nothrow) CortexWindow();
	if (window == NULL) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	window->Show();
}


int
main()
{
	CortexApp* app = new(std::nothrow) CortexApp();
	if (app == NULL)
		return 1;
	app->Run();
	delete app;
	return 0;
}
