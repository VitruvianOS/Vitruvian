/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <SharedEvent.h>

#include <libroot/futex.h>


#define COUNT_MASK		((int32)0x3FFFFFFF)
#define EPOCH_FLAG		((int32)0x40000000)
#define DEAD_FLAG		((int32)0x80000000)


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


status_t
BSharedEvent::Acquire(int32 count, uint32 flags, bigtime_t timeout)
{
	if (fState == NULL || count <= 0 || count > COUNT_MASK)
		return B_BAD_VALUE;

	bigtime_t relative = B_INFINITE_TIMEOUT;
	if ((flags & B_ABSOLUTE_TIMEOUT) != 0) {
		if (timeout != B_INFINITE_TIMEOUT) {
			bigtime_t now = (flags & B_TIMEOUT_REAL_TIME_BASE) != 0
				? real_time_clock_usecs() : system_time();
			relative = timeout > now ? timeout - now : 0;
		}
	} else if ((flags & B_RELATIVE_TIMEOUT) != 0)
		relative = timeout < 0 ? 0 : timeout;
	bigtime_t deadline = futex_deadline(relative);

	// Registered before the count is read, so a release either sees the
	// waiter or the waiter sees the count.
	__atomic_add_fetch(&fState->fWaiters, 1, __ATOMIC_SEQ_CST);
	int32 epoch = __atomic_load_n(&fState->fCount, __ATOMIC_SEQ_CST)
		& EPOCH_FLAG;
	status_t result;

	for (;;) {
		int32 state = __atomic_load_n(&fState->fCount, __ATOMIC_ACQUIRE);
		if ((state & DEAD_FLAG) != 0) {
			result = B_BAD_SEM_ID;
			break;
		}
		if ((state & EPOCH_FLAG) != epoch) {
			result = B_OK;
			break;
		}
		if ((state & COUNT_MASK) >= count) {
			if (__atomic_compare_exchange_n(&fState->fCount, &state,
					state - count, false, __ATOMIC_ACQUIRE,
					__ATOMIC_RELAXED)) {
				result = B_OK;
				break;
			}
			continue;
		}

		bigtime_t left = futex_time_left(deadline);
		if (left < 0) {
			result = futex_timeout_error(relative);
			break;
		}
		// A release changes the word, so the wait returns at once.
		status_t error = futex_wait_shared(&fState->fCount, state, left);
		if (error == B_TIMED_OUT || futex_wait_failed(error)) {
			result = error;
			break;
		}
	}

	__atomic_sub_fetch(&fState->fWaiters, 1, __ATOMIC_SEQ_CST);
	return result;
}


status_t
BSharedEvent::Release(int32 count, uint32 flags)
{
	if (fState == NULL)
		return B_BAD_VALUE;

	for (;;) {
		int32 state = __atomic_load_n(&fState->fCount, __ATOMIC_RELAXED);
		if ((state & DEAD_FLAG) != 0)
			return B_BAD_SEM_ID;

		int32 value;
		if ((flags & B_RELEASE_ALL) != 0)
			value = (state & EPOCH_FLAG) ^ EPOCH_FLAG;
		else {
			if (count <= 0)
				return B_BAD_VALUE;
			int32 total = (state & COUNT_MASK) + count;
			if (total < 0 || total > COUNT_MASK)
				total = COUNT_MASK;
			value = (state & EPOCH_FLAG) | total;
		}
		if (__atomic_compare_exchange_n(&fState->fCount, &state, value, false,
				__ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
			break;
		}
	}

	// Waiters ask for different counts, so a single wake could land on one
	// that cannot proceed.
	if (__atomic_load_n(&fState->fWaiters, __ATOMIC_SEQ_CST) > 0)
		futex_wake_all_shared(&fState->fCount);
	return B_OK;
}


status_t
BSharedEvent::Count(int32* count) const
{
	if (fState == NULL || count == NULL)
		return B_BAD_VALUE;

	int32 state = __atomic_load_n(&fState->fCount, __ATOMIC_ACQUIRE);
	if ((state & DEAD_FLAG) != 0)
		return B_BAD_SEM_ID;
	*count = state & COUNT_MASK;
	return B_OK;
}


void
BSharedEvent::Post()
{
	Release(1, 0);
}


status_t
BSharedEvent::Wait()
{
	return Acquire(1, 0, 0);
}


status_t
BSharedEvent::WaitWithTimeout(bigtime_t timeout)
{
	return Acquire(1, B_RELATIVE_TIMEOUT, timeout);
}


void
BSharedEvent::Reset()
{
	if (fState == NULL)
		return;

	int32 state = __atomic_load_n(&fState->fCount, __ATOMIC_RELAXED);
	while (!__atomic_compare_exchange_n(&fState->fCount, &state,
			state & ~COUNT_MASK, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
	}
}


void
BSharedEvent::MarkDead()
{
	if (fState == NULL)
		return;

	__atomic_fetch_or(&fState->fCount, DEAD_FLAG, __ATOMIC_SEQ_CST);
	futex_wake_all_shared(&fState->fCount);
}


bool
BSharedEvent::IsDead() const
{
	return fState != NULL
		&& (__atomic_load_n(&fState->fCount, __ATOMIC_ACQUIRE)
			& DEAD_FLAG) != 0;
}
