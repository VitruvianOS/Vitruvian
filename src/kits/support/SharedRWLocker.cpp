/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <SharedRWLocker.h>

#include <libroot/futex.h>


#define WRITER_FLAG		((int32)0x80000000)
#define WRITER_WAITING	((int32)0x40000000)
#define READER_MASK		((int32)0x3FFFFFFF)


BSharedRWLocker::BSharedRWLocker()
	:
	fState(NULL)
{
}


BSharedRWLocker::BSharedRWLocker(shared_rw_lock_state* state)
	:
	fState(state)
{
}


BSharedRWLocker::~BSharedRWLocker()
{
}


void
BSharedRWLocker::SetTo(shared_rw_lock_state* state)
{
	fState = state;
}


bool
BSharedRWLocker::ReadLock()
{
	return ReadLockWithTimeout(B_INFINITE_TIMEOUT) == B_OK;
}


status_t
BSharedRWLocker::ReadLockWithTimeout(bigtime_t timeout)
{
	if (fState == NULL)
		return B_BAD_VALUE;
	bigtime_t deadline = futex_deadline(timeout);
	for (;;) {
		int32 state = __atomic_load_n(&fState->fState, __ATOMIC_ACQUIRE);

		if ((state & (WRITER_FLAG | WRITER_WAITING)) != 0) {
			bigtime_t left = futex_time_left(deadline);
			if (left < 0)
				return futex_timeout_error(timeout);
			status_t error = futex_wait_shared(&fState->fState, state,
				left);
			if (error == B_TIMED_OUT || futex_wait_failed(error))
				return error;
			continue;
		}

		if (__atomic_compare_exchange_n(&fState->fState, &state, state + 1,
				false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
			return B_OK;
		}
	}
}


void
BSharedRWLocker::ReadUnlock()
{
	if (fState == NULL)
		return;

	int32 old = __atomic_add_fetch(&fState->fState, -1, __ATOMIC_RELEASE);
	if ((old & READER_MASK) == 0
		&& (old & (WRITER_FLAG | WRITER_WAITING)) != 0) {
		futex_wake_all_shared(&fState->fState);
	}
}


bool
BSharedRWLocker::WriteLock()
{
	return WriteLockWithTimeout(B_INFINITE_TIMEOUT) == B_OK;
}


status_t
BSharedRWLocker::WriteLockWithTimeout(bigtime_t timeout)
{
	if (fState == NULL)
		return B_BAD_VALUE;
	bigtime_t deadline = futex_deadline(timeout);
	for (;;) {
		int32 state = __atomic_load_n(&fState->fState, __ATOMIC_ACQUIRE);

		// The winner clears the waiting bit, other writers set it again.
		if ((state & (WRITER_FLAG | READER_MASK)) == 0) {
			if (__atomic_compare_exchange_n(&fState->fState, &state,
					WRITER_FLAG, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
				return B_OK;
			}
			continue;
		}

		bigtime_t left = futex_time_left(deadline);
		status_t error = B_TIMED_OUT;
		if (left >= 0) {
			__atomic_or_fetch(&fState->fState, WRITER_WAITING,
				__ATOMIC_RELAXED);
			error = futex_wait_shared(&fState->fState,
				state | WRITER_WAITING, left);
		}
		if (error == B_TIMED_OUT || futex_wait_failed(error)) {
			// Readers would wait on the bit forever, so drop it and let
			// the writers still waiting set it again.
			__atomic_fetch_and(&fState->fState, ~WRITER_WAITING,
				__ATOMIC_RELEASE);
			futex_wake_all_shared(&fState->fState);
			return left < 0 ? futex_timeout_error(timeout) : error;
		}
	}
}


void
BSharedRWLocker::WriteUnlock()
{
	if (fState == NULL)
		return;

	// Waiting writers keep their bit.
	__atomic_fetch_and(&fState->fState, ~WRITER_FLAG, __ATOMIC_RELEASE);
	futex_wake_all_shared(&fState->fState);
}


bool
BSharedRWLocker::IsReadLocked() const
{
	if (fState == NULL)
		return false;

	return (__atomic_load_n(&fState->fState, __ATOMIC_ACQUIRE)
		& READER_MASK) > 0;
}


bool
BSharedRWLocker::IsWriteLocked() const
{
	if (fState == NULL)
		return false;

	return (__atomic_load_n(&fState->fState, __ATOMIC_ACQUIRE)
		& WRITER_FLAG) != 0;
}
