// Unit tests for the ThreadedTest helper itself
//
// These use Collect() rather than Run(), so that a failure a test deliberately
// provokes is inspected instead of being reported.

#include <OS.h>

#include <atomic>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <ThreadedTest.h>


TEST_CASE("ThreadedTest: every thread runs", "[ThreadedTest][utils]")
{
	std::atomic<int> started(0);
	ThreadedTest test;

	for (int i = 0; i < 4; i++)
		test.AddThread("worker", [&started]() { started++; });

	CHECK(test.Collect().empty());
	CHECK(started.load() == 4);
}


TEST_CASE("ThreadedTest: slow threads are waited for", "[ThreadedTest][utils]")
{
	// wait_for_thread() currently returns immediately instead of waiting, so
	// this guards against the helper going back to it.
	const bigtime_t kWork = 200000;
	std::atomic<bool> finished(false);
	ThreadedTest test;

	test.AddThread("slow", [&finished, kWork]() {
		snooze(kWork);
		finished = true;
	});

	bigtime_t start = system_time();
	CHECK(test.Collect().empty());
	CHECK(finished.load());
	CHECK(system_time() - start >= kWork);
}


TEST_CASE("ThreadedTest: THREAD_CHECK records and continues",
	"[ThreadedTest][utils]")
{
	bool reachedEnd = false;
	ThreadedTest test;

	test.AddThread("A", [&reachedEnd]() {
		THREAD_CHECK(1 == 2);
		reachedEnd = true;
	});

	std::vector<ThreadedTestFailure> failures = test.Collect();
	REQUIRE(failures.size() == 1);
	CHECK(failures[0].message == "1 == 2");
	CHECK(failures[0].thread == "A");
	CHECK(failures[0].line > 0);
	CHECK(reachedEnd);
}


TEST_CASE("ThreadedTest: THREAD_REQUIRE ends its thread",
	"[ThreadedTest][utils]")
{
	bool reachedEnd = false;
	bool otherThreadRan = false;
	ThreadedTest test;

	test.AddThread("A", [&reachedEnd]() {
		THREAD_REQUIRE(false);
		reachedEnd = true;
	});
	test.AddThread("B", [&otherThreadRan]() { otherThreadRan = true; });

	std::vector<ThreadedTestFailure> failures = test.Collect();
	REQUIRE(failures.size() == 1);
	CHECK(failures[0].message == "false");
	CHECK(failures[0].thread == "A");
	CHECK_FALSE(reachedEnd);

	// One thread giving up does not disturb the others
	CHECK(otherThreadRan);
}


TEST_CASE("ThreadedTest: THREAD_FAIL records its message",
	"[ThreadedTest][utils]")
{
	ThreadedTest test;

	test.AddThread("A", []() { THREAD_FAIL("something went wrong"); });

	std::vector<ThreadedTestFailure> failures = test.Collect();
	REQUIRE(failures.size() == 1);
	CHECK(failures[0].message == "something went wrong");
}


TEST_CASE("ThreadedTest: an escaping exception is recorded",
	"[ThreadedTest][utils]")
{
	ThreadedTest test;

	test.AddThread("A", []() { throw std::runtime_error("boom"); });
	test.AddThread("B", []() { throw 42; });

	std::vector<ThreadedTestFailure> failures = test.Collect();
	REQUIRE(failures.size() == 2);
	for (size_t i = 0; i < failures.size(); i++)
		CHECK(failures[i].message.find("unexpected exception") == 0);
}


TEST_CASE("ThreadedTest: failures from several threads are kept apart",
	"[ThreadedTest][utils]")
{
	ThreadedTest test;

	test.AddThread("A", []() { THREAD_FAIL("from A"); });
	test.AddThread("B", []() { THREAD_FAIL("from B"); });

	std::vector<ThreadedTestFailure> failures = test.Collect();
	REQUIRE(failures.size() == 2);

	std::string first = failures[0].thread + ":" + failures[0].message;
	std::string second = failures[1].thread + ":" + failures[1].message;
	CHECK(first != second);
	CHECK((first == "A:from A" || first == "B:from B"));
	CHECK((second == "A:from A" || second == "B:from B"));
}


TEST_CASE("ThreadedTest: the threads run at the same time",
	"[ThreadedTest][utils]")
{
	// Each thread waits for the other to arrive, which only works if they run
	// concurrently.
	std::atomic<bool> arrivedA(false);
	std::atomic<bool> arrivedB(false);
	const bigtime_t kTimeout = 5000000;

	ThreadedTest test;

	test.AddThread("A", [&]() {
		arrivedA = true;
		bigtime_t end = system_time() + kTimeout;
		while (!arrivedB.load() && system_time() < end)
			snooze(1000);
		THREAD_CHECK(arrivedB.load());
	});

	test.AddThread("B", [&]() {
		arrivedB = true;
		bigtime_t end = system_time() + kTimeout;
		while (!arrivedA.load() && system_time() < end)
			snooze(1000);
		THREAD_CHECK(arrivedA.load());
	});

	CHECK(test.Collect().empty());
}
