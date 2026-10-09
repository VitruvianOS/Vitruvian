/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LOGIND_BRIDGE_H
#define _LOGIND_BRIDGE_H


#include <Messenger.h>
#include <OS.h>


class DisplayResumeGuard;


// Internal registrar messages posted by LogindBridge when logind signals fire.
static const uint32 kMsgLogindPrepareForShutdown = 'lPfS';
static const uint32 kMsgLogindPrepareForSleep    = 'lPfL';
// A Suspend/Hibernate request logind refused. Fields: "hibernate" (bool),
// "reason" (string).
static const uint32 kMsgLogindSleepRefused       = 'lSlR';
static const uint32 kMsgLogindSessionLock        = 'lSnL';
static const uint32 kMsgLogindSessionUnlock      = 'lSnU';

// Field: "active" (bool) — true = about to happen, false = resume.
// Field: "type" (string, optional), from PrepareForShutdownWithMetadata:
// "poweroff", "reboot", "halt", "kexec", "soft-reboot", ...


class LogindBridge {
public:
					LogindBridge(const BMessenger& target);
					~LogindBridge();

	status_t		Start();
	void			Stop();

	// Called by Registrar after the quit dance is done (or immediately
	// on PrepareForShutdown(true) if _IsShutDownInProgress). Releases the
	// shutdown inhibit fd so systemd proceeds.
	void			ReleaseShutdownInhibit();

	// Called after B_SYSTEM_SUSPENDING has been broadcast to all apps.
	// Releases the sleep inhibit fd so systemd suspends. The sleep lock
	// is automatically re-acquired inside LogindBridge on PrepareForSleep(false).
	void			ReleaseSleepInhibit();

	// Manager.CanSuspend / CanHibernate. Available when the answer is
	// "yes" or "challenge"; "no" and "na" count as unavailable. Also
	// false when the active display driver cannot resume.
	bool			CanSuspend();
	bool			CanHibernate();

	// Non-owning. Detect() must be called before Start() for the block inhibitor
	// and the CanSuspend/CanHibernate gates to apply.
	void			SetDisplayGuard(DisplayResumeGuard* guard);

	// Calls Manager.Suspend/Hibernate with interactive=true so polkit can ask.
	// The bridge thread runs the call, so the caller's looper never blocks on a polkit prompt.
	status_t		Suspend();
	status_t		Hibernate();

private:
	static int32	_ThreadEntry(void* self);
	int32			_ThreadLoop();

	status_t		_AcquireShutdownInhibit();
	status_t		_AcquireSleepInhibit();
	status_t		_AcquireBlockSleepInhibit();
	void			_ReleaseBlockSleepInhibit();
	void			_UpdateSleepAvailability();
	bool			_CanSleep(void* bus, const char* method);
	status_t		_RequestSleep(const char* method);
	void			_ProcessPendingSleep();

	BMessenger		fTarget;
	void*			fBus;			// sd_bus*
	int				fShutdownFd;	// delay inhibit for shutdown
	int				fSleepFd;		// delay inhibit for sleep
	int				fBlockSleepFd;	// block inhibit while guard applies
	DisplayResumeGuard* fDisplayGuard;
	thread_id		fThread;
	bool			fRunning;
	int32			fPendingSleep;	// sleep_request the bridge thread runs
	bigtime_t		fSleepCheckTime;
	bool			fCanSuspend;
	bool			fCanHibernate;
};


#endif	// _LOGIND_BRIDGE_H
