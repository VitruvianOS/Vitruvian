/*
 * Copyright 2018, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include <debugger.h>
#include <OS.h>
#include <Debug.h>

#include "syscalls.h"


void
_kern_debugger(const char *message)
{
	exit(1);
}


status_t
_kern_kernel_debugger(const char *message)
{
	_kern_debugger(message);
	return B_OK;
}


int
_kern_disable_debugger(int state)
{
	UNIMPLEMENTED();
	return 0;
}


status_t
_kern_install_default_debugger(port_id debuggerPort)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


port_id
_kern_install_team_debugger(team_id team, port_id debuggerPort)
{
	UNIMPLEMENTED();
	return -1;
}


status_t
_kern_remove_team_debugger(team_id team)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_debug_thread(thread_id thread)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


void
_kern_wait_for_debugger(void)
{
	UNIMPLEMENTED();
}


status_t
_kern_set_debugger_breakpoint(void *address, uint32 type, int32 length,
	bool watchpoint)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_clear_debugger_breakpoint(void *address,
	bool watchpoint)

{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_set_debugger_watchpoint(void *address, uint32 type, int32 length)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_clear_debugger_watchpoint(void *address)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


void
_kern_debug_output(const char* message)
{
	printf("%s", message);
}


void
_kern_ktrace_output(const char* message)
{
	UNIMPLEMENTED();
}
