/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <errno.h>
#include <signal.h>
#include <syscall.h>
#include <unistd.h>
#include <syscalls.h>
#include <signal_defs.h>


extern status_t
_kern_send_signal(int32 id, uint32 signal,
	const union sigval* userValue, uint32 flags)
{
	// SIGNAL_FLAG_SEND_TO_THREAD means id is a tid, not a pid.
	// kill() only works with pids; use tgkill() to target a specific thread.
	int result;
	if (id < 0)
		result = kill(id, signal);
	else if (flags & SIGNAL_FLAG_SEND_TO_THREAD)
		result = syscall(SYS_tgkill, (long)getpid(), (long)id, (long)signal);
	else
		result = kill(id, signal);

	if (result == 0)
		return B_OK;
	switch (errno) {
		case EPERM:
			return B_NOT_ALLOWED;
		case ESRCH:
			return B_BAD_THREAD_ID;
		case EINVAL:
			return B_BAD_VALUE;
		default:
			return B_ERROR;
	}
}
