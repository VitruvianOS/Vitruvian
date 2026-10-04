/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _HOSTSHIMS_OS_H
#define _HOSTSHIMS_OS_H

#include <SupportDefs.h>

typedef status_t (*thread_func)(void*);

thread_id spawn_thread(thread_func func, const char* name, int32 priority,
	void* data);
status_t resume_thread(thread_id thread);
status_t wait_for_thread(thread_id thread, status_t* _returnValue);

#endif
