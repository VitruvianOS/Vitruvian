// Unit tests for the thread functions in <OS.h>

#include <OS.h>

#include <atomic>

#include <catch2/catch_test_macros.hpp>


struct ThreadState {
	std::atomic<bool>	finished;
	sem_id				done;
	bigtime_t			work;
};


static int32
SnoozingThread(void* data)
{
	ThreadState* state = static_cast<ThreadState*>(data);
	snooze(state->work);
	state->finished = true;
	release_sem(state->done);

	return 123;
}


/*	This is tagged [!shouldfail] because wait_for_thread() is broken: it
	returns B_OK within a few microseconds instead of waiting, and reports an
	exit code of 0 rather than the value the thread returned. The
	implementation is in src/system/libroot2/thread.cpp, which asks the nexus
	module for NEXUS_THREAD_WAITFOR.

	Callers that join a thread before freeing what it uses are racing, so this
	records the bug rather than leaving it unnoticed. When it is fixed this
	test starts passing, which Catch2 reports as a failure: drop the
	[!shouldfail] tag and the KNOWN BUG prefix then.

	The prefix is in the name because ctest only reports whether the
	executable failed, and [!shouldfail] makes it exit successfully. Without
	the name saying so, a known bug would show up in ctest as a plain pass.
*/
TEST_CASE("KNOWN BUG: wait_for_thread does not wait for the thread to finish",
	"[thread][libroot][known-bug][!shouldfail]")
{
	ThreadState state;
	state.finished = false;
	state.work = 200000;
	state.done = create_sem(0, "thread test");
	REQUIRE(state.done >= B_OK);

	thread_id thread = spawn_thread(&SnoozingThread, "snoozing",
		B_NORMAL_PRIORITY, &state);
	REQUIRE(thread >= B_OK);
	REQUIRE(resume_thread(thread) == B_OK);

	bigtime_t start = system_time();
	status_t exitValue = -1;
	status_t result = wait_for_thread(thread, &exitValue);

	CHECK(result == B_OK);
	CHECK(system_time() - start >= state.work);
	CHECK(state.finished.load());
	CHECK(exitValue == 123);

	// Whatever wait_for_thread() did, the thread must be finished with
	// ThreadState before this returns.
	acquire_sem(state.done);
	delete_sem(state.done);
}
