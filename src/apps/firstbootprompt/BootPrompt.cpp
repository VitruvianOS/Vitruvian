/*
 * Copyright 2010, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2020, Panagiotis "Ivory" Vasilopoulos <git@n0toose.net>
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "BootPrompt.h"

#include <stdlib.h>

#ifndef __VOS__
#include <LaunchRoster.h>
#else
#include <kernel/util/KMessage.h>
#include <LaunchDaemonDefs.h>
#endif
#include <Roster.h>
#include <RosterPrivate.h>


static int sExitValue;


int
main(int, char **)
{
	BootPromptApp app;
	app.Run();
	return sExitValue;
}


// #pragma mark -


const char* kAppSignature = "application/x-vnd.Haiku-FirstBootPrompt";
const char* kDeskbarSignature = "application/x-vnd.Be-TSKB";


BootPromptApp::BootPromptApp()
	:
	BApplication(kAppSignature)
{
}


void
BootPromptApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_BOOT_DESKTOP:
		{
#ifdef __VOS__
			port_id janusPort = find_port(B_LAUNCH_DAEMON_PORT_NAME);
			if (janusPort >= 0) {
				BPrivate::KMessage msg(BPrivate::B_JANUS_LOGIN_OK);
				msg.AddString("user", "vos-live");
				msg.AddString("mode", "live");
				if (message->GetBool("enable_ssh", false))
					msg.AddInt32("enable_ssh", 1);
				BPrivate::KMessage reply;
				msg.SendTo(janusPort, -1, &reply,
					2000000LL, 5000000LL, getpid());
			}
#else
			BLaunchRoster().Target("desktop");
#endif
			sExitValue = 1;
			PostMessage(B_QUIT_REQUESTED);
			break;
		}
		case MSG_RUN_INSTALLER:
		{
			be_roster->Launch("application/x-vnd.Haiku-Installer");
			sExitValue = 0;
			PostMessage(B_QUIT_REQUESTED);
			break;
		}
		case MSG_REBOOT_REQUESTED:
		{
			BRoster::Private(be_roster).ShutDown(true, false, false);
			sExitValue = -1;
			break;
		}

		default:
			BApplication::MessageReceived(message);
	}
}


void
BootPromptApp::ReadyToRun()
{
	new BootPromptWindow();
}


bool
BootPromptApp::QuitRequested()
{
	// Skip the window's reboot prompt: we got here from a button, not close.
	return true;
}
