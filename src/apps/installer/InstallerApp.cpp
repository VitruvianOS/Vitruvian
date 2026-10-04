/*
 * Copyright 2020, Panagiotis "Ivory" Vasilopoulos <git@n0toose.net>
 * Copyright 2015, Axel Dörfler <axeld@pinc-software.de>
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2005, Jérôme DUVAL.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "InstallerApp.h"
#include "InstallerDefs.h"

#include <string.h>
#include <unistd.h>

#include <Alert.h>
#include <Roster.h>
#include <RosterPrivate.h>
#include <TextView.h>

#include "tracker_private.h"
#ifndef __VOS__
#include "Utility.h"
#endif


static const uint32 kMsgAgree = 'agre';
static const uint32 kMsgNext = 'next';

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
	BApplication("application/x-vnd.vos-Installer"),
	fSetupMode(false),
	fSetupDone(false)
{
}


void
InstallerApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAgree:
			fEULAWindow->PostMessage(B_QUIT_REQUESTED);
			break;
		case MSG_SETUP_DONE:
			fSetupDone = true;
			break;
		case kMsgNext:
			new InstallerWindow(fSetupMode);
			break;

		default:
			BApplication::MessageReceived(message);
	}
}


void
InstallerApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--setup-account") == 0)
			fSetupMode = true;
	}
}


void
InstallerApp::ReadyToRun()
{
#if 1
	// Show the EULA first.
	fEULAWindow = new EULAWindow();
#else
	// Show the installer window without EULA.
	new InstallerWindow(fSetupMode);
#endif
}


void
InstallerApp::Quit()
{
	BApplication::Quit();

	if (!be_roster->IsRunning(kDeskbarSignature)) {
		if (CurrentMessage()->GetBool("install_complete")) {
			// Synchronize disks
			sync();

#ifndef __VOS__
			if (Utility::IsReadOnlyVolume("/boot")) {
				// Unblock CD tray, and eject the CD
				Utility::BlockMedia("/boot", false);
				Utility::EjectMedia("/boot");
			}
#endif

			// Quickly reboot without touching anything
			// on disk (which we might just have ejected)
			BRoster::Private((BRoster*)be_roster).ShutDown(true, false,
				false);
		} else if (fSetupDone) {
			// Setup finished: the prompt no longer applies to this root.
		} else {
			// Return to FirstBootPrompt if the user hasn't
			// installed Haiku yet
			be_roster->Launch("application/x-vnd.vos-FirstBootPrompt");
		}
	}
}
