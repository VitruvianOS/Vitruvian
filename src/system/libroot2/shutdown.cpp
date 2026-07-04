/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <SupportDefs.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <syscalls.h>


// Hand off to systemd. No raw reboot(2) fallback: on ext4 that path
// skips remount-ro and corrupts the journal — worse than returning
// B_ERROR to the caller.
status_t
_kern_shutdown(bool reb)
{
	if (geteuid() != 0) {
		fprintf(stderr, "_kern_shutdown: not root\n");
		return B_NOT_ALLOWED;
	}

	const char* verb = reb ? "reboot" : "poweroff";

	pid_t child = fork();
	if (child < 0) {
		fprintf(stderr, "_kern_shutdown: fork: %s\n", strerror(errno));
		return B_ERROR;
	}
	if (child == 0) {
		execl("/usr/bin/systemctl", "systemctl", verb, (char*)NULL);
		execl("/bin/systemctl",     "systemctl", verb, (char*)NULL);
		_exit(127);
	}

	int status = 0;
	if (waitpid(child, &status, 0) != child) {
		fprintf(stderr, "_kern_shutdown: waitpid: %s\n", strerror(errno));
		return B_ERROR;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "_kern_shutdown: systemctl %s failed (status=0x%x)\n",
			verb, status);
		return B_ERROR;
	}
	return B_OK;
}


status_t
_kern_suspend(void)
{
	int fd = open("/sys/power/state", O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "_kern_suspend: cannot open /sys/power/state: %s\n",
			strerror(errno));
		return B_ERROR;
	}

	ssize_t ret = write(fd, "mem\n", 4);
	if (ret < 0)
		ret = write(fd, "freeze\n", 7);
	close(fd);

	return ret > 0 ? B_OK : B_ERROR;
}
