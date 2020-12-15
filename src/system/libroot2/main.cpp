/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "main.h"

#include <kernel/messaging.h>

#include <stdlib.h>
#include <linux/limits.h>

#include "KernelDebug.h"
#include "messaging/MessagingService.h"


mode_t __gUmask = 022;
int32 __gCPUCount;
int __libc_argc;
char** __libc_argv;


// This function is executed before everything else
void __attribute__ ((constructor))
init_kernel_layer(int argc, char** argv)
{
	TRACE("init_kernel_layer()\n");

	// Init global stuff
	__gCPUCount = sysconf(_SC_NPROCESSORS_ONLN);
	__libc_argc = argc;
	__libc_argv = argv;

	// Set screen
	setenv("TARGET_SCREEN", "root", 1);

	// Init the kernel layer
	port_init();

	if (argv[0] != NULL && strcmp(argv[0], "registrar") <= 0)
		init_messaging_service();
}


void __attribute__ ((destructor))
deinit__kernel_layer()
{
	TRACE("deinit_kernel_layer()\n");
	teardown_ports();
}
