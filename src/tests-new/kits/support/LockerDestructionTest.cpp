// Destruction tests for BLocker
//
// A thread waiting for a lock has to be woken up when the lock is deleted, and
// told that it did not get it. The two test cases cover a thread waiting in
// Lock() and one waiting in LockWithTimeout().
//
// The locker is deleted by one thread while the other is blocked on it, which
// is the behaviour being tested.

#include <Locker.h>
#include <OS.h>

#include <catch2/catch_test_macros.hpp>

#include <ThreadedTest.h>


static const bigtime_t kSnoozeTime = 200000;


static void
RunDestructionTest(bool benaphore)
{
	BLocker* locker = new BLocker(benaphore);
	ThreadedTest test;

	// Holds the lock, lets the other thread take it, then blocks on it until
	// it is deleted underneath
	test.AddThread("waiter", [&locker]() {
		THREAD_REQUIRE(locker->Lock());
		snooze(kSnoozeTime);
		locker->Unlock();
		snooze(kSnoozeTime);

		THREAD_REQUIRE(!locker->Lock());
	});

	test.AddThread("deleter", [&locker]() {
		snooze(kSnoozeTime);
		THREAD_REQUIRE(locker->Lock());
		snooze(kSnoozeTime * 2);

		BLocker* doomed = locker;
		locker = NULL;
		delete doomed;
	});

	test.Run();
}


static void
RunTimeoutDestructionTest(bool benaphore)
{
	BLocker* locker = new BLocker(benaphore);
	ThreadedTest test;

	// Same as above, but both threads use LockWithTimeout(). The last wait is
	// given a long timeout so that the delete, not the timeout, is what ends
	// it.
	test.AddThread("waiter", [&locker]() {
		THREAD_REQUIRE(locker->LockWithTimeout(kSnoozeTime) == B_OK);
		snooze(kSnoozeTime);
		locker->Unlock();
		snooze(kSnoozeTime);

		THREAD_REQUIRE(locker->LockWithTimeout(kSnoozeTime * 10)
			== B_BAD_SEM_ID);
	});

	test.AddThread("deleter", [&locker]() {
		snooze(kSnoozeTime / 10);
		THREAD_REQUIRE(locker->LockWithTimeout(kSnoozeTime / 10)
			== B_TIMED_OUT);
		THREAD_REQUIRE(locker->LockWithTimeout(kSnoozeTime * 10) == B_OK);
		snooze(kSnoozeTime * 2);

		BLocker* doomed = locker;
		locker = NULL;
		delete doomed;
	});

	test.Run();
}


TEST_CASE("BLocker: deleting a lock a thread waits for", "[BLocker][support]")
{
	SECTION("benaphore")
	{
		RunDestructionTest(true);
	}

	SECTION("semaphore")
	{
		RunDestructionTest(false);
	}
}


TEST_CASE("BLocker: deleting a lock a thread waits for with a timeout",
	"[BLocker][support]")
{
	SECTION("benaphore")
	{
		RunTimeoutDestructionTest(true);
	}

	SECTION("semaphore")
	{
		RunTimeoutDestructionTest(false);
	}
}
