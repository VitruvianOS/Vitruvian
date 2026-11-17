/*
 * Copyright 2018-2025, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <errno.h>
#include <time.h>

#include <linux/futex.h>
#include <sys/syscall.h>

#include <syscalls.h>


static inline int
futex_wait(int32* addr, int32 expected, const struct timespec* until)
{
	if (until == NULL)
		return syscall(SYS_futex, addr, FUTEX_WAIT, expected, NULL, NULL, 0);
	return syscall(SYS_futex, addr, FUTEX_WAIT_BITSET, expected, until, NULL,
		FUTEX_BITSET_MATCH_ANY);
}


static inline int
futex_wake(int32* addr)
{
	return syscall(SYS_futex, addr, FUTEX_WAKE, 1, NULL, NULL, 0);
}


status_t
_kern_mutex_lock(int32* mutex, const char* name, uint32 flags,
		bigtime_t timeout)
{
	// 0 free, 1 held, 2 held with waiters.
	int32 expected = 0;
	if (__atomic_compare_exchange_n(mutex, &expected, 1, false,
			__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return B_OK;

	// Absolute CLOCK_MONOTONIC deadline, computed once.
	struct timespec until;
	struct timespec* untilp = NULL;
	if (timeout > 0 && clock_gettime(CLOCK_MONOTONIC, &until) == 0) {
		bigtime_t at = (bigtime_t)until.tv_sec * 1000000
			+ until.tv_nsec / 1000 + timeout;
		until.tv_sec = at / 1000000;
		until.tv_nsec = (at % 1000000) * 1000;
		untilp = &until;
	}

	while (__atomic_exchange_n(mutex, 2, __ATOMIC_ACQUIRE) != 0) {
		int result = futex_wait(mutex, 2, untilp);
		if (untilp != NULL && result == -1) {
			if (errno == ETIMEDOUT)
				return B_TIMED_OUT;
			if (errno == EINTR)
				return B_INTERRUPTED;
		}
	}
	return B_OK;
}


status_t
_kern_mutex_unlock(int32* mutex, uint32 flags)
{
	if (__atomic_exchange_n(mutex, 0, __ATOMIC_RELEASE) == 2)
		futex_wake(mutex);
	return B_OK;
}


status_t
_kern_mutex_switch_lock(int32* fromMutex, int32* toMutex,
		const char* name, uint32 flags, bigtime_t timeout)
{
	if (_kern_mutex_unlock(fromMutex, flags) != B_OK)
		return B_ERROR;

	return _kern_mutex_lock(toMutex, name, flags, timeout);
}
