/*
 * Copyright 2001-2016, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 * 		Christian Packmann
 *		Dario Casalinuovo
 */


#include "AppServer.h"

#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#include <AutoDeleter.h>
#include <LaunchRoster.h>
#include <PortLink.h>
#include <RosterPrivate.h>

#include "BitmapManager.h"
#include "Desktop.h"
#include "GlobalFontManager.h"
#include "HWInterface.h"
#include "InputManager.h"
#include "ScreenManager.h"
#include "ServerProtocol.h"


//#define DEBUG_SERVER
#ifdef DEBUG_SERVER
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


// Globals
port_id gAppServerPort;
BTokenSpace gTokenSpace;
uint32 gAppServerSIMDFlags = 0;


/*!	\brief Constructor

	This loads the default fonts, allocates all the major global variables,
	spawns the main housekeeping threads, loads user preferences for the UI
	and decorator, and allocates various locks.
*/
AppServer::AppServer(status_t* status)
	:
	SERVER_BASE("application/x-vnd.Haiku-app_server", "picasso", -1, false,
		status),
	fDesktopLock("AppServerDesktopLock"),
	fLockRequest(NULL),
	fLockResult(B_OK),
	fLockPending(0)
{
	openlog("app_server", 0, LOG_DAEMON);

	gInputManager = new InputManager();

	// Create the font server and scan the proper directories.
	gFontManager = new GlobalFontManager;
	if (gFontManager->InitCheck() != B_OK)
		debugger("font manager could not be initialized!");

	gFontManager->Run();

	gScreenManager = new ScreenManager();
	gScreenManager->Run();

	// Create the bitmap allocator. Object declared in BitmapManager.cpp
	gBitmapManager = new BitmapManager();

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#if 0
	// This is not presently needed, as app_server is launched from the login session.
	// TODO: check the attached displays, and launch login session for them
	BMessage data;
	data.AddString("name", "app_server");
	data.AddInt32("session", 0);
	BLaunchRoster().Target("login", data);
#endif

	// Inform the registrar we've (re)started.
	BMessage request(kMsgAppServerStarted);
	BRoster::Private().SendTo(&request, NULL, false);
#endif
}


/*!	\brief Destructor
	Reached only when the server is asked to shut down in Test mode.
*/
AppServer::~AppServer()
{
	delete fLockRequest;
	delete gBitmapManager;

	gScreenManager->Lock();
	gScreenManager->Quit();

	gFontManager->Lock();
	gFontManager->Quit();

	closelog();
}


void
AppServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case AS_GET_DESKTOP:
		{
			// AS_GET_DESKTOP is 0, the code of a bare reply to an
			// asynchronous send from this team.
			if (message->IsReply())
				break;

			Desktop* desktop = NULL;

			int32 userID = message->GetInt32("user", 0);
			int32 version = message->GetInt32("version", 0);
			const char* targetScreen = message->GetString("target");

			// Client-supplied "user" must match the kernel-attested writer
			// uid unless we're running as root (system-mode).
			uid_t senderUid = message->SenderUid();
			uid_t serverUid = getuid();
			bool acceptable = (serverUid == 0)
				|| (senderUid != (uid_t)-1
					&& (int32)senderUid == userID);

			if (version != AS_PROTOCOL_VERSION) {
				syslog(LOG_ERR, "Application for user %" B_PRId32 " does not "
					"support the current server protocol (%" B_PRId32 ").\n",
					userID, version);
			} else if (!acceptable) {
				syslog(LOG_ERR, "AS_GET_DESKTOP: sender uid %u does not match "
					"requested user %" B_PRId32 "; refusing.\n",
					(unsigned)senderUid, userID);
			} else {
				desktop = _FindDesktop(userID, targetScreen);
				if (desktop == NULL) {
					// we need to create a new desktop object for this user
					// TODO: test if the user exists on the system
					// TODO: maybe have a separate AS_START_DESKTOP_SESSION for
					// authorizing the user
					desktop = _CreateDesktop(userID, targetScreen);
				}
			}

			BMessage reply;
			if (desktop != NULL)
				reply.AddInt32("port", desktop->MessagePort());
			else
				reply.what = (uint32)B_ERROR;

			message->SendReply(&reply);
			break;
		}

		case B_SEAT_ENABLED:
		case B_SEAT_DISABLED:
		{
			bool enable = (message->what == B_SEAT_ENABLED);
			for (int32 i = 0; i < fDesktops.CountItems(); i++) {
				Desktop* desktop = fDesktops.ItemAt(i);
				if (desktop == NULL)
					continue;
				::HWInterface* hw = desktop->HWInterface();
				if (hw == NULL)
					continue;
				if (enable)
					hw->OnSeatEnabled();
				else
					hw->OnSeatDisabled();
			}

			BMessage reply(B_REPLY);
			message->SendReply(&reply);
			break;
		}

		case B_SESSION_LOCK:
		case B_SESSION_UNLOCK:
		{
			// Only root (janus, registrar) may change lock state; a
			// session app must go through the janus broker.
			uid_t senderUid = message->SenderUid();
			if (senderUid != 0) {
				syslog(LOG_ERR, "B_SESSION_* rejected from uid %u\n",
					(unsigned)senderUid);
				break;
			}

			// Each Desktop applies it on its own thread and answers; reply
			// once all have. Never wait here: a wedged Desktop must not hang us.
			int32 unlockTeam = message->GetInt32("team", -1);
			bool lock = (message->what == B_SESSION_LOCK);
			// Keep the request (and its reply target) past this handler.
			BMessage* request = DetachCurrentMessage();
			if (request == NULL)
				break;

			{
				BAutolock locker(fDesktopLock);
				delete fLockRequest;
				fLockRequest = request;
				fLockResult = B_OK;
				fLockPending = 0;
				for (int32 i = 0; i < fDesktops.CountItems(); i++) {
					Desktop* desktop = fDesktops.ItemAt(i);
					if (desktop == NULL)
						continue;
					status_t s = lock
						? desktop->RequestLockScreen(unlockTeam,
							BMessenger(this))
						: desktop->RequestUnlockScreen(BMessenger(this));
					if (s == B_OK)
						fLockPending++;
				}
			}

			if (fLockPending == 0) {
				// No desktop to apply it; answer now.
				BAutolock locker(fDesktopLock);
				if (fLockRequest != NULL) {
					BMessage reply(B_ERROR);
					fLockRequest->SendReply(&reply);
					delete fLockRequest;
					fLockRequest = NULL;
				}
			}
			break;
		}

		case Desktop::kMsgDesktopLockDone:
		{
			// A Desktop finished applying the lock state. Reply to
			// the original requester once all of them have.
			status_t result = message->GetInt32("result", B_ERROR);
			BAutolock locker(fDesktopLock);
			if (result != B_OK)
				fLockResult = result;
			if (fLockPending > 0)
				fLockPending--;
			if (fLockPending == 0 && fLockRequest != NULL) {
				BMessage reply(fLockResult == B_OK ? B_REPLY : B_ERROR);
				fLockRequest->SendReply(&reply);
				delete fLockRequest;
				fLockRequest = NULL;
			}
			break;
		}

		default:
			// We don't allow application scripting
			STRACE(("AppServer received unexpected code %" B_PRId32 "\n",
				message->what));
			break;
	}
}


bool
AppServer::QuitRequested()
{
	// Tear down desktops so the HWInterface destructor drops the DRM
	// master — otherwise the next app_server hits EACCES on drmSetMaster.
	while (fDesktops.CountItems() > 0) {
		Desktop *desktop = fDesktops.RemoveItemAt(0);

		thread_id thread = desktop->Thread();
		desktop->PostMessage(B_QUIT_REQUESTED);

		status_t status;
		wait_for_thread(thread, &status);
	}

#if TEST_MODE
	delete this;
	exit(0);

	return SERVER_BASE::QuitRequested();
#else
	return true;
#endif
}


/*!	\brief Creates a desktop object for an authorized user
*/
Desktop*
AppServer::_CreateDesktop(uid_t userID, const char* targetScreen)
{
	BAutolock locker(fDesktopLock);
	ObjectDeleter<Desktop> desktop;
	try {
		desktop.SetTo(new Desktop(userID, targetScreen));

		status_t status = desktop->Init();
		if (status == B_OK)
			status = desktop->Run();
		if (status == B_OK && !fDesktops.AddItem(desktop.Get()))
			status = B_NO_MEMORY;

		if (status != B_OK) {
			syslog(LOG_ERR, "Cannot initialize Desktop object: %s\n",
				strerror(status));
			return NULL;
		}
	} catch (...) {
		// there is obviously no memory left
		return NULL;
	}

	return desktop.Detach();
}


/*!	\brief Finds the desktop object that belongs to a certain user
*/
Desktop*
AppServer::_FindDesktop(uid_t userID, const char* targetScreen)
{
	BAutolock locker(fDesktopLock);

	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);

		if (desktop->UserID() == userID
			&& ((desktop->TargetScreen() == NULL && targetScreen == NULL)
				|| (desktop->TargetScreen() != NULL && targetScreen != NULL
					&& strcmp(desktop->TargetScreen(), targetScreen) == 0))) {
			return desktop;
		}
	}

	return NULL;
}


//	#pragma mark -


// Async-signal-safe: post B_QUIT_REQUESTED so the looper unwinds the
// C++ teardown (Desktop -> HWInterface dtor -> drmDropMaster).
static void
signal_quit(int /*signo*/)
{
	if (gAppServerPort >= 0)
		write_port_etc(gAppServerPort, B_QUIT_REQUESTED, NULL, 0,
			B_RELATIVE_TIMEOUT, 0);
}


int
main(int argc, char** argv)
{
	srand(real_time_clock_usecs());

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = signal_quit;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);

	status_t status;
	AppServer* server = new AppServer(&status);
	if (status == B_OK)
		server->Run();

	return status == B_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
