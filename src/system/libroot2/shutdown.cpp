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
