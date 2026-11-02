/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OS_SUPPORT_SHARED_LOCKER_H
#define _OS_SUPPORT_SHARED_LOCKER_H


#include <pthread.h>

#include <OS.h>
#include <SupportDefs.h>


/* Mutex on a word the caller places in memory shared with its peers, an
 * area or a MAP_SHARED mapping. There is no kernel object. fState holds
 * 0 free, 1 held, 2 held with waiters, and bit 31 is the dead bit.
 *
 * A peer dying with the lock held cannot release it. The survivor that
 * learns of the death calls MarkDead(): every Lock() then fails with
 * B_BAD_SEM_ID, Unlock() does nothing, and blocked waiters are woken. */
struct shared_lock_state {
	int32	fState;
	int32	_reserved[3];
};


/* Robust mode: a process shared robust pthread mutex, so the kernel
 * reports a holder that dies. Call BSharedLocker::InitRobust() once in the
 * creator, then SetRobust() in every peer. The word layout above is not
 * touched.
 *
 * A Lock() that finds the previous holder dead marks the mutex consistent,
 * sets fDead, releases it and fails with B_BAD_SEM_ID, as for MarkDead().
 * The protected data is not trusted after an owner death, so there is no
 * success-with-flag result. Every later lock fails with B_BAD_SEM_ID.
 * MarkDead() in this mode only sets fDead: threads already blocked in the
 * mutex wake when the holder unlocks or dies. */
struct shared_robust_locker_state {
	pthread_mutex_t	fMutex;
	int32			fDead;
	int32			_reserved[3];
};


class BSharedLocker {
public:
							BSharedLocker();
							BSharedLocker(shared_lock_state* state);
	virtual					~BSharedLocker();

			void				SetTo(shared_lock_state* state);

	static	status_t			InitRobust(shared_robust_locker_state* state);
			void				SetRobust(
									shared_robust_locker_state* state);

			bool				Lock();
			status_t			LockWithTimeout(bigtime_t timeout);
			void				Unlock();

			bool				IsLocked() const;

			void				MarkDead();
			bool				IsDead() const;

private:
							BSharedLocker(const BSharedLocker&);
			BSharedLocker&		operator=(const BSharedLocker&);

			shared_lock_state*	fState;
			shared_robust_locker_state*	fRobust;
			bool				fLocked;
};


#endif	// _OS_SUPPORT_SHARED_LOCKER_H
