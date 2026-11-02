/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <SharedEvent.h>

#include <libroot/futex.h>


BSharedEvent::BSharedEvent()
	:
	fState(NULL)
{
}


BSharedEvent::BSharedEvent(shared_event_state* state)
	:
	fState(state)
{
}


BSharedEvent::~BSharedEvent()
{
}


void
BSharedEvent::SetTo(shared_event_state* state)
{
	fState = state;
}


void
BSharedEvent::Post()
{
	if (fState == NULL)
		return;

	__atomic_add_fetch(&fState->fCount, 1, __ATOMIC_RELEASE);
	// Waiters are not counted, so every post wakes one.
	futex_wake_shared(&fState->fCount, 1);
}


status_t
BSharedEvent::Wait()
{
	if (fState == NULL)
		return B_BAD_VALUE;

	for (;;) {
		int32 count = __atomic_load_n(&fState->fCount, __ATOMIC_ACQUIRE);
		if (count > 0) {
			if (__atomic_compare_exchange_n(&fState->fCount, &count,
					count - 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				return B_OK;
			}
			continue;
		}

		// A Post in between changes the word and the wait returns.
		status_t error = futex_wait_shared(&fState->fCount, count, 0);
		if (futex_wait_failed(error))
			return error;
	}
}


status_t
BSharedEvent::WaitWithTimeout(bigtime_t timeout)
{
	if (fState == NULL)
		return B_BAD_VALUE;
	bigtime_t deadline = futex_deadline(timeout);
	for (;;) {
		int32 count = __atomic_load_n(&fState->fCount, __ATOMIC_ACQUIRE);
		if (count > 0) {
			if (__atomic_compare_exchange_n(&fState->fCount, &count,
					count - 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				return B_OK;
			}
			continue;
		}

		bigtime_t left = futex_time_left(deadline);
		if (left < 0)
			return futex_timeout_error(timeout);
		status_t error = futex_wait_shared(&fState->fCount, count, left);
		if (error == B_TIMED_OUT || futex_wait_failed(error))
			return error;
	}
}


void
BSharedEvent::Reset()
{
	if (fState == NULL)
		return;

	__atomic_store_n(&fState->fCount, 0, __ATOMIC_RELEASE);
}
