/*
 * Copyright 2024, Vitruvian OS.
 * Based on Haiku Installer, Copyright 2005-2015, various authors.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "InstallerApp.h"

#include <unistd.h>

#include <Roster.h>

#include "tracker_private.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InstallerApp"


int main(int, char **)
{
	InstallerApp installer;
	installer.Run();
	return 0;
}


InstallerApp::InstallerApp()
	:
	BApplication("application/x-vnd.Vitruvian-Installer")
{
}


void
InstallerApp::MessageReceived(BMessage* message)
{
	BApplication::MessageReceived(message);
}


void
InstallerApp::ReadyToRun()
{
	new InstallerWindow();
}


void
InstallerApp::Quit()
{
	BApplication::Quit();

	if (!be_roster->IsRunning(kDeskbarSignature)) {
		if (CurrentMessage()->GetBool("install_complete")) {
			sync();
		}
	}
}
