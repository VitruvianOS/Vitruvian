/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SHARED_BFUTEX_LOCKER_H
#define _SHARED_BFUTEX_LOCKER_H


#include <OS.h>
#include <SupportDefs.h>

#include <support/UserSemaphore.h>


/* Non-recursive lock with the BLocker calls. The owner locking twice
 * deadlocks, code that re-enters uses BLocker. */
class BFutexLocker {
public:
					BFutexLocker();
					BFutexLocker(const char* name);
	virtual				~BFutexLocker();

		status_t		InitCheck() const;

		bool			Lock();
		status_t		LockWithTimeout(bigtime_t timeout);
		void			Unlock();

		thread_id		LockingThread() const;
		bool			IsLocked() const;

private:
					BFutexLocker(const BFutexLocker&);
		BFutexLocker&	operator=(const BFutexLocker&);
		bool			AcquireLock(bigtime_t timeout, status_t* error);

		int32			fBenaphoreCount;
		int32			fOwner;
		user_sem		fSem;
};


#endif	// _SHARED_BFUTEX_LOCKER_H
