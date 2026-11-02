/*
 * Copyright 2001-2009 Haiku, Inc. All rights reserved.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Erik Jaesler, erik@cgsoftware.com
 */

#include <OS.h>
#include <Locker.h>
#include <SupportDefs.h>

#include <stdio.h>

#include <support/UserSemaphore.h>


// A user_sem lives in the private tail of the object.
static inline user_sem*
semaphore_of(int32* lock)
{
	return (user_sem*)lock;
}


BLocker::BLocker()
{
	InitLocker(NULL, true);
}


BLocker::BLocker(const char *name)
{
	InitLocker(name, true);
}


BLocker::BLocker(bool benaphoreStyle)
{
	InitLocker(NULL, benaphoreStyle);
}


BLocker::BLocker(const char *name, bool benaphoreStyle)
{
	InitLocker(name, benaphoreStyle);
}


// Kept for binary compatibility, the last argument is ignored.
BLocker::BLocker(const char *name, bool benaphoreStyle,
	bool)
{
	InitLocker(name, benaphoreStyle);
}


BLocker::~BLocker()
{
	user_sem_delete(semaphore_of(&fSemLock));
}


status_t
BLocker::InitCheck() const
{
	return B_OK;
}


bool
BLocker::Lock()
{
	status_t result;
	return AcquireLock(B_INFINITE_TIMEOUT, &result);
}


status_t
BLocker::LockWithTimeout(bigtime_t timeout)
{
	status_t result;

	AcquireLock(timeout, &result);
	return result;
}


void
BLocker::Unlock()
{
	// Any thread may unlock, but it is reported.
	if (find_thread(NULL) != fLockOwner) {
		fprintf(stderr, "Unlocking BLocker from wrong thread %" B_PRId32
			", current holder %" B_PRId32 "\n", find_thread(NULL),
			fLockOwner);
	}

	fRecursiveCount--;

	if (fRecursiveCount == 0) {
		fLockOwner = B_ERROR;

		int32 oldBenaphoreCount = atomic_add(&fBenaphoreCount, -1);

		// For a semaphore style lock the old count is always above 1.
		if (oldBenaphoreCount > 1)
			user_sem_release(semaphore_of(&fSemLock));
	}
}


thread_id
BLocker::LockingThread() const
{
	return fLockOwner;
}


bool
BLocker::IsLocked() const
{
	return find_thread(NULL) == fLockOwner;
}


int32
BLocker::CountLocks() const
{
	return fRecursiveCount;
}


int32
BLocker::CountLockRequests() const
{
	return fBenaphoreCount;
}


sem_id
BLocker::Sem() const
{
	return B_ERROR;
}


void
BLocker::InitLocker(const char *, bool benaphore)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
	static_assert(sizeof(user_sem) == sizeof(int32) * 4 + sizeof(void*),
		"user_sem overlays the private tail of BLocker");
#define CHECK_OFFSET(field, member) \
	static_assert(__builtin_offsetof(BLocker, field) \
			== __builtin_offsetof(BLocker, fSemLock) \
				+ __builtin_offsetof(user_sem, member), \
		"user_sem " #member " offset")
	CHECK_OFFSET(fSemLock, lock);
	CHECK_OFFSET(fSemCount, count);
	CHECK_OFFSET(fSemInside, inside);
	CHECK_OFFSET(fRecursiveCount, spare);
	CHECK_OFFSET(fSemQueue, queue);
#undef CHECK_OFFSET
	static_assert(sizeof(void*) != 8 || sizeof(BLocker) == 40,
		"BLocker size changed");
#pragma GCC diagnostic pop

	if (benaphore) {
		// The semaphore starts acquired.
		fBenaphoreCount = 0;
		user_sem_init(semaphore_of(&fSemLock), 0);
	} else {
		// One thread passes, the next one blocks.
		fBenaphoreCount = 1;
		user_sem_init(semaphore_of(&fSemLock), 1);
	}

	fLockOwner = B_ERROR;
	fRecursiveCount = 0;
}


bool
BLocker::AcquireLock(bigtime_t timeout, status_t *error)
{
	status_t status = B_OK;
	const thread_id thread = find_thread(NULL);

	if (thread != fLockOwner) {
		int32 oldBenaphoreCount = atomic_add(&fBenaphoreCount, 1);
		if (oldBenaphoreCount > 0) {
			status = user_sem_acquire(semaphore_of(&fSemLock), timeout);

			// The benaphore count is not decremented on failure. Doing so
			// races with Unlock() releasing the semaphore and lets two
			// threads in. The lock then behaves as a semaphore.
		}
	}

	if (status == B_OK) {
		if (fLockOwner < 0) {
			fLockOwner = thread;
			fRecursiveCount = 1;
		} else
			fRecursiveCount++;
	}

	if (error != NULL)
		*error = status;

	return (status == B_OK);
}
