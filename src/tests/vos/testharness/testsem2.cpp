// Standard Includes -----------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// System Includes -------------------------------------------------------------
#include <OS.h>

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------

// Local Defines ---------------------------------------------------------------
#define dprintf printf

// Globals ---------------------------------------------------------------------

static void sem_test();
static void sem_thread_test();
static void sem_timeout_test();
static void sem_multicount_test();
static void sem_negative_count_test();
static void sem_delete_with_waiters_test();
static void sem_iteration_test();
static void sem_error_handling_test();
static void sem_accumulation_test();
static int32 sem_test_thread_one(void *arg);
static int32 sem_test_thread_two(void *arg);
static int32 sem_test_thread_three(void *arg);
static int32 sem_test_thread_four(void *arg);
static int32 sem_waiter_thread(void *arg);
static int32 sem_delete_waiter_thread(void *arg);

int main()
{
	dprintf("testsem: start test\n");
	sem_test();
	dprintf("testsem: repeating\n");
	sem_test();
	dprintf("testsem: thread test\n");
	sem_thread_test();
	//dprintf("testsem: timeout modes test\n");
	sem_timeout_test();
	dprintf("testsem: multi-count test\n");
	sem_multicount_test();
	dprintf("testsem: negative count test\n");
	sem_negative_count_test();
	dprintf("testsem: delete with waiters test\n");
	sem_delete_with_waiters_test();
	dprintf("testsem: iteration test\n");
	sem_iteration_test();
	dprintf("testsem: error handling test\n");
	sem_error_handling_test();
	dprintf("testsem: accumulation test\n");
	sem_accumulation_test();
	return 0;
}


sem_id test_s1, test_s2, test_s3, test_s4;
int thread_count = 0;

void sem_test()
{
	int32 count;
	sem_info info;
	status_t status;

	/* Create semaphores */

	dprintf("testsem: begin test\n");
	test_s1 = create_sem(0,  "sem #1");
	test_s2 = create_sem(1,  "sem #2");
	test_s3 = create_sem(13, "sem #3");

	dprintf("testsem (%s): 'sem #1' has id %d\n",
			(test_s1 >= 0) ? "pass" : "FAIL", test_s1);
	dprintf("testsem (%s): 'sem #2' has id %d\n",
			(test_s2 >= 0) ? "pass" : "FAIL", test_s2);
	dprintf("testsem (%s): 'sem #3' has id %d\n",
			(test_s3 >= 0) ? "pass" : "FAIL", test_s3);

	/* Manipulate semaphores */

	/* test get_sem_count on freshly created sem*/
	dprintf("testsem (pass): *** test get_sem_count() on freshly created sem with count of 1\n");
	status = get_sem_count(test_s2, &count);
	dprintf("testsem (%s): get_sem_count(test_s2) returned %d with count %d\n",
			((status == 0) && (count == 1)) ? "pass" : "FAIL", status, count);

	/* test acquire_sem */
	dprintf("testsem (pass): *** test acquire_sem\n");
	status = acquire_sem(test_s2);
	dprintf("testsem (%s): acquire_sem(test_s2) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* test get_sem_count after acquire*/
	dprintf("testsem (pass): *** test get_sem_count() after the acquire\n");
	status = get_sem_count(test_s2, &count);
	dprintf("testsem (%s): get_sem_count(test_s2) returned %d with count %d\n",
			((status == 0) && (count == 0)) ? "pass" : "FAIL", status, count);

	/* test acquire_sem with timeout on sem inited to >0 */
	dprintf("testsem (pass): *** test acquire_sem with timeout on sem inited to count of 1\n");
	status = acquire_sem_etc(test_s2, 1, B_TIMEOUT, 1000000);
	dprintf("testsem (%s): acquire_sem_etc(test_s2, 1) with 1s timeout returned %d\n",
			(status == B_TIMED_OUT) ? "pass" : "FAIL", status);

	/* test release_sem */
	dprintf("testsem (pass): *** test release_sem\n");
	status = release_sem(test_s2);
	dprintf("testsem (%s): release_sem(test_s2) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* test acquire_sem with timeout on sem inited to 0 */
	dprintf("testsem (pass): *** test acquire_sem with timeout on sem inited to count of 0\n");
	status = acquire_sem_etc(test_s1, 1, B_TIMEOUT, 1000000);
	dprintf("testsem (%s): acquire_sem_etc(test_s1, 1) with 1s timeout returned %d\n",
			(status == B_TIMED_OUT) ? "pass" : "FAIL", status);

	/* test acquire_sem with zero timeout */
	dprintf("testsem (pass): *** test acquire_sem with timeout of 0\n");
	status = acquire_sem_etc(test_s1, 1, B_TIMEOUT, 0);
	dprintf("testsem (%s): acquire_sem_etc(test_s1, 1) with 0s timeout returned %d\n",
			(status == B_WOULD_BLOCK) ? "pass" : "FAIL", status);

	/* test get_sem_count */
	dprintf("testsem (pass): *** test get_sem_count\n");
	status = get_sem_count(test_s3, &count);
	dprintf("testsem (%s): get_sem_count(test_s3) returned %d with count %d\n",
			((status == 0) && (count == 13)) ? "pass" : "FAIL", status, count);

	/* test acquire_sem_etc with no timeout */
	dprintf("testsem (pass): *** test acquire_sem with no timeout\n");
	status = acquire_sem_etc(test_s3, 13, 0, 0);
	dprintf("testsem (%s): acquire_sem(test_s3, 13) with no timeout returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* test get_sem_info and results of previous acquire_sem_etc */
	dprintf("testsem (pass): *** test get_sem_info\n");
	status = get_sem_info(test_s3, &info);
	dprintf("testsem (%s): get_sem_info(test_s3) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);
	dprintf("testsem (%s): sem count is now %d\n",
			(info.count == 0) ? "pass" : "FAIL", info.count);

	/* test set_sem_owner */
	dprintf("testsem (pass): *** test set_sem_owner\n");
	status = set_sem_owner(test_s3, 10);
	dprintf("testsem (%s): set_sem_owner(test_s3) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* Delete semaphores */

	status = delete_sem(test_s1);
	dprintf("testsem (%s): delete_sem(test_s1) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);

	status = delete_sem(test_s2);
	dprintf("testsem (%s): delete_sem(test_s2) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);

	status = delete_sem(test_s3);
	dprintf("testsem (%s): delete_sem(test_s3) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* test delete of already deleted sem */
	status = delete_sem(test_s3);
	dprintf("testsem (%s): delete_sem(test_s3 again) returned %d\n",
			(status == B_BAD_SEM_ID) ? "pass" : "FAIL", status);

	dprintf("testsem: end test\n");
}


void sem_thread_test()
{
	thread_id t1, t2, t3, t4;

	test_s4 = create_sem(0, "sem #3");

	dprintf("testsem (%s): 'sem #4' has id %d\n",
			(test_s4 >= 0) ? "pass" : "FAIL", test_s4);

	dprintf("testsem (pass): spawning thread 1\n");
	t1 = spawn_thread(sem_test_thread_one, "sem_test_1", B_NORMAL_PRIORITY, NULL);
	resume_thread(t1);

	dprintf("testsem (pass): spawning thread 2\n");
	t2 = spawn_thread(sem_test_thread_two, "sem_test_2", B_NORMAL_PRIORITY, NULL);
	resume_thread(t2);
	
	dprintf("testsem (pass): spawning thread 3\n");
	t3 = spawn_thread(sem_test_thread_three, "sem_test_3", B_NORMAL_PRIORITY, NULL);
	resume_thread(t3);
	
	dprintf("testsem (pass): spawning thread 4\n");
	t4 = spawn_thread(sem_test_thread_four, "sem_test_4", B_NORMAL_PRIORITY, NULL);
	resume_thread(t4);
	
	release_sem_etc(test_s4, 4, 0);
	
	/* Using wait_for_thread might seem ideal here, but if the test fails */
	/* it would never return thus hanging the app, so I use a poorman's   */
	/* sychronization method instead.                                     */
	usleep(1000000);
	
	dprintf("testsem (%s): %d thread(s) returned\n",
			(thread_count == 4) ? "pass" : "FAIL", thread_count);
}

static int32 sem_test_thread_one(void *arg)
{
	acquire_sem(test_s4);
	thread_count++;
	return 0;
}

static int32 sem_test_thread_two(void *arg)
{
	acquire_sem(test_s4);
	thread_count++;
	return 0;
}
static int32 sem_test_thread_three(void *arg)
{
	acquire_sem(test_s4);
	thread_count++;
	return 0;
}
static int32 sem_test_thread_four(void *arg)
{
	acquire_sem(test_s4);
	thread_count++;
	return 0;
}

/* Test B_RELATIVE_TIMEOUT vs B_ABSOLUTE_TIMEOUT */
void sem_timeout_test()
{
	sem_id sem;
	status_t status;
	bigtime_t start, elapsed;
	
	sem = create_sem(0, "timeout_test");
	dprintf("testsem (%s): created timeout test sem %d\n",
			(sem >= 0) ? "pass" : "FAIL", sem);
	
	/* Test B_RELATIVE_TIMEOUT */
	dprintf("testsem (pass): *** test B_RELATIVE_TIMEOUT\n");
	start = system_time();
	status = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, 500000); // 0.5 seconds
	elapsed = system_time() - start;
	dprintf("testsem (pass): B_RELATIVE_TIMEOUT (500ms) returned %d after %ld us\n",
			status, (long)elapsed);
	
	/* Test B_ABSOLUTE_TIMEOUT */
	dprintf("testsem (pass): *** test B_ABSOLUTE_TIMEOUT\n");
	start = system_time();
	bigtime_t deadline = start + 500000; // 0.5 seconds from now
	status = acquire_sem_etc(sem, 1, B_ABSOLUTE_TIMEOUT, deadline);
	elapsed = system_time() - start;
	dprintf("testsem (pass): B_ABSOLUTE_TIMEOUT (500ms) returned %d after %ld us\n",
			status, (long)elapsed);
	
	/* Test B_ABSOLUTE_TIMEOUT with past time (should return immediately) */
	dprintf("testsem (pass): *** test B_ABSOLUTE_TIMEOUT with past time\n");
	start = system_time();
	deadline = start - 1000000; // 1 second in the past
	status = acquire_sem_etc(sem, 1, B_ABSOLUTE_TIMEOUT, deadline);
	elapsed = system_time() - start;
	dprintf("testsem (pass): B_ABSOLUTE_TIMEOUT (past) returned %d after %ld us\n",
			status, (long)elapsed);
	
	delete_sem(sem);
}

/* Test acquire_sem_etc and release_sem_etc with count > 1 */
void sem_multicount_test()
{
	sem_id sem;
	status_t status;
	int32 count;
	
	sem = create_sem(10, "multicount_test");
	dprintf("testsem (%s): created multicount test sem %d with count 10\n",
			(sem >= 0) ? "pass" : "FAIL", sem);
	
	/* Test partial acquisition */
	dprintf("testsem (pass): *** test acquire_sem_etc with count=3\n");
	status = acquire_sem_etc(sem, 3, 0, 0);
	dprintf("testsem (%s): acquire_sem_etc(sem, 3) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);
	
	get_sem_count(sem, &count);
	dprintf("testsem (%s): sem count after acquiring 3 is %d\n",
			(count == 7) ? "pass" : "FAIL", count);
	
	/* Test another partial acquisition */
	dprintf("testsem (pass): *** test acquire_sem_etc with count=5\n");
	status = acquire_sem_etc(sem, 5, 0, 0);
	dprintf("testsem (%s): acquire_sem_etc(sem, 5) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);
	
	get_sem_count(sem, &count);
	dprintf("testsem (%s): sem count after acquiring 5 more is %d\n",
			(count == 2) ? "pass" : "FAIL", count);
	
	/* Test release_sem_etc with count > 1 */
	dprintf("testsem (pass): *** test release_sem_etc with count=4\n");
	status = release_sem_etc(sem, 4, 0);
	dprintf("testsem (%s): release_sem_etc(sem, 4) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);
	
	get_sem_count(sem, &count);
	dprintf("testsem (%s): sem count after releasing 4 is %d\n",
			(count == 6) ? "pass" : "FAIL", count);
	
	/* Test trying to acquire more than available with timeout */
	dprintf("testsem (pass): *** test acquire_sem_etc trying to get more than available\n");
	status = acquire_sem_etc(sem, 10, B_RELATIVE_TIMEOUT, 200000); // Try to get 10, but only 6 available
	dprintf("testsem (%s): acquire_sem_etc(sem, 10) with timeout returned %d\n",
			(status == B_TIMED_OUT) ? "pass" : "FAIL", status);
	
	delete_sem(sem);
}

/* Test negative semaphore count (threads waiting) */
static volatile int32 waiter_acquired = 0;
static sem_id waiter_sem;

void sem_negative_count_test()
{
	status_t status;
	int32 count;
	thread_id t;
	
	waiter_sem = create_sem(0, "negative_count_test");
	dprintf("testsem (%s): created negative count test sem %d with count 0\n",
			(waiter_sem >= 0) ? "pass" : "FAIL", waiter_sem);
	
	/* Spawn thread that will wait on semaphore */
	dprintf("testsem (pass): *** spawning waiter thread\n");
	waiter_acquired = 0;
	t = spawn_thread(sem_waiter_thread, "sem_waiter", B_NORMAL_PRIORITY, NULL);
	resume_thread(t);
	
	/* Give thread time to block */
	usleep(200000);
	
	/* Check that count is negative */
	dprintf("testsem (pass): *** checking for negative count\n");
	status = get_sem_count(waiter_sem, &count);
	dprintf("testsem (%s): get_sem_count returned %d with count %d\n",
			((status == 0) && (count < 0)) ? "pass" : "FAIL", status, count);
	dprintf("testsem (%s): waiter_acquired is still 0\n",
			(waiter_acquired == 0) ? "pass" : "FAIL");
	
	/* Release semaphore and verify thread acquires it */
	dprintf("testsem (pass): *** releasing semaphore\n");
	release_sem(waiter_sem);
	
	/* Give thread time to acquire */
	usleep(200000);
	
	dprintf("testsem (%s): waiter_acquired is now 1\n",
			(waiter_acquired == 1) ? "pass" : "FAIL");
	
	status = get_sem_count(waiter_sem, &count);
	dprintf("testsem (%s): count is now %d\n",
			(count == 0) ? "pass" : "FAIL", count);
	
	wait_for_thread(t, &status);
	delete_sem(waiter_sem);
}

static int32 sem_waiter_thread(void *arg)
{
	acquire_sem(waiter_sem);
	waiter_acquired = 1;
	return 0;
}

/* Test delete_sem with waiting threads */
static volatile int32 delete_waiter_unblocked = 0;
static volatile int32 delete_waiter_result = 0;
static sem_id delete_test_sem;

void sem_delete_with_waiters_test()
{
	thread_id t;
	status_t status;
	int32 thread_status;
	
	delete_test_sem = create_sem(0, "delete_waiter_test");
	dprintf("testsem (%s): created delete waiter test sem %d\n",
			(delete_test_sem >= 0) ? "pass" : "FAIL", delete_test_sem);
	
	/* Spawn thread that will wait on semaphore */
	dprintf("testsem (pass): *** spawning thread to wait on sem\n");
	delete_waiter_unblocked = 0;
	delete_waiter_result = 0;
	t = spawn_thread(sem_delete_waiter_thread, "delete_waiter", B_NORMAL_PRIORITY, NULL);
	resume_thread(t);
	
	/* Give thread time to block */
	usleep(200000);
	
	dprintf("testsem (%s): thread is waiting (unblocked=%d)\n",
			(delete_waiter_unblocked == 0) ? "pass" : "FAIL", delete_waiter_unblocked);
	
	/* Delete semaphore - should unblock waiting thread with B_BAD_SEM_ID */
	dprintf("testsem (pass): *** deleting semaphore with waiting thread\n");
	status = delete_sem(delete_test_sem);
	dprintf("testsem (%s): delete_sem returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);
	
	/* Wait for thread to finish */
	wait_for_thread(t, &thread_status);
	
	dprintf("testsem (%s): waiting thread unblocked with result %d\n",
			((delete_waiter_unblocked == 1) && (delete_waiter_result == B_BAD_SEM_ID)) ? "pass" : "FAIL",
			delete_waiter_result);
}

static int32 sem_delete_waiter_thread(void *arg)
{
	status_t result = acquire_sem(delete_test_sem);
	delete_waiter_result = result;
	delete_waiter_unblocked = 1;
	return 0;
}

/* Test get_next_sem_info (iterating through team's semaphores) */
void sem_iteration_test()
{
	sem_id sems[5];
	sem_info info;
	int32 cookie = 0;
	int found_count = 0;
	status_t status;
	int i;
	
	dprintf("testsem (pass): *** creating 5 semaphores for iteration test\n");
	for (i = 0; i < 5; i++) {
		char name[32];
		snprintf(name, sizeof(name), "iter_sem_%d", i);
		sems[i] = create_sem(i, name);
		dprintf("testsem (%s): created sem %d: %s\n",
				(sems[i] >= 0) ? "pass" : "FAIL", sems[i], name);
	}
	
	/* Iterate through all semaphores for this team */
	dprintf("testsem (pass): *** iterating through team semaphores\n");
	cookie = 0;
	found_count = 0;
	while ((status = get_next_sem_info(0, &cookie, &info)) == B_OK) {
		/* Check if this is one of our test semaphores */
		for (i = 0; i < 5; i++) {
			if (info.sem == sems[i]) {
				found_count++;
				dprintf("testsem (pass): found sem %d: %s (count=%d)\n",
						info.sem, info.name, info.count);
				break;
			}
		}
	}
	
	dprintf("testsem (%s): found %d of 5 test semaphores\n",
			(found_count == 5) ? "pass" : "FAIL", found_count);
	dprintf("testsem (%s): get_next_sem_info returned %d at end\n",
			(status == B_BAD_VALUE) ? "pass" : "FAIL", status);
	
	/* Clean up */
	for (i = 0; i < 5; i++) {
		delete_sem(sems[i]);
	}
}

/* Test error handling for invalid count values */
void sem_error_handling_test()
{
	sem_id sem;
	status_t status;
	
	sem = create_sem(5, "error_test");
	dprintf("testsem (%s): created error test sem %d\n",
			(sem >= 0) ? "pass" : "FAIL", sem);
	
	/* Test create_sem with negative count */
	dprintf("testsem (pass): *** test create_sem with count=-1 (invalid)\n");
	sem_id bad_sem = create_sem(-1, "bad_sem");
	dprintf("testsem (%s): create_sem(-1) returned %d\n",
			(bad_sem == B_BAD_VALUE) ? "pass" : "FAIL", bad_sem);
	
	/* Test acquire with count < 1 */
	dprintf("testsem (pass): *** test acquire_sem_etc with count=0 (invalid)\n");
	status = acquire_sem_etc(sem, 0, 0, 0);
	dprintf("testsem (%s): acquire_sem_etc(sem, 0) returned %d\n",
			(status == B_BAD_VALUE) ? "pass" : "FAIL", status);
	
	dprintf("testsem (pass): *** test acquire_sem_etc with count=-1 (invalid)\n");
	status = acquire_sem_etc(sem, -1, 0, 0);
	dprintf("testsem (%s): acquire_sem_etc(sem, -1) returned %d\n",
			(status == B_BAD_VALUE) ? "pass" : "FAIL", status);
	
	/* Test release with count < 0 */
	dprintf("testsem (pass): *** test release_sem_etc with count=-1 (invalid)\n");
	status = release_sem_etc(sem, -1, 0);
	dprintf("testsem (%s): release_sem_etc(sem, -1) returned %d\n",
			(status == B_BAD_VALUE) ? "pass" : "FAIL", status);
	
	dprintf("testsem (pass): *** test release_sem_etc with count=0 (invalid)\n");
	status = release_sem_etc(sem, 0, 0);
	dprintf("testsem (%s): release_sem_etc(sem, 0) returned %d\n",
			(status == B_BAD_VALUE) ? "pass" : "FAIL", status);
	
	/* Test operations on invalid sem_id */
	dprintf("testsem (pass): *** test operations on invalid sem_id\n");
	status = acquire_sem(99999);
	dprintf("testsem (%s): acquire_sem(invalid) returned %d\n",
			(status == B_BAD_SEM_ID) ? "pass" : "FAIL", status);
	
	status = release_sem(99999);
	dprintf("testsem (%s): release_sem(invalid) returned %d\n",
			(status == B_BAD_SEM_ID) ? "pass" : "FAIL", status);
	
	status = delete_sem(99999);
	dprintf("testsem (%s): delete_sem(invalid) returned %d\n",
			(status == B_BAD_SEM_ID) ? "pass" : "FAIL", status);
	
	int32 count;
	status = get_sem_count(99999, &count);
	dprintf("testsem (%s): get_sem_count(invalid) returned %d\n",
			(status == B_BAD_SEM_ID) ? "pass" : "FAIL", status);
	
	sem_info info;
	status = get_sem_info(99999, &info);
	dprintf("testsem (%s): get_sem_info(invalid) returned %d\n",
			(status == B_BAD_SEM_ID) ? "pass" : "FAIL", status);
	
	/* Test set_sem_owner with invalid team_id */
	dprintf("testsem (pass): *** test set_sem_owner with invalid team_id\n");
	status = set_sem_owner(sem, -999);
	dprintf("testsem (%s): set_sem_owner(invalid team) returned %d\n",
			(status == B_BAD_TEAM_ID) ? "pass" : "FAIL", status);
	
	status = set_sem_owner(99999, 0);
	dprintf("testsem (%s): set_sem_owner(invalid sem) returned %d\n",
			(status == B_BAD_SEM_ID) ? "pass" : "FAIL", status);
	
	/* Test get_next_sem_info with invalid team_id */
	dprintf("testsem (pass): *** test get_next_sem_info with invalid team_id\n");
	int32 cookie = 0;
	status = get_next_sem_info(-999, &cookie, &info);
	dprintf("testsem (%s): get_next_sem_info(invalid team) returned %d\n",
			(status == B_BAD_TEAM_ID) ? "pass" : "FAIL", status);
	
	delete_sem(sem);
}

/* Test that count can accumulate beyond initial value */
void sem_accumulation_test()
{
	sem_id sem;
	status_t status;
	int32 count;
	int i;
	
	sem = create_sem(2, "accumulation_test");
	dprintf("testsem (%s): created accumulation test sem %d with initial count 2\n",
			(sem >= 0) ? "pass" : "FAIL", sem);
	
	/* Release multiple times without acquiring */
	dprintf("testsem (pass): *** releasing sem 10 times without acquiring\n");
	for (i = 0; i < 10; i++) {
		status = release_sem(sem);
		if (status != 0) {
			dprintf("testsem (FAIL): release_sem failed at iteration %d with status %d\n", i, status);
			break;
		}
	}
	
	get_sem_count(sem, &count);
	dprintf("testsem (%s): sem count after 10 releases is %d (expected 12)\n",
			(count == 12) ? "pass" : "FAIL", count);
	
	/* Acquire them all back */
	dprintf("testsem (pass): *** acquiring all 12 counts\n");
	status = acquire_sem_etc(sem, 12, 0, 0);
	dprintf("testsem (%s): acquire_sem_etc(sem, 12) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);
	
	get_sem_count(sem, &count);
	dprintf("testsem (%s): sem count after acquiring 12 is %d\n",
			(count == 0) ? "pass" : "FAIL", count);
	
	/* Test release_sem_etc with large count */
	dprintf("testsem (pass): *** test release_sem_etc with count=100\n");
	status = release_sem_etc(sem, 100, 0);
	dprintf("testsem (%s): release_sem_etc(sem, 100) returned %d\n",
			(status == 0) ? "pass" : "FAIL", status);
	
	get_sem_count(sem, &count);
	dprintf("testsem (%s): sem count is now %d\n",
			(count == 100) ? "pass" : "FAIL", count);
	
	delete_sem(sem);
}
