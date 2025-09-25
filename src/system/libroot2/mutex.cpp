/*
 * Copyright 2018-2025, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>

#include <syscalls.h>


static inline int
futex_wait(int32* addr, int32 expected, bigtime_t timeout) {
	struct timespec ts;
	if (timeout > 0) {
		ts.tv_sec = timeout / 1000000000;
		ts.tv_nsec = timeout % 1000000000;
		return syscall(SYS_futex, addr, FUTEX_WAIT,
			expected, &ts, NULL, 0);
	} else {
		return syscall(SYS_futex, addr, FUTEX_WAIT,
			expected, NULL, NULL, 0);
	}
}


static inline int
futex_wake(int32* addr) {
	return syscall(SYS_futex, addr, FUTEX_WAKE, 1, NULL, NULL, 0);
}


status_t
_kern_mutex_lock(int32* mutex, const char* name, uint32 flags,
		bigtime_t timeout) {
	int32 expected = 0;
	while (__atomic_exchange_n(mutex, 1, __ATOMIC_ACQUIRE) != 0) {
		if (timeout > 0) {
			if (futex_wait(mutex, expected, timeout) == -1) {
				if (errno == EAGAIN)
					return B_TIMED_OUT;
			}
		} else {
			while (*mutex != 0);
		}
	}
	return B_OK;
}


status_t
_kern_mutex_unlock(int32* mutex, uint32 flags) {
	if (__atomic_exchange_n(mutex, 0, __ATOMIC_RELEASE) == 1)
		futex_wake(mutex);
	return B_OK;
}


status_t
_kern_mutex_switch_lock(int32* fromMutex, int32* toMutex,
		const char* name, uint32 flags, bigtime_t timeout) {
	if (_kern_mutex_unlock(fromMutex, flags) != B_OK)
		return B_ERROR;

	return _kern_mutex_lock(toMutex, name, flags, timeout);
}
