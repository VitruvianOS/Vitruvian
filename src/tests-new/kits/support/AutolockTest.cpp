// Unit tests for BAutolock
//
// BAutolock takes a lock for as long as it is in scope. The BLocker case is
// checked from three threads: one holds the lock and then deletes it, while
// the other two wait for it inside a BAutolock and must be told they did not
// get it.

#include <Autolock.h>
#include <Locker.h>
#include <Looper.h>
#include <OS.h>

#include <catch2/catch_test_macros.hpp>

#include <ThreadedTest.h>


static const bigtime_t kSnoozeTime = 250000;


TEST_CASE("BAutolock: with a BLocker", "[BAutolock][support]")
{
	BLocker* locker = new BLocker();
	ThreadedTest test;

	test.AddThread("owner", [&locker]() {
		THREAD_REQUIRE(locker->Lock());
		THREAD_CHECK(locker->LockingThread() == find_thread(NULL));

		// The other two threads are waiting for this lock inside a BAutolock
		snooze(kSnoozeTime);
		delete locker;
		locker = new BLocker();

		{
			BAutolock autolock(locker);
			THREAD_CHECK(locker->IsLocked());
			THREAD_CHECK(locker->LockingThread() == find_thread(NULL));
			THREAD_CHECK(autolock.IsLocked());
		}
		THREAD_CHECK(locker->LockingThread() != find_thread(NULL));

		{
			BAutolock autolock(*locker);
			THREAD_CHECK(locker->IsLocked());
			THREAD_CHECK(locker->LockingThread() == find_thread(NULL));
			THREAD_CHECK(autolock.IsLocked());
		}
		THREAD_CHECK(locker->LockingThread() != find_thread(NULL));
	});

	// Both of these take the lock the first thread is about to delete, one
	// through each constructor
	test.AddThread("waiter by pointer", [&locker]() {
		snooze(kSnoozeTime / 10);
		BLocker* doomed = locker;

		BAutolock autolock(doomed);
		THREAD_CHECK(!autolock.IsLocked());
	});

	test.AddThread("waiter by reference", [&locker]() {
		snooze(kSnoozeTime / 10);
		BLocker* doomed = locker;

		BAutolock autolock(*doomed);
		THREAD_CHECK(!autolock.IsLocked());
	});

	test.Run();

	delete locker;
}


TEST_CASE("BAutolock: with a BLooper", "[BAutolock][support]")
{
	BLooper* looper = new BLooper();
	looper->Run();

	{
		BAutolock autolock(looper);
		CHECK(looper->IsLocked());
		CHECK(looper->LockingThread() == find_thread(NULL));
		CHECK(autolock.IsLocked());
	}

	CHECK(looper->LockingThread() != find_thread(NULL));

	looper->Lock();
	looper->Quit();
}
