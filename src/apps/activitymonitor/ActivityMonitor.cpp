/*
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ActivityMonitor.h"

#include <stdlib.h>

#include <Application.h>

#include "ActivityWindow.h"


const char* kAppName = B_TRANSLATE_SYSTEM_NAME("ActivityMonitor");
const char* kSignature = "application/x-vnd.vos-ActivityMonitor";


ActivityMonitor::ActivityMonitor()
	: BApplication(kSignature)
{
	fWindow = new ActivityWindow();
}


ActivityMonitor::~ActivityMonitor()
{
}


void
ActivityMonitor::ReadyToRun()
{
	fWindow->Show();
}


void
ActivityMonitor::RefsReceived(BMessage* message)
{
	fWindow->PostMessage(message);
}


void
ActivityMonitor::MessageReceived(BMessage* message)
{
	BApplication::MessageReceived(message);
}


void
ActivityMonitor::AboutRequested()
{
}


//	#pragma mark -


int
main(int /*argc*/, char** /*argv*/)
{
	ActivityMonitor app;
	app.Run();

	return 0;
}
