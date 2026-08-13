/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */
#ifndef _GLIB_SYNC_TIMEOUT_H
#define _GLIB_SYNC_TIMEOUT_H


#include <OS.h>
#include <SupportDefs.h>

#include <gio/gio.h>


namespace BPrivate {


/*!	Bounds a synchronous GIO call that has no timeout of its own
	(nm_client_new(), g_bus_get_sync()).  Without this an unresponsive or
	absent system bus blocks the calling thread forever -- and both backends
	are first constructed from Deskbar's window thread when the status
	replicant is attached, so that freezes the whole desktop.

	Usage:
		GLibSyncTimeout guard(10 * 1000000LL);
		x = some_sync_call(guard.Cancellable(), &error);
		guard.Stop();
*/
class GLibSyncTimeout {
public:
	GLibSyncTimeout(bigtime_t timeout)
		:
		fCancellable(g_cancellable_new()),
		fThread(-1),
		fDeadline(system_time() + timeout),
		fDone(0),
		fExited(0)
	{
		fThread = spawn_thread(_Watchdog, "glib_sync_timeout",
			B_NORMAL_PRIORITY, this);
		if (fThread < B_OK)
			return;
		resume_thread(fThread);
	}

	~GLibSyncTimeout()
	{
		Stop();
		if (fCancellable != NULL)
			g_object_unref(fCancellable);
	}

	GCancellable* Cancellable() const { return fCancellable; }

	//! Joins the watchdog; safe to call more than once.
	void Stop()
	{
		if (fThread < B_OK)
			return;
		atomic_set(&fDone, 1);
		status_t result;
		wait_for_thread(fThread, &result);
		// wait_for_thread() can return before the watchdog has actually left
		// _Watchdog() (e.g. it had not registered yet when we joined). This
		// object lives on the caller's stack, so returning early would leave
		// the thread dereferencing a dead frame -- wait for its own flag.
		while (atomic_get(&fExited) == 0)
			snooze(1000);
		fThread = -1;
	}

private:
	static const bigtime_t kPollInterval = 10000;

	static int32 _Watchdog(void* data)
	{
		GLibSyncTimeout* self = (GLibSyncTimeout*)data;
		while (atomic_get(&self->fDone) == 0) {
			if (system_time() >= self->fDeadline) {
				g_cancellable_cancel(self->fCancellable);
				break;
			}
			snooze(kPollInterval);
		}
		atomic_set(&self->fExited, 1);
		return 0;
	}

	GCancellable*	fCancellable;
	thread_id		fThread;
	bigtime_t		fDeadline;
	int32			fDone;
	int32			fExited;
};


}	// namespace BPrivate


#endif	// _GLIB_SYNC_TIMEOUT_H
