// Helpers shared by the BLocker tests.

#ifndef _LOCKER_TEST_SUPPORT_H
#define _LOCKER_TEST_SUPPORT_H

#include <Locker.h>
#include <OS.h>

#include <ThreadedTest.h>


/*	Releases the lock when it goes out of scope, however the thread leaves it.
	Without this a thread that gives up on a failed THREAD_REQUIRE would hold
	the lock forever and the other threads would wait for it.

	The old cppunit tests called this SafetyLock. It unlocks as many times as
	this thread holds the lock, rather than once.
*/
class UnlockOnExit {
public:
	UnlockOnExit(BLocker*& locker)
		:
		fLocker(locker)
	{
	}

	~UnlockOnExit()
	{
		while (fLocker != NULL && fLocker->IsLocked())
			fLocker->Unlock();
	}

private:
	BLocker*&	fLocker;
};


/*	Checks that the lock is held by this thread exactly \a expectedCount times,
	or, for an expected count of zero, that this thread does not hold it.
*/
static inline void
CheckLock(BLocker* locker, int expectedCount)
{
	bool isLocked = locker->IsLocked();
	thread_id lockingThread = locker->LockingThread();
	thread_id self = find_thread(NULL);
	int32 count = locker->CountLocks();

	if (expectedCount > 0) {
		THREAD_REQUIRE(isLocked);
		THREAD_REQUIRE(lockingThread == self);
		THREAD_REQUIRE(count == expectedCount);
	} else
		THREAD_REQUIRE(!(isLocked && lockingThread == self));
}


#endif	// _LOCKER_TEST_SUPPORT_H
