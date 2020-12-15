/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <OS.h>

#include <syscalls.h>


extern status_t
_kern_send_signal(int32 id, uint32 signal,
	const union sigval* userValue, uint32 flags)
{
	return kill(id, signal);
}
