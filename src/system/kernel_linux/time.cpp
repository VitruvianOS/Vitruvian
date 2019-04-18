/*
 * Copyright 2018, Dario Casalinuovo.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>


status_t
_kern_set_timer(int32 timerID, thread_id threadID,
	bigtime_t startTime, bigtime_t interval, uint32 flags,
	struct user_timer_info* oldInfo)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
