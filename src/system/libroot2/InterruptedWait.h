/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LIBROOT2_INTERRUPTED_WAIT_H
#define _LIBROOT2_INTERRUPTED_WAIT_H


#include <OS.h>


namespace BKernelPrivate {


// nexus returns B_INTERRUPTED for any wakeup, including the suspend freezer's, so repeat the
// wait here unless the caller passed B_CAN_INTERRUPT.

// Call once before the first wait. nexus port waits only take relative
// timeouts, so an absolute one is converted in place.
static inline bigtime_t
wait_deadline(uint32* flags, bigtime_t* timeout)
{
	if ((*flags & B_ABSOLUTE_TIMEOUT) != 0) {
		bigtime_t deadline = *timeout;
		*flags = (*flags & ~B_ABSOLUTE_TIMEOUT) | B_RELATIVE_TIMEOUT;
		*timeout = deadline - system_time();
		if (*timeout < 0)
			*timeout = 0;
		return deadline;
	}
	if ((*flags & B_RELATIVE_TIMEOUT) != 0 && *timeout > 0
		&& *timeout != B_INFINITE_TIMEOUT)
		return system_time() + *timeout;
	return -1;
}


// Returns true when the wait has to be repeated with *timeout. A wait
// that ran out while interrupted ends with B_TIMED_OUT in *status.
static inline bool
wait_again(status_t* status, uint32 flags, bigtime_t deadline,
	bigtime_t* timeout)
{
	// Only an absolute deadline already past reaches nexus as a zero
	// relative timeout; that is a timeout, not B_WOULD_BLOCK.
	if (*status == B_WOULD_BLOCK && deadline >= 0) {
		*status = B_TIMED_OUT;
		return false;
	}
	if (*status != B_INTERRUPTED || (flags & B_CAN_INTERRUPT) != 0)
		return false;

	if (deadline >= 0) {
		*timeout = deadline - system_time();
		if (*timeout <= 0) {
			*status = B_TIMED_OUT;
			return false;
		}
	}
	return true;
}


}	// namespace BKernelPrivate


#endif	// _LIBROOT2_INTERRUPTED_WAIT_H
