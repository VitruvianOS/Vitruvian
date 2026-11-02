/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <SharedLocker.h>

#include <libroot/futex.h>


#define DEAD_FLAG	((int32)0x80000000)


BSharedLocker::BSharedLocker()
	:
	fState(NULL),
	fLocked(false)
{
}


BSharedLocker::BSharedLocker(shared_lock_state* state)
	:
	fState(state),
	fLocked(false)
{
}


BSharedLocker::~BSharedLocker()
{
}


void
BSharedLocker::SetTo(shared_lock_state* state)
{
	Unlock();
	fState = state;
	fLocked = false;
}


bool
BSharedLocker::Lock()
{
	return LockWithTimeout(B_INFINITE_TIMEOUT) == B_OK;
}


status_t
BSharedLocker::LockWithTimeout(bigtime_t timeout)
{
	if (fState == NULL)
		return B_BAD_VALUE;

	int32 expected = 0;
	if (__atomic_compare_exchange_n(&fState->fState, &expected, 1, false,
			__ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		fLocked = true;
		return B_OK;
	}

	// 0 free, 1 held, 2 held with waiters
	bigtime_t deadline = futex_deadline(timeout);
	while (true) {
		int32 state = __atomic_load_n(&fState->fState, __ATOMIC_ACQUIRE);
		if ((state & DEAD_FLAG) != 0)
			return B_BAD_SEM_ID;

		if (state == 0) {
			if (__atomic_compare_exchange_n(&fState->fState, &state, 2, false,
					__ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				fLocked = true;
				return B_OK;
			}
			continue;
		}
		if (state == 1 && !__atomic_compare_exchange_n(&fState->fState,
				&state, 2, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
			continue;
		}

		bigtime_t left = futex_time_left(deadline);
		if (left < 0)
			return futex_timeout_error(timeout);
		status_t error = futex_wait_shared(&fState->fState, 2, left);
		if (error == B_TIMED_OUT || futex_wait_failed(error))
			return error;
	}
}


void
BSharedLocker::Unlock()
{
	if (fState == NULL || !fLocked)
		return;

	fLocked = false;
	int32 state = __atomic_load_n(&fState->fState, __ATOMIC_RELAXED);
	for (;;) {
		if ((state & DEAD_FLAG) != 0)
			return;
		if (__atomic_compare_exchange_n(&fState->fState, &state, 0, false,
				__ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
			break;
		}
	}
	if (state == 2)
		futex_wake_shared(&fState->fState, 1);
}


void
BSharedLocker::MarkDead()
{
	if (fState == NULL)
		return;

	__atomic_fetch_or(&fState->fState, DEAD_FLAG, __ATOMIC_SEQ_CST);
	futex_wake_all_shared(&fState->fState);
}


bool
BSharedLocker::IsDead() const
{
	return fState != NULL
		&& (__atomic_load_n(&fState->fState, __ATOMIC_ACQUIRE)
			& DEAD_FLAG) != 0;
}


bool
BSharedLocker::IsLocked() const
{
	return fLocked;
}
