/*
 * Copyright 2005, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef SHARED_CURSOR_AREA_H
#define SHARED_CURSOR_AREA_H


#include <SharedEvent.h>
#include <SupportDefs.h>


struct shared_cursor {
	uint32	pos;
	int32	read;
	// Wake word for the app_server cursor thread, posted by the
	// input_server on every cursor update.
	shared_event_state	event;
};


#endif	/* SHARED_CURSOR_AREA_H */
