/*
 * Test: stale-ID generation guard
 *
 * Verifies that after delete + re-create (which may recycle the same
 * kernel ID), a stale handle is rejected by the generation guard.
 *
 * This exercises the IdGenerationGuard applied to sem, port, and area
 * in src/system/libroot2/{sem,port,area}.cpp.
 */

#include <OS.h>

#include <stdio.h>
#include <string.h>


#define dprintf printf


static int sFailCount = 0;


static void
test_sem_stale()
{
	dprintf("test_stale_id: sem ---\n");

	sem_id s1 = create_sem(1, "stale_sem");
	if (s1 < 0) {
		dprintf("FAIL: create_sem returned %ld\n", s1);
		sFailCount++;
		return;
	}
	dprintf("  create_sem -> %ld\n", s1);

	// Acquire works on live handle.
	status_t st = acquire_sem(s1);
	dprintf("  acquire_sem(s1): %s (status=%ld)\n",
		(st == B_OK) ? "pass" : "FAIL", st);
	if (st != B_OK) sFailCount++;

	// Release so we can delete.
	release_sem(s1);

	// Delete the semaphore.
	st = delete_sem(s1);
	dprintf("  delete_sem(s1): %s (status=%ld)\n",
		(st == B_OK) ? "pass" : "FAIL", st);
	if (st != B_OK) sFailCount++;

	// Stale handle must be rejected.
	st = acquire_sem(s1);
	dprintf("  acquire_sem(s1-stale): %s (expected B_BAD_SEM_ID=%ld, got %ld)\n",
		(st < 0) ? "pass" : "FAIL", (long)B_BAD_SEM_ID, (long)st);
	if (st >= 0) sFailCount++;

	// Create again — kernel may recycle the same integer.
	sem_id s2 = create_sem(1, "stale_sem_2");
	if (s2 < 0) {
		dprintf("FAIL: second create_sem returned %ld\n", s2);
		sFailCount++;
		return;
	}
	dprintf("  create_sem -> %ld\n", s2);

	// New handle works.
	st = acquire_sem(s2);
	dprintf("  acquire_sem(s2-new): %s (status=%ld)\n",
		(st == B_OK) ? "pass" : "FAIL", st);
	if (st != B_OK) sFailCount++;
	release_sem(s2);

	// Old handle still rejected even if same kernel ID.
	st = acquire_sem(s1);
	dprintf("  acquire_sem(s1-stale-after-recycle): %s (got %ld)\n",
		(st < 0) ? "pass" : "FAIL", (long)st);
	if (st >= 0) sFailCount++;

	delete_sem(s2);
}


static void
test_port_stale()
{
	dprintf("test_stale_id: port ---\n");

	port_id p1 = create_port(10, "stale_port");
	if (p1 < 0) {
		dprintf("FAIL: create_port returned %ld\n", p1);
		sFailCount++;
		return;
	}
	dprintf("  create_port -> %ld\n", p1);

	// Write then read so we know it works.
	const char* msg = "hello";
	ssize_t wr = write_port(p1, 1, msg, strlen(msg) + 1);
	dprintf("  write_port(p1): %s (ssize=%ld)\n",
		(wr > 0) ? "pass" : "FAIL", wr);
	if (wr <= 0) sFailCount++;

	// Delete.
	status_t st = delete_port(p1);
	dprintf("  delete_port(p1): %s (status=%ld)\n",
		(st == B_OK) ? "pass" : "FAIL", st);
	if (st != B_OK) sFailCount++;

	// Stale handle must be rejected.
	ssize_t sz = port_buffer_size(p1);
	dprintf("  port_buffer_size(p1-stale): %s (expected <0, got %ld)\n",
		(sz < 0) ? "pass" : "FAIL", sz);
	if (sz >= 0) sFailCount++;

	// Create again.
	port_id p2 = create_port(10, "stale_port_2");
	if (p2 < 0) {
		dprintf("FAIL: second create_port returned %ld\n", p2);
		sFailCount++;
		return;
	}
	dprintf("  create_port -> %ld\n", p2);

	// New handle works.
	wr = write_port(p2, 2, msg, strlen(msg) + 1);
	dprintf("  write_port(p2-new): %s (ssize=%ld)\n",
		(wr > 0) ? "pass" : "FAIL", wr);
	if (wr <= 0) sFailCount++;

	// Old handle still rejected.
	sz = port_buffer_size(p1);
	dprintf("  port_buffer_size(p1-stale-after-recycle): %s (got %ld)\n",
		(sz < 0) ? "pass" : "FAIL", sz);
	if (sz >= 0) sFailCount++;

	delete_port(p2);
}


static void
test_area_stale()
{
	dprintf("test_stale_id: area ---\n");

	void* addr1 = NULL;
	area_id a1 = create_area("stale_area", &addr1, B_ANY_ADDRESS,
		B_PAGE_SIZE, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	if (a1 < 0) {
		dprintf("FAIL: create_area returned %ld\n", a1);
		sFailCount++;
		return;
	}
	dprintf("  create_area -> %ld\n", a1);

	// Write to it so we know it's accessible.
	if (addr1)
		memset(addr1, 0x42, B_PAGE_SIZE);

	// Delete.
	status_t st = delete_area(a1);
	dprintf("  delete_area(a1): %s (status=%ld)\n",
		(st == B_OK) ? "pass" : "FAIL", st);
	if (st != B_OK) sFailCount++;

	// Stale handle must be rejected.
	area_info info;
	st = _get_area_info(a1, &info, sizeof(info));
	dprintf("  _get_area_info(a1-stale): %s (expected <0, got %ld)\n",
		(st < 0) ? "pass" : "FAIL", (long)st);
	if (st >= 0) sFailCount++;

	// Create again.
	void* addr2 = NULL;
	area_id a2 = create_area("stale_area_2", &addr2, B_ANY_ADDRESS,
		B_PAGE_SIZE, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	if (a2 < 0) {
		dprintf("FAIL: second create_area returned %ld\n", a2);
		sFailCount++;
		return;
	}
	dprintf("  create_area -> %ld\n", a2);

	// New handle works.
	st = _get_area_info(a2, &info, sizeof(info));
	dprintf("  _get_area_info(a2-new): %s (status=%ld)\n",
		(st == B_OK) ? "pass" : "FAIL", st);
	if (st != B_OK) sFailCount++;

	// Old handle still rejected.
	st = _get_area_info(a1, &info, sizeof(info));
	dprintf("  _get_area_info(a1-stale-after-recycle): %s (got %ld)\n",
		(st < 0) ? "pass" : "FAIL", (long)st);
	if (st >= 0) sFailCount++;

	delete_area(a2);
}


int
main()
{
	dprintf("test_stale_id: begin\n");

	test_sem_stale();
	test_port_stale();
	test_area_stale();

	dprintf("test_stale_id: end — %s (%d failure%s)\n",
		sFailCount == 0 ? "ALL PASS" : "FAILURES",
		sFailCount, sFailCount == 1 ? "" : "s");

	return sFailCount;
}
