/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host test for the unconfirmed display change state machine: runs
// anywhere with a C++ compiler, no window server required.

#include <stdio.h>
#include <stdlib.h>

#include "DisplayChangeTracker.h"


static int sFailures = 0;


#define CHECK(expression) \
	do { \
		if (!(expression)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
			sFailures++; \
		} \
	} while (0)


static void
TestTimeoutReverts()
{
	DisplayChangeTracker tracker;
	uint32_t generation = tracker.Begin(42);

	CHECK(tracker.IsPending());
	CHECK(tracker.Owner() == 42);
	CHECK(tracker.Timeout(generation));
	CHECK(!tracker.IsPending());
	// A second timeout must not revert twice.
	CHECK(!tracker.Timeout(generation));
}


static void
TestConfirmKeeps()
{
	DisplayChangeTracker tracker;
	uint32_t generation = tracker.Begin(42);

	CHECK(tracker.Confirm(42));
	CHECK(!tracker.IsPending());
	// The deadline fired too late: the change was kept already.
	CHECK(!tracker.Timeout(generation));
	CHECK(!tracker.Confirm(42));
}


static void
TestCancelReverts()
{
	DisplayChangeTracker tracker;
	uint32_t generation = tracker.Begin(42);

	CHECK(tracker.Cancel(42));
	CHECK(!tracker.IsPending());
	// Escape resolved it; the deadline must not revert again.
	CHECK(!tracker.Timeout(generation));
	CHECK(!tracker.Cancel(42));
}


static void
TestOwnerDeathReverts()
{
	DisplayChangeTracker tracker;
	uint32_t generation = tracker.Begin(42);

	CHECK(tracker.OwnerDied(42));
	CHECK(!tracker.IsPending());
	CHECK(!tracker.Timeout(generation));
	CHECK(!tracker.Confirm(42));
}


static void
TestWrongOwnerIsRejected()
{
	DisplayChangeTracker tracker;
	uint32_t generation = tracker.Begin(42);

	CHECK(!tracker.Confirm(7));
	CHECK(!tracker.Cancel(7));
	CHECK(!tracker.OwnerDied(7));
	CHECK(tracker.IsPending());
	CHECK(tracker.Timeout(generation));
}


static void
TestRestartMakesStaleTimeoutHarmless()
{
	DisplayChangeTracker tracker;
	uint32_t first = tracker.Begin(42);
	uint32_t second = tracker.Begin(42);

	CHECK(tracker.IsPending());
	CHECK(second != first);
	// The old timer fired late; the window was restarted since.
	CHECK(!tracker.Timeout(first));
	CHECK(tracker.IsPending());
	CHECK(tracker.Timeout(second));
}


static void
TestSecondBeginJoins()
{
	DisplayChangeTracker tracker;
	tracker.Begin(42);
	tracker.Begin(42);

	// One window, one owner: the first snapshot stands for all of it.
	CHECK(tracker.IsPending());
	CHECK(tracker.Owner() == 42);
	CHECK(tracker.Confirm(42));
	CHECK(!tracker.IsPending());
}


static void
TestBeginAfterResolveStartsFresh()
{
	DisplayChangeTracker tracker;
	uint32_t first = tracker.Begin(42);
	CHECK(tracker.Cancel(42));

	uint32_t second = tracker.Begin(7);
	CHECK(second != first);
	CHECK(tracker.Owner() == 7);
	CHECK(tracker.Timeout(second));
}


int
main(int /*argc*/, char** /*argv*/)
{
	TestTimeoutReverts();
	TestConfirmKeeps();
	TestCancelReverts();
	TestOwnerDeathReverts();
	TestWrongOwnerIsRejected();
	TestRestartMakesStaleTimeoutHarmless();
	TestSecondBeginJoins();
	TestBeginAfterResolveStartsFresh();

	if (sFailures != 0) {
		printf("test_display_change_tracker: %d FAIL\n", sFailures);
		return EXIT_FAILURE;
	}

	printf("test_display_change_tracker: PASS\n");
	return EXIT_SUCCESS;
}
