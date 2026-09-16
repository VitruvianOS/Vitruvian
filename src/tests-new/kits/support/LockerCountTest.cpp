// Tests for BLocker::CountLockRequests()
//
// Three threads take part. The first holds two gate locks that keep the other
// two out, takes the lock under test, and counts the requests as it lets each
// of the other threads through. Those threads first time out on the lock, then
// block on it, which is what the counts below reflect.
//
// A benaphore style BLocker counts one request fewer than a semaphore style
// one throughout, so the two cases are spelled out separately rather than
// parameterised.

#include <Locker.h>
#include <OS.h>

#include <catch2/catch_test_macros.hpp>

#include <ThreadedTest.h>

#include "LockerTestSupport.h"


static const bigtime_t kSnoozeTime = 100000;


/*	Waits at \a gate until the first thread opens it, times out on \a locker,
	then blocks on it until the first thread lets go.

	\a expected and \a expectedAlternative are both allowed because the count
	depends on whether the other waiting thread has arrived yet.
*/
static void
CountingThread(BLocker*& locker, BLocker*& gate, int32 expected,
	int32 expectedAlternative)
{
	UnlockOnExit unlockOnExit(locker);

	snooze(kSnoozeTime / 10);
	THREAD_REQUIRE(gate->Lock());
	THREAD_REQUIRE(locker->LockWithTimeout(kSnoozeTime / 10) == B_TIMED_OUT);
	THREAD_REQUIRE(locker->Lock());

	int32 actual = locker->CountLockRequests();
	THREAD_CHECK(actual == expected || actual == expectedAlternative);

	locker->Unlock();
}


TEST_CASE("BLocker: counting lock requests, benaphore", "[BLocker][support]")
{
	BLocker theLocker(true);
	BLocker theGate2("gate 2");
	BLocker theGate3("gate 3");
	BLocker* locker = &theLocker;
	BLocker* gate2 = &theGate2;
	BLocker* gate3 = &theGate3;

	ThreadedTest test;

	test.AddThread("counter", [&]() {
		UnlockOnExit unlockLocker(locker);
		UnlockOnExit unlockGate2(gate2);
		UnlockOnExit unlockGate3(gate3);

		THREAD_REQUIRE(gate2->Lock());
		THREAD_REQUIRE(gate3->Lock());
		THREAD_CHECK(locker->CountLockRequests() == 0);

		THREAD_REQUIRE(locker->Lock());
		THREAD_CHECK(locker->CountLockRequests() == 1);

		// Let the second thread through
		gate2->Unlock();
		snooze(kSnoozeTime);
		THREAD_CHECK(locker->CountLockRequests() == 3);

		// And the third
		gate3->Unlock();
		snooze(kSnoozeTime);
		THREAD_CHECK(locker->CountLockRequests() == 5);

		locker->Unlock();
		snooze(kSnoozeTime);
		THREAD_CHECK(locker->CountLockRequests() == 2);
	});

	test.AddThread("second", [&]() { CountingThread(locker, gate2, 3, 4); });
	test.AddThread("third", [&]() { CountingThread(locker, gate3, 3, 4); });

	test.Run();
}


TEST_CASE("BLocker: counting lock requests, semaphore", "[BLocker][support]")
{
	BLocker theLocker(false);
	BLocker theGate2("gate 2");
	BLocker theGate3("gate 3");
	BLocker* locker = &theLocker;
	BLocker* gate2 = &theGate2;
	BLocker* gate3 = &theGate3;

	ThreadedTest test;

	test.AddThread("counter", [&]() {
		UnlockOnExit unlockLocker(locker);
		UnlockOnExit unlockGate2(gate2);
		UnlockOnExit unlockGate3(gate3);

		THREAD_REQUIRE(gate2->Lock());
		THREAD_REQUIRE(gate3->Lock());
		THREAD_CHECK(locker->CountLockRequests() == 1);

		THREAD_REQUIRE(locker->Lock());
		THREAD_CHECK(locker->CountLockRequests() == 2);

		gate2->Unlock();
		snooze(kSnoozeTime);
		THREAD_CHECK(locker->CountLockRequests() == 4);

		gate3->Unlock();
		snooze(kSnoozeTime);
		THREAD_CHECK(locker->CountLockRequests() == 6);

		locker->Unlock();
		snooze(kSnoozeTime);
		THREAD_CHECK(locker->CountLockRequests() == 3);
	});

	test.AddThread("second", [&]() { CountingThread(locker, gate2, 4, 5); });
	test.AddThread("third", [&]() { CountingThread(locker, gate3, 4, 5); });

	test.Run();
}
