// Standard Includes -----------------------------------------------------------
#include <stdio.h>
#include <string.h>

// System Includes -------------------------------------------------------------
#include <OS.h>

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------

// Local Defines ---------------------------------------------------------------
#define dprintf printf

// Globals ---------------------------------------------------------------------

static void port_test();


int main()
{
	port_test();
	return 0;
}


static int32 port_test_thread_func(void *arg);

port_id test_p1, test_p2, test_p3, test_p4, test_p5;

void port_test()
{
	char testdata[5];
	thread_id t;
	int32 dummy;
	int32 dummy2;
	port_id dummy_port;
	status_t status;

	strcpy(testdata, "abcd");

	dprintf("porttest: begin test\n");

	/* Create ports */

	test_p1 = create_port(1,    "test port #1");

	dprintf("porttest (%s):'test port #1' has id %ld\n",
			(test_p1 >= 0) ? "pass" : "FAIL", test_p1);

	/* Manipulate ports */

	dummy_port = find_port("test port #1");
	dprintf("porttest (%s): find_port(test port #1) returned %ld\n",
			(test_p1 == dummy_port) ? "pass" : "FAIL", dummy_port);

	dprintf("porttest (info): write_port() on 1, 2 and 3\n");
	status = write_port(test_p1, 1, &testdata, sizeof(testdata));
	dprintf("porttest (%s): write_port(test port #1) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	dummy = port_count(test_p1);
	dprintf("porttest (%s): port_count(test_p1) = %ld\n",
			(dummy == 1) ? "pass" : "FAIL", (long)dummy);

	status = write_port_etc(test_p1, 1, &testdata, sizeof(testdata), B_TIMEOUT, 1000000);
	dprintf("porttest (%s): write_port() on 1 with timeout of 1 sec (blocks 1 sec) returned %ld\n",
			(status == B_TIMED_OUT) ? "pass" : "FAIL", status);

	port_info info;
	if (get_port_info(test_p1, &info) != B_OK)
		printf("err port info\n");

	printf("%d %d\n", info.team, info.port);

	dprintf("porttest (pass): spawning thread for port 1\n");
	t = spawn_thread(port_test_thread_func, "port_test", B_NORMAL_PRIORITY, NULL);
	resume_thread(t);

	status = write_port(test_p1, 1, &testdata, sizeof(testdata));
	dprintf("porttest (%s): write_port() on 1 returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	// now we can write more (no blocking)
	status = write_port(test_p1, 2, &testdata, sizeof(testdata));
	dprintf("porttest (%s): write_port() on 2 returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	status = write_port(test_p1, 3, &testdata, sizeof(testdata));
	dprintf("porttest (%s): write_port() on 3 returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	dprintf("porttest: waiting on spawned thread\n");
	wait_for_thread(t, NULL);

	dprintf("porttest: end test main thread\n");
}


static int32
port_test_thread_func(void *arg)
{
	int32 msg_code;
	char buf[6];
	buf[5] = '\0';
	status_t status;

	dprintf("(reading thread) porttest: enter port_test_thread_func()\n");

	status = read_port(test_p1, &msg_code, &buf, 3);
	dprintf("(reading thread) porttest (%s): read_port() on 1, code %ld, buf %s, returned %ld\n",
			(status >= 0) ? "pass" : "FAIL", msg_code, buf, status);

	status = read_port(test_p1, &msg_code, &buf, 4);
	dprintf("(reading thread) porttest (%s): read_port() on 1, code %ld, buf %s, returned %ld\n",
			(status >= 0) ? "pass" : "FAIL", msg_code, buf, status);
	buf[4] = 'X';

	status = read_port(test_p1, &msg_code, &buf, 5);
	dprintf("(reading thread) porttest (%s): read_port() on 1, code %ld, buf %s, returned %ld\n",
			((status >= 0) && (buf[4] != 'X')) ? "pass" : "FAIL", msg_code, buf, status);

	status = read_port(test_p1, &msg_code, &buf, 3);
	dprintf("(reading thread) porttest (%s): read_port() on 1, code %ld, buf %s, returned %ld\n",
			(status >= 0) ? "pass" : "FAIL", msg_code, buf, status);
	
	status = delete_port(test_p1);
	dprintf("(reading thread) porttest (%s): delete_port() on 1 (from other thread) returned %ld\n",
			(status == 0) ? "pass" : "FAIL", status);

	dprintf("(reading thread) porttest: leave port_test_thread_func()\n");

	return 0;
}
