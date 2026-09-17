// Concurrency tests for BLocker
//
// Several threads lock and unlock the same BLocker in a loop, checking that
// only one of them is ever inside the lock, that nesting works, and that
// IsLocked(), LockingThread() and CountLocks() agree while they do it.
//
// Both styles of BLocker are covered. The second test case makes the first
// LockWithTimeout() in each thread time out: after a timeout a benaphore
// style BLocker behaves like a semaphore style one, so this exercises that
// state as well.

#include <Locker.h>
#include <OS.h>

#include <catch2/catch_test_macros.hpp>

#include <ThreadedTest.h>

#include "LockerTestSupport.h"


// How many times each thread locks and unlocks
static const int32 kLoopCount = 10000;

static const bigtime_t kSnoozeTime = 200000;


/*	Locks in one of two ways depending on the attempt, so that both Lock() and
	LockWithTimeout() are used on the way in and on the nested acquisition.
*/
static bool
AcquireLock(BLocker* locker, int32 attempt, bool firstAcquisition)
{
	bool useTimeout = firstAcquisition
		? (attempt % 2) == 1 : ((attempt / 2) % 2) == 1;

	if (useTimeout)
		return locker->LockWithTimeout(1000000) == B_OK;

	return locker->Lock();
}


/*	The body both test cases share: take the lock, check that no other thread
	is inside it, nest a second acquisition, and unwind.

	\a insideLock is only ever touched while the lock is held, so a thread
	seeing it set means the lock let two threads in at once.
*/
static void
LockingLoop(BLocker*& locker, bool& insideLock)
{
	UnlockOnExit unlockOnExit(locker);

	for (int32 i = 0; i < kLoopCount; i++) {
		CheckLock(locker, 0);
		THREAD_REQUIRE(AcquireLock(locker, i, true));

		THREAD_REQUIRE(!insideLock);
		insideLock = true;
		CheckLock(locker, 1);

		THREAD_REQUIRE(AcquireLock(locker, i, false));
		CheckLock(locker, 2);
		locker->Unlock();
		CheckLock(locker, 1);

		THREAD_REQUIRE(insideLock);
		insideLock = false;
		locker->Unlock();
		CheckLock(locker, 0);
	}
}


static void
RunConcurrencyTest(bool benaphore)
{
	BLocker theLocker(benaphore);
	BLocker* locker = &theLocker;
	bool insideLock = false;

	ThreadedTest test;
	for (int i = 0; i < 3; i++) {
		test.AddThread(std::string(1, 'A' + i), [&locker, &insideLock]() {
			LockingLoop(locker, insideLock);
		});
	}

	test.Run();
}


static void
RunTimeoutConcurrencyTest(bool benaphore)
{
	BLocker theLocker(benaphore);
	BLocker* locker = &theLocker;
	bool insideLock = false;

	ThreadedTest test;

	// Holds the lock long enough for the other two threads to time out on it
	test.AddThread("Acquire", [&locker, &insideLock]() {
		UnlockOnExit unlockOnExit(locker);

		THREAD_REQUIRE(locker->Lock());
		snooze(kSnoozeTime);
		locker->Unlock();

		LockingLoop(locker, insideLock);
	});

	for (int i = 0; i < 2; i++) {
		test.AddThread("Timeout" + std::to_string(i + 1),
			[&locker, &insideLock]() {
				UnlockOnExit unlockOnExit(locker);

				snooze(kSnoozeTime / 2);
				THREAD_REQUIRE(locker->LockWithTimeout(kSnoozeTime / 10)
					== B_TIMED_OUT);

				LockingLoop(locker, insideLock);
			});
	}

	test.Run();
}


TEST_CASE("BLocker: concurrent locking", "[BLocker][support]")
{
	SECTION("benaphore")
	{
		RunConcurrencyTest(true);
	}

	SECTION("semaphore")
	{
		RunConcurrencyTest(false);
	}
}


TEST_CASE("BLocker: concurrent locking after a timeout", "[BLocker][support]")
{
	SECTION("benaphore")
	{
		RunTimeoutConcurrencyTest(true);
	}

	SECTION("semaphore")
	{
		RunTimeoutConcurrencyTest(false);
	}
}
