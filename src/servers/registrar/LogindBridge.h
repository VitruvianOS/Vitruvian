/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LOGIND_BRIDGE_H
#define _LOGIND_BRIDGE_H


#include <Messenger.h>
#include <OS.h>


// Internal registrar messages posted by LogindBridge when logind signals fire.
static const uint32 kMsgLogindPrepareForShutdown = 'lPfS';
static const uint32 kMsgLogindPrepareForSleep    = 'lPfL';

// Field: "active" (bool) — true = about to happen, false = resume.


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

private:
	static int32	_ThreadEntry(void* self);
	int32			_ThreadLoop();

	status_t		_AcquireShutdownInhibit();
	status_t		_AcquireSleepInhibit();

	BMessenger		fTarget;
	void*			fBus;			// sd_bus*
	int				fShutdownFd;	// delay inhibit for shutdown
	int				fSleepFd;		// delay inhibit for sleep
	thread_id		fThread;
	bool			fRunning;
};


#endif	// _LOGIND_BRIDGE_H
