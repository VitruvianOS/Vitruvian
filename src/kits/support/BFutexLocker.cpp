/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <OS.h>
#include <SupportDefs.h>

#include <stdio.h>

#include <BFutexLocker.h>


BFutexLocker::BFutexLocker()
{
	fBenaphoreCount = 0;
	fOwner = B_ERROR;
	user_sem_init(&fSem, 0);
}


BFutexLocker::BFutexLocker(const char* /*name*/)
{
	fBenaphoreCount = 0;
	fOwner = B_ERROR;
	user_sem_init(&fSem, 0);
}


BFutexLocker::~BFutexLocker()
{
	user_sem_delete(&fSem);
}


status_t
BFutexLocker::InitCheck() const
{
	return B_OK;
}


bool
BFutexLocker::Lock()
{
	status_t result;
	return AcquireLock(B_INFINITE_TIMEOUT, &result);
}


status_t
BFutexLocker::LockWithTimeout(bigtime_t timeout)
{
	status_t result;

	AcquireLock(timeout, &result);
	return result;
}


void
BFutexLocker::Unlock()
{
	fOwner = B_ERROR;

	if (atomic_add(&fBenaphoreCount, -1) > 1)
		user_sem_release(&fSem);
}


thread_id
BFutexLocker::LockingThread() const
{
	return fOwner;
}


bool
BFutexLocker::IsLocked() const
{
	return find_thread(NULL) == fOwner;
}


bool
BFutexLocker::AcquireLock(bigtime_t timeout, status_t* error)
{
	status_t status = B_OK;

	// Not recursive, the owner locking twice deadlocks.
	if (atomic_add(&fBenaphoreCount, 1) > 0)
		status = user_sem_acquire(&fSem, timeout);

	if (status == B_OK)
		fOwner = find_thread(NULL);

	if (error != NULL)
		*error = status;
	return status == B_OK;
}
