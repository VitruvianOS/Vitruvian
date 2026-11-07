/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LIBROOT_FUTEX_H
#define _LIBROOT_FUTEX_H


#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <OS.h>
#include <SupportDefs.h>


/* Private ops, for words only this team maps. A timeout of 0 or less waits
 * forever. */
static inline status_t
futex_wait(volatile int32* addr, int32 expected, bigtime_t timeoutUs)
{
	struct timespec ts;
	struct timespec* tsp = NULL;
	if (timeoutUs > 0) {
		ts.tv_sec = timeoutUs / 1000000;
		ts.tv_nsec = (timeoutUs % 1000000) * 1000;
		tsp = &ts;
	}

	int result = syscall(SYS_futex, (int*)addr, FUTEX_WAIT_PRIVATE, expected,
		tsp, NULL, 0);
	if (result == 0)
		return B_OK;
	if (errno == ETIMEDOUT)
		return B_TIMED_OUT;
	if (errno == EINTR)
		return B_INTERRUPTED;
	if (errno == EAGAIN)
		return B_BUSY;
	return B_ERROR;
}


static inline status_t
futex_wake(volatile int32* addr, int32 count)
{
	syscall(SYS_futex, (int*)addr, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
	return B_OK;
}


static inline status_t
futex_wake_all(volatile int32* addr)
{
	return futex_wake(addr, INT_MAX);
}


/* Shared ops, for words in an area or a MAP_SHARED mapping. */
static inline status_t
futex_wait_shared(volatile int32* addr, int32 expected, bigtime_t timeoutUs)
{
	struct timespec ts;
	struct timespec* tsp = NULL;
	if (timeoutUs > 0) {
		ts.tv_sec = timeoutUs / 1000000;
		ts.tv_nsec = (timeoutUs % 1000000) * 1000;
		tsp = &ts;
	}

	int result = syscall(SYS_futex, (int*)addr, FUTEX_WAIT, expected, tsp,
		NULL, 0);
	if (result == 0)
		return B_OK;
	if (errno == ETIMEDOUT)
		return B_TIMED_OUT;
	if (errno == EINTR)
		return B_INTERRUPTED;
	if (errno == EAGAIN)
		return B_BUSY;
	return B_ERROR;
}


static inline status_t
futex_wake_shared(volatile int32* addr, int32 count)
{
	syscall(SYS_futex, (int*)addr, FUTEX_WAKE, count, NULL, NULL, 0);
	return B_OK;
}


static inline status_t
futex_wake_all_shared(volatile int32* addr)
{
	return futex_wake_shared(addr, INT_MAX);
}


/* What a lock returns when it gives up, a zero timeout only tries. */
static inline status_t
futex_timeout_error(bigtime_t timeout)
{
	return timeout == 0 ? B_WOULD_BLOCK : B_TIMED_OUT;
}


/* True when a wait failed for a reason retrying will not fix. */
static inline bool
futex_wait_failed(status_t error)
{
	return error != B_OK && error != B_BUSY && error != B_INTERRUPTED
		&& error != B_TIMED_OUT;
}


/* Time left before deadline, 0 for no deadline, negative once it passed. */
static inline bigtime_t
futex_deadline(bigtime_t timeout)
{
	if (timeout == B_INFINITE_TIMEOUT)
		return 0;
	return system_time() + (timeout > 0 ? timeout : 0);
}


static inline bigtime_t
futex_time_left(bigtime_t deadline)
{
	if (deadline == 0)
		return 0;
	bigtime_t left = deadline - system_time();
	return left > 0 ? left : -1;
}


#endif	// _LIBROOT_FUTEX_H
