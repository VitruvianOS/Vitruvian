/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "main.h"

#include <kernel/messaging.h>

#include "messaging/MessagingService.h"


// This function should be called before anything else
void __attribute__ ((constructor))
init_kernel_layer()
{
	printf("init_kernel_layer()\n");
	init_area_map();
	port_init();
	init_thread();
	init_messaging_service();
}


void __attribute__ ((destructor))
deinit__kernel_layer()
{
	printf("deinit_kernel_layer()\n");
}
