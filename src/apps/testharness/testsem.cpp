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
static int32 sem_test_thread_one(void *arg);
static int32 sem_test_thread_two(void *arg);
static int32 sem_test_thread_three(void *arg);
static int32 sem_test_thread_four(void *arg);

int main()
{
	dprintf("testsem: start test\n");
	sem_test();
	dprintf("testsem: repeating\n");
	sem_test();
	dprintf("testsem: thread test\n");
	sem_thread_test();
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

	dprintf("testsem (%s): 'sem #1' has id %ld\n",
			(test_s1 >= 0) ? "pass" : "FAIL", test_s1);
	dprintf("testsem (%s): 'sem #2' has id %ld\n",
			(test_s2 >= 0) ? "pass" : "FAIL", test_s2);
	dprintf("testsem (%s): 'sem #3' has id %ld\n",
			(test_s3 >= 0) ? "pass" : "FAIL", test_s3);

	/* Manipulate semaphores */

	/* test get_sem_count on freshly created sem*/
	dprintf("testsem (pass): *** test get_sem_count() on freshly created sem with count of 1\n");
	status = get_sem_count(test_s2, &count);
	dprintf("testsem (%s): get_sem_count(test_s2) returned %ld with count %ld\n",
			((status == 0) && (count == 1)) ? "pass" : "FAIL", status, count);

	/* test acquire_sem */
	dprintf("testsem (pass): *** test acquire_sem\n");
	status = acquire_sem(test_s2);
	dprintf("testsem (%s): acquire_sem(test_s2) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* test get_sem_count after acquire*/
	dprintf("testsem (pass): *** test get_sem_count() after the acquire\n");
	status = get_sem_count(test_s2, &count);
	dprintf("testsem (%s): get_sem_count(test_s2) returned %ld with count %ld\n",
			((status == 0) && (count == 0)) ? "pass" : "FAIL", status, count);

	/* test acquire_sem with timeout on sem inited to >0 */
	dprintf("testsem (pass): *** test acquire_sem with timeout on sem inited to count of 1\n");
	status = acquire_sem_etc(test_s2, 1, B_TIMEOUT, 1000000);
	dprintf("testsem (%s): acquire_sem_etc(test_s2, 1) with 1s timeout returned %ld\n",
			(status == B_TIMED_OUT) ? "pass" : "FAIL", status);

	/* test release_sem */
	dprintf("testsem (pass): *** test release_sem\n");
	status = release_sem(test_s2);
	dprintf("testsem (%s): release_sem(test_s2) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* test acquire_sem with timeout on sem inited to 0 */
	dprintf("testsem (pass): *** test acquire_sem with timeout on sem inited to count of 0\n");
	status = acquire_sem_etc(test_s1, 1, B_TIMEOUT, 1000000);
	dprintf("testsem (%s): acquire_sem_etc(test_s1, 1) with 1s timeout returned %ld\n",
			(status == B_TIMED_OUT) ? "pass" : "FAIL", status);

	/* test acquire_sem with zero timeout */
	dprintf("testsem (pass): *** test acquire_sem with timeout of 0\n");
	status = acquire_sem_etc(test_s1, 1, B_TIMEOUT, 0);
	dprintf("testsem (%s): acquire_sem_etc(test_s1, 1) with 0s timeout returned %ld\n",
			(status == B_WOULD_BLOCK) ? "pass" : "FAIL", status);

	/* test get_sem_count */
	dprintf("testsem (pass): *** test get_sem_count\n");
	status = get_sem_count(test_s3, &count);
	dprintf("testsem (%s): get_sem_count(test_s3) returned %ld with count %ld\n",
			((status == 0) && (count == 13)) ? "pass" : "FAIL", status, count);

	/* test acquire_sem_etc with no timeout */
	dprintf("testsem (pass): *** test acquire_sem with no timeout\n");
	status = acquire_sem_etc(test_s3, 13, 0, 0);
	dprintf("testsem (%s): acquire_sem(test_s3, 13) with no timeout returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* test get_sem_info and results of previous acquire_sem_etc */
	dprintf("testsem (pass): *** test get_sem_info\n");
	status = get_sem_info(test_s3, &info);
	dprintf("testsem (%s): get_sem_info(test_s3) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);
	dprintf("testsem (%s): sem count is now %ld\n",
			(info.count == 0) ? "pass" : "FAIL", info.count);

	/* test set_sem_owner */
	dprintf("testsem (pass): *** test set_sem_owner\n");
	status = set_sem_owner(test_s3, 10);
	dprintf("testsem (%s): set_sem_owner(test_s3) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* Delete semaphores */

	status = delete_sem(test_s1);
	dprintf("testsem (%s): delete_sem(test_s1) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	status = delete_sem(test_s2);
	dprintf("testsem (%s): delete_sem(test_s2) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	status = delete_sem(test_s3);
	dprintf("testsem (%s): delete_sem(test_s3) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	/* test delete of already deleted sem */
	status = delete_sem(test_s3);
	dprintf("testsem (%s): delete_sem(test_s3 again) returned %ld\n",
			(status < 0) ? "pass" : "FAIL", status);

	dprintf("testsem: end test\n");
}


void sem_thread_test()
{
	thread_id t1, t2, t3, t4;

	test_s4 = create_sem(0, "sem #3");

	dprintf("testsem (%s): 'sem #4' has id %ld\n",
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
