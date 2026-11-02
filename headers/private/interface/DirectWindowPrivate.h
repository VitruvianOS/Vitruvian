/*
 * Copyright 2005, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DIRECT_WINDOW_PRIVATE_H
#define _DIRECT_WINDOW_PRIVATE_H


#include <DirectWindow.h>
#include <OS.h>
#include <SharedEvent.h>


#define DIRECT_BUFFER_INFO_AREA_SIZE B_PAGE_SIZE

/* Two event words ride at the end of the shared area, after the clip
 * list tail: the client maps the same pages, so no sem ids cross. */
#define DIRECT_WINDOW_SYNC_RESERVED (2 * (int)sizeof(shared_event_state))


struct direct_window_sync_data {
	area_id	area;
};


static inline shared_event_state*
direct_window_disable_state(direct_buffer_info* info)
{
	return (shared_event_state*)((uint8*)info + DIRECT_BUFFER_INFO_AREA_SIZE
		- DIRECT_WINDOW_SYNC_RESERVED);
}


static inline shared_event_state*
direct_window_ack_state(direct_buffer_info* info)
{
	return direct_window_disable_state(info) + 1;
}


#endif	// _DIRECT_WINDOW_PRIVATE_H
