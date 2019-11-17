/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <SupportDefs.h>

#include <unistd.h>
#include <linux/reboot.h>
#include <sys/reboot.h>

#include <syscalls.h>


status_t
_kern_shutdown(bool reb)
{
	sync();

	if (reb == true)
		reboot(RB_AUTOBOOT);
	else
		reboot(LINUX_REBOOT_CMD_POWER_OFF);

	return B_OK;
}
