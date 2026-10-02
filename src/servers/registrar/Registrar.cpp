/*
 * Copyright 2001-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 */

#include "Registrar.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <exception>
#include <vector>

#include <Application.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <File.h>
#include <FindDirectory.h>
#include <LaunchDaemonDefs.h>
#include <Message.h>
#include <MessengerPrivate.h>
#include <OS.h>
#include <Path.h>
#include <RegistrarDefs.h>
#include <RosterPrivate.h>
#include <String.h>
#include <system_info.h>

#include <kernel/util/KMessage.h>


#include "AuthenticationManager.h"
#include "ClipboardHandler.h"
#include "Debug.h"
#include "EventQueue.h"
#include "LogindBridge.h"
#include "MessageDeliverer.h"
#include "MessageEvent.h"
#include "MessageRunnerManager.h"
#include "MIMEManager.h"
#include "PackageWatchingManager.h"
#include "ShutdownProcess.h"
#include "SleepWindow.h"
#include "TRoster.h"


/*!
	\class Registrar
	\brief The application class of the registrar.

	Glues the registrar services together and dispatches the roster messages.
*/

using std::nothrow;
using namespace BPrivate;

//! Name of the event queue.
static const char *kEventQueueName = "timer_thread";

//! Message code for the periodic roster sanity check event.
static const uint32 kMsgRosterSanityCheck = 'rSAN';

// Half of kMaximalEarlyPreRegistrationPeriod, so an expired early pre-reg
// is reaped at most one interval late even if HandleAddApplication's lazy
// check never gets a chance to run (e.g. no further launch is attempted).
static const bigtime_t kSanityCheckInterval = 30000000LL;

// Sleep timing: how long apps get after B_SYSTEM_SUSPENDING, when the window closes, and how
// recent our own request must be to name the sleep kind and how much sleep proves a sleep.
// kMsgSleepLockDone: a janus lock round-trip ended for this sleep cycle, with the result.
static const uint32 kMsgSleepNotified = 'slNt';
static const uint32 kMsgSleepRelease = 'slRl';
static const uint32 kMsgSleepLockDone = 'slLd';
static const bigtime_t kSleepHandlerTime = 2000000LL;
static const bigtime_t kSleepWindowCloseTime = 250000LL;
static const bigtime_t kSleepNotifySlack = 100000LL;
static const bigtime_t kSleepRequestValidity = 60000000LL;
static const bigtime_t kSleptThreshold = 100000LL;

// Critical battery: checked this often, acted on at this charge.
static const uint32 kMsgBatteryCheck = 'btCk';
static const bigtime_t kBatteryCheckInterval = 30000000LL;
static const int kBatteryCriticalPercent = 3;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Registrar"


static bigtime_t
sleep_clock_offset()
{
	struct timespec boot, monotonic;
	clock_gettime(CLOCK_BOOTTIME, &boot);
	clock_gettime(CLOCK_MONOTONIC, &monotonic);
	return (bigtime_t)(boot.tv_sec - monotonic.tv_sec) * 1000000
		+ (boot.tv_nsec - monotonic.tv_nsec) / 1000;
}


/*!	\brief Creates the registrar application class.
	\param error Passed to the BApplication constructor for returning an
		   error code.
*/
Registrar::Registrar(status_t* _error)
	:
	BServer(B_REGISTRAR_SIGNATURE, B_REGISTRAR_PORT_NAME, -1, false, _error),
	fRoster(NULL),
	fClipboardHandler(NULL),
	fMIMEManager(NULL),
	fEventQueue(NULL),
	fSanityCheckEvent(NULL),
	fMessageRunnerManager(NULL),
	fShutdownProcess(NULL),
	fAuthenticationManager(NULL),
	fPackageWatchingManager(NULL),
	fLogindBridge(NULL),
	fSleepCycle(0),
	fSleepHibernate(false),
	fSleepClockOffset(0),
	fSleepNotified(false),
	fSleepRequestHibernate(false),
	fSleepRequestTime(0),
	fBatteryActed(false)
{
	FUNCTION_START();

	set_thread_priority(find_thread(NULL), B_NORMAL_PRIORITY + 1);
}


/*!	\brief Frees all resources associated with the registrar.

	All registrar services, that haven't been shut down earlier, are
	terminated.
*/
Registrar::~Registrar()
{
	FUNCTION_START();
	Lock();
	if (fLogindBridge != NULL) {
		fLogindBridge->Stop();
		delete fLogindBridge;
		fLogindBridge = NULL;
	}
	fEventQueue->Die();
	delete fSanityCheckEvent;
	delete fMessageRunnerManager;
	delete fEventQueue;
	fMIMEManager->Lock();
	fMIMEManager->Quit();
	RemoveHandler(fClipboardHandler);
	delete fClipboardHandler;
	delete fRoster;
	// Invalidate the global be_roster, so that the BApplication destructor
	// won't dead-lock when sending a message to itself.
	BRoster::Private().SetTo(BMessenger(), BMessenger());
	FUNCTION_END();
}


/*!	\brief Overrides the super class version to dispatch roster specific
		   messages.
	\param message The message to be handled
*/
void
Registrar::MessageReceived(BMessage *message)
{
	try {
		_MessageReceived(message);
	} catch (std::exception& exception) {
		char buffer[1024];
		snprintf(buffer, sizeof(buffer),
			"Registrar::MessageReceived() caught exception: %s",
			exception.what());
		debugger(buffer);
	} catch (...) {
		debugger("Registrar::MessageReceived() caught unknown exception");
	}
}


/*!	\brief Overrides the super class version to initialize the registrar
		   services.
*/
void
Registrar::ReadyToRun()
{
	FUNCTION_START();

	// create message deliverer
	status_t error = MessageDeliverer::CreateDefault();
	if (error != B_OK) {
		FATAL("Registrar::ReadyToRun(): Failed to create the message "
			"deliverer: %s\n", strerror(error));
	}

	// create event queue
	fEventQueue = new EventQueue(kEventQueueName);

	// create roster
	fRoster = new TRoster;
	fRoster->Init();

	// create clipboard handler
	fClipboardHandler = new ClipboardHandler;
	AddHandler(fClipboardHandler);

	// create MIME manager
	fMIMEManager = new MIMEManager;
	fMIMEManager->Run();

	// create message runner manager
	fMessageRunnerManager = new MessageRunnerManager(fEventQueue);

	// init the global be_roster
	BRoster::Private().SetTo(be_app_messenger, BMessenger(NULL, fMIMEManager));

	// Sanity check roster after team deletion
	BMessenger target(this);
	BMessenger::Private messengerPrivate(target);

	port_id port = messengerPrivate.Port();
	int32 token = messengerPrivate.Token();
	// Pidfd watching is the primary; use global subscription as fallback.
	if (!fRoster->IsWatchingTeams())
		__start_watching_system(-1, B_WATCH_SYSTEM_TEAM_DELETION, port, token);
	fRoster->CheckSanity();
		// Clean up any teams that exited before we started watching

	// Re-armed on each fire (see kMsgRosterSanityCheck handler below); the
	// general safety net for stale early pre-registrations whose launcher
	// never dies, since CheckSanity() above only ever runs once at startup.
	fEventQueue->AddEvent(new(nothrow) MessageEvent(
		system_time() + kBatteryCheckInterval, this, kMsgBatteryCheck));

	fSanityCheckEvent = new(nothrow) MessageEvent(
		system_time() + kSanityCheckInterval, BMessenger(this),
		kMsgRosterSanityCheck);
	if (fSanityCheckEvent != NULL) {
		fSanityCheckEvent->SetAutoDelete(false);
		fEventQueue->AddEvent(fSanityCheckEvent);
	}

	// Bring up logind bridge: delay inhibit locks + PrepareForShutdown /
	// PrepareForSleep signal subscription. See LogindBridge.h.
	fLogindBridge = new(nothrow) LogindBridge(BMessenger(this));
	if (fLogindBridge == NULL || fLogindBridge->Start() != B_OK) {
		fprintf(stderr, "Registrar: LogindBridge init failed; bottom-up "
			"shutdown/sleep will bypass the quit dance.\n");
		delete fLogindBridge;
		fLogindBridge = NULL;
	}

	FUNCTION_END();
}


/*!	\brief Overrides the super class version to avoid termination of the
		   registrar until the system shutdown.
*/
bool
Registrar::QuitRequested()
{
	FUNCTION_START();
	// The final registrar must not quit. At least not that easily. ;-)
	return BApplication::QuitRequested();
}


/*!	\brief Returns the registrar's event queue.
	\return The registrar's event queue.
*/
EventQueue*
Registrar::GetEventQueue() const
{
	return fEventQueue;
}



/*!	\brief Returns the Registrar application object.
	\return The Registrar application object.
*/
Registrar*
Registrar::App()
{
	return dynamic_cast<Registrar*>(be_app);
}


void
Registrar::_MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgRosterSanityCheck:
			fRoster->CheckSanity();
			if (fSanityCheckEvent != NULL) {
				fSanityCheckEvent->SetTime(system_time() + kSanityCheckInterval);
				fEventQueue->AddEvent(fSanityCheckEvent);
			}
			break;

		// general requests
		case B_REG_GET_MIME_MESSENGER:
		{
			PRINT("B_REG_GET_MIME_MESSENGER\n");
			BMessenger messenger(NULL, fMIMEManager);
			BMessage reply(B_REG_SUCCESS);
			reply.AddMessenger("messenger", messenger);
			message->SendReply(&reply);
			break;
		}

		case B_REG_GET_CLIPBOARD_MESSENGER:
		{
			PRINT("B_REG_GET_CLIPBOARD_MESSENGER\n");
			BMessenger messenger(fClipboardHandler);
			BMessage reply(B_REG_SUCCESS);
			reply.AddMessenger("messenger", messenger);
			message->SendReply(&reply);
			break;
		}

		// shutdown process
		case B_REG_SHUT_DOWN:
		{
			PRINT("B_REG_SHUT_DOWN\n");

			_HandleShutDown(message);
			break;
		}
		case B_REG_IS_SHUT_DOWN_IN_PROGRESS:
		{
			PRINT("B_REG_IS_SHUT_DOWN_IN_PROGRESS\n");

			_HandleIsShutDownInProgress(message);
			break;
		}
		case B_REG_REQUEST_SLEEP:
		{
			PRINT("B_REG_REQUEST_SLEEP\n");

			_HandleRequestSleep(message);
			break;
		}
		case B_REG_IS_SLEEP_AVAILABLE:
		{
			PRINT("B_REG_IS_SLEEP_AVAILABLE\n");

			_HandleIsSleepAvailable(message);
			break;
		}
		case kMsgLogindPrepareForShutdown:
			_HandleLogindPrepareForShutdown(message);
			break;
		case kMsgLogindPrepareForSleep:
			_HandleLogindPrepareForSleep(message);
			break;
		case kMsgLogindSleepRefused:
			_ShowSleepFailure(message->GetBool("hibernate", false),
				message->GetString("reason", NULL));
			break;
		case kMsgSleepNotified:
		case kMsgSleepRelease:
		case kMsgSleepLockDone:
			_HandleSleepTimer(message);
			break;
		case kMsgLogindSessionLock:
			_HandleLogindSessionLock(message);
			break;
		case kMsgBatteryCheck:
			_CheckBattery();
			fEventQueue->AddEvent(new(nothrow) MessageEvent(
				system_time() + kBatteryCheckInterval, this, kMsgBatteryCheck));
			break;
		case B_REG_TEAM_DEBUGGER_ALERT:
		{
			if (fShutdownProcess != NULL)
				fShutdownProcess->PostMessage(message);
			break;
		}

		// roster requests
		case B_REG_ADD_APP:
			fRoster->HandleAddApplication(message);
			break;
		case B_REG_COMPLETE_REGISTRATION:
			fRoster->HandleCompleteRegistration(message);
			break;
		case B_REG_IS_APP_REGISTERED:
			fRoster->HandleIsAppRegistered(message);
			break;
		case B_REG_REMOVE_PRE_REGISTERED_APP:
			fRoster->HandleRemovePreRegApp(message);
			break;
		case B_REG_REMOVE_APP:
			fRoster->HandleRemoveApp(message);
			break;
		case B_REG_SET_THREAD_AND_TEAM:
			fRoster->HandleSetThreadAndTeam(message);
			break;
		case B_REG_SET_SIGNATURE:
			fRoster->HandleSetSignature(message);
			break;
		case B_REG_GET_APP_INFO:
			fRoster->HandleGetAppInfo(message);
			break;
		case B_REG_GET_APP_LIST:
			fRoster->HandleGetAppList(message);
			break;
		case B_REG_UPDATE_ACTIVE_APP:
			fRoster->HandleUpdateActiveApp(message);
			break;
		case B_REG_BROADCAST:
			fRoster->HandleBroadcast(message);
			break;
		case B_REG_START_WATCHING:
			fRoster->HandleStartWatching(message);
			break;
		case B_REG_STOP_WATCHING:
			fRoster->HandleStopWatching(message);
			break;
		case B_REG_GET_RECENT_DOCUMENTS:
			fRoster->HandleGetRecentDocuments(message);
			break;
		case B_REG_GET_RECENT_FOLDERS:
			fRoster->HandleGetRecentFolders(message);
			break;
		case B_REG_GET_RECENT_APPS:
			fRoster->HandleGetRecentApps(message);
			break;
		case B_REG_ADD_TO_RECENT_DOCUMENTS:
			fRoster->HandleAddToRecentDocuments(message);
			break;
		case B_REG_ADD_TO_RECENT_FOLDERS:
			fRoster->HandleAddToRecentFolders(message);
			break;
		case B_REG_ADD_TO_RECENT_APPS:
			fRoster->HandleAddToRecentApps(message);
			break;
		case B_REG_CLEAR_RECENT_DOCUMENTS:
			fRoster->ClearRecentDocuments();
			break;
		case B_REG_CLEAR_RECENT_FOLDERS:
			fRoster->ClearRecentFolders();
			break;
		case B_REG_CLEAR_RECENT_APPS:
			fRoster->ClearRecentApps();
			break;
		case B_REG_LOAD_RECENT_LISTS:
			fRoster->HandleLoadRecentLists(message);
			break;
		case B_REG_SAVE_RECENT_LISTS:
			fRoster->HandleSaveRecentLists(message);
			break;

		// message runner requests
		case B_REG_REGISTER_MESSAGE_RUNNER:
			fMessageRunnerManager->HandleRegisterRunner(message);
			break;
		case B_REG_UNREGISTER_MESSAGE_RUNNER:
			fMessageRunnerManager->HandleUnregisterRunner(message);
			break;
		case B_REG_SET_MESSAGE_RUNNER_PARAMS:
			fMessageRunnerManager->HandleSetRunnerParams(message);
			break;
		case B_REG_GET_MESSAGE_RUNNER_INFO:
			fMessageRunnerManager->HandleGetRunnerInfo(message);
			break;

		// internal messages
		case B_SYSTEM_OBJECT_UPDATE:
		{
			team_id team = (team_id)message->GetInt32("team", -1);
			if (team >= 0 && message->GetInt32("opcode", 0) == B_TEAM_DELETED)
				fRoster->HandleRemoveApp(message);
			break;
		}
		case B_REG_SHUTDOWN_FINISHED:
			if (fShutdownProcess) {
				fShutdownProcess->PostMessage(B_QUIT_REQUESTED,
					fShutdownProcess);
				fShutdownProcess = NULL;
			}
			// Quit dance done; let logind proceed with poweroff.
			if (fLogindBridge != NULL)
				fLogindBridge->ReleaseShutdownInhibit();
			break;

		case kMsgAppServerStarted:
		{
			fRoster->HandleAppServerStarted(message);
			// Don't pass this message on to our BApplication, as that may deadlock.
			break;
		}

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


/*!	\brief Handle a shut down request message.
	\param request The request to be handled.
*/
void
Registrar::_HandleShutDown(BMessage *request)
{
	// check, whether we're already shutting down
	status_t error = B_SHUTTING_DOWN;
	if (fShutdownProcess == NULL)
		error = _CreateShutdownProcess(request);

	if (error != B_OK) {
		ShutdownProcess::SendReply(request, error);
		return;
	}

	// the ShutdownProcess owns the request now
	DetachCurrentMessage();
	fShutdownProcess->Run();
}


/*!	\brief Creates and initializes fShutdownProcess for  request.

	On success the process takes ownership of \a request once it runs.
*/
status_t
Registrar::_CreateShutdownProcess(BMessage *request)
{
	fShutdownProcess = new(nothrow) ShutdownProcess(fRoster, fEventQueue);
	if (fShutdownProcess == NULL)
		return B_NO_MEMORY;

	status_t error = fShutdownProcess->Init(request);
	if (error != B_OK) {
		delete fShutdownProcess;
		fShutdownProcess = NULL;
	}
	return error;
}

/*!	\brief Handle a is shut down in progress request message.
	\param request The request to be handled.
*/
void
Registrar::_HandleIsShutDownInProgress(BMessage *request)
{
	BMessage reply(B_REG_SUCCESS);
	reply.AddBool("in-progress", fShutdownProcess != NULL);
	request->SendReply(&reply);
}


/*!	\brief Handle a suspend/hibernate request.

	Asks logind to run the sleep. Reply goes out as soon as the request
	is accepted; the registrar looper must not sit through the sleep.
*/
void
Registrar::_HandleRequestSleep(BMessage *request)
{
	const char* which = NULL;
	if (request->FindString("sleep", &which) != B_OK || which == NULL) {
		BMessage reply(B_REG_ERROR);
		reply.AddInt32("error", B_BAD_VALUE);
		request->SendReply(&reply);
		return;
	}

	status_t error = B_BAD_VALUE;
	if (fLogindBridge == NULL)
		error = B_NO_INIT;
	else if (strcmp(which, "suspend") == 0)
		error = fLogindBridge->Suspend();
	else if (strcmp(which, "hibernate") == 0)
		error = fLogindBridge->Hibernate();

	if (error == B_OK) {
		fSleepRequestHibernate = strcmp(which, "hibernate") == 0;
		fSleepRequestTime = system_time();
	}

	BMessage reply(error == B_OK ? B_REG_SUCCESS : B_REG_ERROR);
	if (error != B_OK)
		reply.AddInt32("error", error);
	request->SendReply(&reply);
}


/*!	\brief Handle a sleep-availability query.

	Replies with "available" for the requested sleep kind.
*/
void
Registrar::_HandleIsSleepAvailable(BMessage *request)
{
	const char* which = NULL;
	if (request->FindString("sleep", &which) != B_OK || which == NULL) {
		BMessage reply(B_REG_ERROR);
		reply.AddInt32("error", B_BAD_VALUE);
		request->SendReply(&reply);
		return;
	}

	bool available = false;
	if (fLogindBridge != NULL) {
		if (strcmp(which, "suspend") == 0)
			available = fLogindBridge->CanSuspend();
		else if (strcmp(which, "hibernate") == 0)
			available = fLogindBridge->CanHibernate();
	}

	BMessage reply(B_REG_SUCCESS);
	reply.AddBool("available", available);
	request->SendReply(&reply);
}


/*!	\brief Handle logind PrepareForShutdown(active).

	Two-level shutdown collaboration. Reentrancy guard: if
	fShutdownProcess is already running, this is the systemctl -> logind
	loop reflecting our own top-down shutdown back at us; just release
	the inhibit fd and return.

	Otherwise this is a bottom-up trigger (power button, systemctl
	poweroff, shutdown -h now) and we run the quit dance WITHOUT the
	refusal window (delay-lock semantics: cannot veto once the signal
	fires) then release the inhibit.

	Reboot vs poweroff comes from PrepareForShutdownWithMetadata's
	"type" field (systemd >= v257). Without it we default to poweroff.
*/
void
Registrar::_HandleLogindPrepareForShutdown(BMessage *request)
{
	bool active = false;
	if (request->FindBool("active", &active) != B_OK || !active)
		return;

	if (fShutdownProcess != NULL) {
		// Our own shutdown is already in flight (top-down BRoster::Shutdown
		// -> _kern_shutdown -> systemctl -> PrepareForShutdown loop). Don't
		// re-enter. Inhibit will be released when B_REG_SHUTDOWN_FINISHED
		// fires from ShutdownProcess.
		return;
	}

	bool reboot = false;
	const char* type = NULL;
	if (request->FindString("type", &type) == B_OK && type != NULL
		&& (strcmp(type, "reboot") == 0
			|| strcmp(type, "kexec") == 0
			|| strcmp(type, "soft-reboot") == 0))
		reboot = true;

	// Fabricate a B_REG_SHUT_DOWN request with confirm=false and run the same ShutdownProcess as the
	// BeAPI path. Release the inhibit in B_REG_SHUTDOWN_FINISHED, not here, since Run() returns immediately.
	BMessage* synthetic = new(nothrow) BMessage(B_REG_SHUT_DOWN);
	if (synthetic == NULL)
		return;
	synthetic->AddBool("reboot", reboot);
	synthetic->AddBool("confirm", false);
	if (_CreateShutdownProcess(synthetic) != B_OK) {
		delete synthetic;
		return;
	}
	fShutdownProcess->Run();
}


/*!	\brief Handle logind PrepareForSleep(active).

	Broadcast B_SYSTEM_SUSPENDING/B_SYSTEM_RESUMED to registered apps
	via TRoster's existing broadcast primitive, then (on suspend)
	release the sleep inhibit so systemd proceeds.

	Screen lock sits in _HandleSleepTimer, at the moment the sleep would
	release the delay inhibitor: hibernate at kMsgSleepNotified, suspend
	at kMsgSleepRelease. The janus lock round-trip runs on a short
	thread; kMsgSleepLockDone releases the inhibit once it ends. The
	registrar looper never waits, and there is no fixed snooze — the
	inhibitor fd is the delay.
*/
void
Registrar::_HandleLogindPrepareForSleep(BMessage *request)
{
	bool active = false;
	if (request->FindBool("active", &active) != B_OK)
		return;

	// Stale timers of an earlier cycle must not release this one's lock.
	fSleepCycle++;

	if (active) {
		// Only our own request says which kind; logind's signal does not.
		fSleepHibernate = fSleepRequestHibernate
			&& system_time() - fSleepRequestTime < kSleepRequestValidity;
		fSleepRequestTime = 0;
		fSleepClockOffset = sleep_clock_offset();
		_ShowSleepWindow(fSleepHibernate);
	} else {
		// CLOCK_BOOTTIME runs on during sleep, CLOCK_MONOTONIC stops: if
		// their offset did not grow, the kernel backed out.
		if (sleep_clock_offset() - fSleepClockOffset > kSleptThreshold)
			fSleepWindow.SendMessage(B_QUIT_REQUESTED);
		else
			_ShowSleepFailure(fSleepHibernate, NULL);
	}

	if (!active) {
		BMessage payload(B_SYSTEM_RESUMED);
		BMessage broadcast(B_REG_BROADCAST);
		broadcast.AddInt32("team", -1);
		broadcast.AddMessage("message", &payload);
		broadcast.AddMessenger("reply_target", BMessenger(this));
		fRoster->HandleBroadcast(&broadcast);
		return;
	}

	// Every app gets B_SYSTEM_SUSPENDING and the sleep goes on once all have handled it, or after
	// kSleepHandlerTime. A suspend closes the window first; the bridge reacquires the lock on resume.
	bigtime_t deadline = system_time() + (fSleepHibernate ? kSleepHandlerTime
		: kSleepHandlerTime - kSleepWindowCloseTime);
	fSleepNotified = false;
	_NotifySleep(deadline);

	// Backstop, in case the notifier could not run.
	BMessage timer(kMsgSleepNotified);
	timer.AddInt32("cycle", fSleepCycle);
	fEventQueue->AddEvent(new(std::nothrow) MessageEvent(
		deadline + kSleepNotifySlack, this, &timer));
}


struct SleepNotice {
	BMessenger	target;
	bigtime_t	deadline;
};


struct SleepNotifier {
	std::vector<BMessenger>	apps;
	BMessenger				registrar;
	int32					cycle;
	bigtime_t				deadline;
};


// A synchronous send ends when the app's handler is done with the message
// (it replies, or B_NO_REPLY comes back when the message is deleted).
static status_t
notify_app(void* data)
{
	SleepNotice* notice = (SleepNotice*)data;
	bigtime_t left = notice->deadline - system_time();
	if (left > 0) {
		BMessage message(B_SYSTEM_SUSPENDING);
		BMessage reply;
		notice->target.SendMessage(&message, &reply, left, left);
	}
	delete notice;
	return B_OK;
}


static status_t
notify_apps(void* data)
{
	SleepNotifier* notifier = (SleepNotifier*)data;

	std::vector<thread_id> threads;
	for (const BMessenger& app : notifier->apps) {
		SleepNotice* notice = new(std::nothrow) SleepNotice;
		if (notice == NULL)
			continue;
		notice->target = app;
		notice->deadline = notifier->deadline;
		thread_id thread = spawn_thread(notify_app, "sleep notice",
			B_NORMAL_PRIORITY, notice);
		if (thread < 0 || resume_thread(thread) != B_OK) {
			delete notice;
			continue;
		}
		threads.push_back(thread);
	}
	for (thread_id thread : threads) {
		status_t result;
		wait_for_thread(thread, &result);
	}

	BMessage done(kMsgSleepNotified);
	done.AddInt32("cycle", notifier->cycle);
	notifier->registrar.SendMessage(&done);
	delete notifier;
	return B_OK;
}


void
Registrar::_NotifySleep(bigtime_t deadline)
{
	SleepNotifier* notifier = new(std::nothrow) SleepNotifier;
	if (notifier == NULL)
		return;
	fRoster->GetAppMessengers(notifier->apps);
	notifier->registrar = BMessenger(this);
	notifier->cycle = fSleepCycle;
	notifier->deadline = deadline;

	thread_id thread = spawn_thread(notify_apps, "sleep notifier",
		B_NORMAL_PRIORITY, notifier);
	if (thread < 0 || resume_thread(thread) != B_OK)
		delete notifier;
}


void
Registrar::_HandleSleepTimer(BMessage *message)
{
	if (message->GetInt32("cycle", -1) != fSleepCycle)
		return;

	if (message->what == kMsgSleepNotified) {
		// The notifier and the backstop both report; act once.
		if (fSleepNotified)
			return;
		fSleepNotified = true;
		if (fSleepHibernate) {
			// Hibernate: lock before the image is written;
			// kMsgSleepLockDone releases the inhibitor.
			_RequestScreenLockForSleep();
			return;
		}
		// Let app_server redraw what the window covered before freezing.
		fSleepWindow.SendMessage(B_QUIT_REQUESTED);
		BMessage timer(kMsgSleepRelease);
		timer.AddInt32("cycle", fSleepCycle);
		fEventQueue->AddEvent(new(std::nothrow) MessageEvent(
			system_time() + kSleepWindowCloseTime, this, &timer));
		return;
	}

	if (message->what == kMsgSleepRelease) {
		// Suspend: the window is gone; lock, then release.
		_RequestScreenLockForSleep();
		return;
	}

	if (message->what == kMsgSleepLockDone) {
		// Applied, skipped or failed: release either way, since sleep
		// must not hang on a dead janus.
		if (fLogindBridge != NULL)
			fLogindBridge->ReleaseSleepInhibit();
	}
}


static bool
read_sysfs(const char* dir, const char* name, char* buffer, size_t size)
{
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "/sys/class/power_supply/%s/%s", dir, name);
	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;
	bool ok = fgets(buffer, size, file) != NULL;
	fclose(file);
	if (ok)
		buffer[strcspn(buffer, "\n")] = '\0';
	return ok;
}


/*!	\brief Run the user's critical-battery action once per discharge.

	Nothing else does it: upower is not installed. The action comes from
	the Power preferences ("power:battery_critical_action").
*/
void
Registrar::_CheckBattery()
{
	DIR* dir = opendir("/sys/class/power_supply");
	if (dir == NULL)
		return;

	int count = 0;
	int capacitySum = 0;
	bool discharging = false;
	bool criticalLevel = false;
	char value[64];
	while (struct dirent* entry = readdir(dir)) {
		if (entry->d_name[0] == '.'
			|| !read_sysfs(entry->d_name, "type", value, sizeof(value))
			|| strcmp(value, "Battery") != 0)
			continue;
		// Mice and headsets report a "Device" scope; only system
		// batteries power the machine.
		if (read_sysfs(entry->d_name, "scope", value, sizeof(value))
			&& strcmp(value, "Device") == 0)
			continue;
		if (!read_sysfs(entry->d_name, "capacity", value, sizeof(value)))
			continue;
		count++;
		capacitySum += atoi(value);
		if (read_sysfs(entry->d_name, "status", value, sizeof(value))
			&& strcmp(value, "Discharging") == 0)
			discharging = true;
		if (read_sysfs(entry->d_name, "capacity_level", value, sizeof(value))
			&& strcmp(value, "Critical") == 0)
			criticalLevel = true;
	}
	closedir(dir);

	if (count == 0)
		return;
	if (!discharging) {
		fBatteryActed = false;
		return;
	}
	bool critical = capacitySum / count <= kBatteryCriticalPercent
		|| (count == 1 && criticalLevel);
	if (!critical || fBatteryActed)
		return;
	fBatteryActed = true;

	BString action("hibernate");
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
		&& path.Append("Power settings") == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		BMessage settings;
		if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK)
			action = settings.GetString("power:battery_critical_action",
				action.String());
	}
	fprintf(stderr, "registrar: battery critical (%d%%), action %s\n",
		capacitySum / count, action.String());

	if (fLogindBridge == NULL || action == "ignore")
		return;
	if (action == "suspend") {
		fLogindBridge->Suspend();
		return;
	}
	if (action == "hibernate" && fLogindBridge->CanHibernate()) {
		fLogindBridge->Hibernate();
		return;
	}

	// Power off, also when hibernate is not available: the same quit
	// dance as BRoster::ShutDown(), without the confirmation.
	if (fShutdownProcess != NULL)
		return;
	BMessage* request = new(nothrow) BMessage(B_REG_SHUT_DOWN);
	if (request == NULL)
		return;
	request->AddBool("reboot", false);
	request->AddBool("confirm", false);
	if (_CreateShutdownProcess(request) != B_OK) {
		delete request;
		return;
	}
	fShutdownProcess->Run();
}


void
Registrar::_ShowSleepWindow(bool hibernate)
{
	fSleepWindow.SendMessage(B_QUIT_REQUESTED);
	if (InitGUIContext() != B_OK)
		return;

	SleepWindow* window = new(std::nothrow) SleepWindow(hibernate);
	if (window == NULL)
		return;
	fSleepWindow = BMessenger(window);
	window->Go(NULL);
}


void
Registrar::_ShowSleepFailure(bool hibernate, const char *reason)
{
	BString text(hibernate
		? B_TRANSLATE("The system could not hibernate.")
		: B_TRANSLATE("The system could not suspend."));
	text << "\n\n";
	if (reason != NULL)
		text << reason;
	else
		text << B_TRANSLATE("A device or program refused to sleep; the "
			"system log has the details.");

	if (!fSleepWindow.IsValid())
		_ShowSleepWindow(hibernate);

	BMessage failed(kMsgSleepFailed);
	failed.AddString("text", text);
	fSleepWindow.SendMessage(&failed);
}


// A janus lock round-trip on its own short thread, like the sleep
// notices. The registrar looper never sits through the janus reply.
struct SleepLockRequest {
	BMessenger	registrar;
	int32		cycle;		// -1 = not part of a sleep cycle
	bool		automatic;	// sleep lock: skip autologin users
};


static status_t
sleep_lock_thread(void* data)
{
	SleepLockRequest* request = (SleepLockRequest*)data;
	port_id janus = find_port(B_LAUNCH_DAEMON_PORT_NAME);
	status_t result = B_ERROR;
	if (janus >= 0) {
		BPrivate::KMessage req(BPrivate::B_JANUS_LOCK_SESSION);
		// Sleep lock: skip autologin users. A manual lock omits this.
		if (request->automatic)
			req.AddBool("automatic", true);
		BPrivate::KMessage reply;
		status_t s = req.SendTo(janus, -1, &reply, 2000000LL, 2000000LL,
			getpid());
		if (s == B_OK)
			// B_NOT_ALLOWED: passwordless or autologin, nothing to lock.
			result = reply.What() == B_OK ? B_OK : B_NOT_ALLOWED;
		else
			result = s;
	}

	if (request->cycle >= 0) {
		BMessage done(kMsgSleepLockDone);
		done.AddInt32("cycle", request->cycle);
		done.AddInt32("result", result);
		request->registrar.SendMessage(&done);
	}
	delete request;
	return B_OK;
}


/*!	Asks janus to lock the screen on a short thread. The caller is
	never blocked on the round-trip.

	\return false if the request could not be scheduled. The sleep
	path must then release the inhibitor itself, or sleep would wait
	forever on a reply that cannot come.
*/
bool
Registrar::_RequestScreenLockAsync(bool automatic, int32 cycle)
{
	SleepLockRequest* request = new(std::nothrow) SleepLockRequest;
	if (request == NULL)
		return false;
	request->registrar = BMessenger(this);
	request->cycle = cycle;
	request->automatic = automatic;

	thread_id thread = spawn_thread(sleep_lock_thread, "sleep lock",
		B_NORMAL_PRIORITY, request);
	if (thread < B_OK || resume_thread(thread) != B_OK) {
		delete request;
		return false;
	}
	return true;
}


/*!	Requests the sleep-path screen lock. The lock must be in place
	before the delay inhibitor is released, so the resumed screen is
	already locked.
*/
void
Registrar::_RequestScreenLockForSleep()
{
	if (!_RequestScreenLockAsync(true, fSleepCycle)
		&& fLogindBridge != NULL) {
		// Could not schedule the request; do not hold the inhibitor.
		fLogindBridge->ReleaseSleepInhibit();
	}
}


void
Registrar::_HandleLogindSessionLock(BMessage* /*message*/)
{
	// loginctl lock-session: not automatic, so autologin users lock too.
	_RequestScreenLockAsync(false, -1);
}



//	#pragma mark -


/*!	\brief Creates and runs the registrar application.

	The main thread is renamed.

	\return 0.
*/
int
main()
{
	// stdout/stderr redirected to /var/log/registrar.log; unbuffer so
	// timing traces reflect reality instead of stdio buffer flush events.
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	FUNCTION_START();

	// Create the global be_clipboard manually -- it will not work, since it
	// wants to talk to the registrar in its constructor, but it doesn't have
	// to and we would otherwise deadlock when initializing our GUI in the
	// app thread.
	be_clipboard = new BClipboard(NULL);

	// create and run the registrar application
	status_t error;
	Registrar *app = new Registrar(&error);
	if (error != B_OK) {
		fprintf(stderr, "REG: Failed to create the BApplication: %s\n",
			strerror(error));
		return 1;
	}

	// rename the main thread
	rename_thread(find_thread(NULL), "roster");

	PRINT("app->Run()...\n");

	try {
		app->Run();
	} catch (std::exception& exception) {
		char buffer[1024];
		snprintf(buffer, sizeof(buffer),
			"registrar main() caught exception: %s", exception.what());
		debugger(buffer);
	} catch (...) {
		debugger("registrar main() caught unknown exception");
	}

	PRINT("delete app...\n");
	delete app;

	FUNCTION_END();
	return 0;
}

