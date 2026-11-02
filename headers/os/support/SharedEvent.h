/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OS_SUPPORT_SHARED_EVENT_H
#define _OS_SUPPORT_SHARED_EVENT_H


#include <OS.h>
#include <SupportDefs.h>


/* Counting event on a shared word, placed as for BSharedLocker. It follows
 * the kernel semaphore API: Acquire() and Release() take the same flags and
 * timeouts as acquire_sem_etc() and release_sem_etc().
 *
 * fCount: bits 0 to 29 the count, bit 30 the release-all epoch, bit 31 the
 * dead bit. fWaiters counts the threads parked in Acquire().
 *
 * Waiters are woken through futex wake, so they are not served strictly in
 * FIFO order. Count() never reports waiters, only the available count.
 * B_RELEASE_ALL wakes every waiter, each returns B_OK without consuming
 * and the count is zeroed.
 *
 * Dead: a peer that learns the other side is gone calls MarkDead(). Every
 * Acquire() then returns B_BAD_SEM_ID, as for a deleted kernel semaphore,
 * Release() does nothing and returns B_BAD_SEM_ID. A dead word stays dead. */
struct shared_event_state {
	int32	fCount;
	int32	fWaiters;
	int32	_reserved[2];
};


class BSharedEvent {
public:
							BSharedEvent();
							BSharedEvent(shared_event_state* state);
	virtual					~BSharedEvent();

			void				SetTo(shared_event_state* state);

			status_t			Acquire(int32 count = 1, uint32 flags = 0,
									bigtime_t timeout = 0);
			status_t			Release(int32 count = 1, uint32 flags = 0);
			status_t			Count(int32* count) const;

			void				Post();
			status_t			Wait();
			status_t			WaitWithTimeout(bigtime_t timeout);

			void				Reset();

			void				MarkDead();
			bool				IsDead() const;

private:
							BSharedEvent(const BSharedEvent&);
			BSharedEvent&		operator=(const BSharedEvent&);

			shared_event_state*	fState;
};


#endif	// _OS_SUPPORT_SHARED_EVENT_H
