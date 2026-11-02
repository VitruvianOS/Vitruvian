/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _OS_SUPPORT_SHARED_LOCKER_H
#define _OS_SUPPORT_SHARED_LOCKER_H


#include <OS.h>
#include <SupportDefs.h>


/* Mutex on a word the caller places in memory shared with its peers, an
 * area or a MAP_SHARED mapping. There is no kernel object. A peer dying
 * with the lock held is left to the protocol that shares the word. */
struct shared_lock_state {
	int32	fState;
	int32	_reserved[3];
};


class BSharedLocker {
public:
							BSharedLocker();
							BSharedLocker(shared_lock_state* state);
	virtual					~BSharedLocker();

			void				SetTo(shared_lock_state* state);

			bool				Lock();
			status_t			LockWithTimeout(bigtime_t timeout);
			void				Unlock();

			bool				IsLocked() const;

private:
							BSharedLocker(const BSharedLocker&);
			BSharedLocker&		operator=(const BSharedLocker&);

			shared_lock_state*	fState;
			bool				fLocked;
};


#endif	// _OS_SUPPORT_SHARED_LOCKER_H
