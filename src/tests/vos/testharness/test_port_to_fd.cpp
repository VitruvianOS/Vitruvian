/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <OS.h>

#include <poll.h>
#include <stdio.h>
#include <unistd.h>


static int32
writer(void* data)
{
	snooze(100000);
	port_id port = *(port_id*)data;
	int32 value = 42;
	return write_port(port, 'test', &value, sizeof(value));
}


int
main(void)
{
	port_id port = create_port(4, "port_to_fd test");
	if (port < 0) {
		printf("create_port FAIL\n");
		return 1;
	}

	int fd = port_to_fd(port);
	if (fd < 0) {
		printf("port_to_fd FAIL (%d)\n", fd);
		return 1;
	}

	struct pollfd pfd = { fd, POLLIN, 0 };
	if (poll(&pfd, 1, 50) != 0) {
		printf("poll on empty port FAIL\n");
		return 1;
	}

	thread_id thread = spawn_thread(writer, "writer", B_NORMAL_PRIORITY,
		&port);
	resume_thread(thread);

	pfd.revents = 0;
	if (poll(&pfd, 1, 2000) != 1 || !(pfd.revents & POLLIN)) {
		printf("poll after write FAIL\n");
		return 1;
	}

	int32 code = 0;
	int32 value = 0;
	ssize_t size = read_port_etc(port, &code, &value, sizeof(value),
		B_RELATIVE_TIMEOUT, 0);
	if (size != (ssize_t)sizeof(value) || code != 'test' || value != 42) {
		printf("read_port FAIL\n");
		return 1;
	}

	pfd.revents = 0;
	if (poll(&pfd, 1, 50) != 0) {
		printf("poll after read FAIL\n");
		return 1;
	}

	status_t result;
	wait_for_thread(thread, &result);
	close(fd);
	delete_port(port);
	printf("test_port_to_fd OK\n");
	return 0;
}
