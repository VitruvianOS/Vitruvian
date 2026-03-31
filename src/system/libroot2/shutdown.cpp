/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <SupportDefs.h>

#include <errno.h>
#include <stdio.h>
#include <linux/reboot.h>
#include <sys/syscall.h>

#include <syscalls.h>


status_t
_kern_shutdown(bool reb)
{
	if (geteuid() != 0) {
		fprintf(stderr, "_kern_shutdown: Permission denied (not root)\n");
		return B_NOT_ALLOWED;
	}

	sync();

	long result = syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
		reb ? LINUX_REBOOT_CMD_RESTART : LINUX_REBOOT_CMD_POWER_OFF, NULL);

	if (result < 0) {
		fprintf(stderr, "_kern_shutdown: reboot syscall failed: %s\n",
			strerror(errno));
		return B_ERROR;
	}

	return B_ERROR;
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

	// Try S3 suspend
	ssize_t ret = write(fd, "mem\n", 4);

	// Fall back to freeze
	if (ret < 0)
		ret = write(fd, "freeze\n", 7);
	close(fd);

	return (ret > 0) ? B_OK : B_ERROR;
}
