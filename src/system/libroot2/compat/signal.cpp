/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <OS.h>

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
	if (flags & SIGNAL_FLAG_SEND_TO_THREAD)
		return syscall(SYS_tgkill, (long)getpid(), (long)id, (long)signal);

	return kill(id, signal);
}
