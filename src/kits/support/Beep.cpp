/*
 * Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2007, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <Beep.h>

#include <stdio.h>

#include <image.h>

#ifndef __VOS__
#include <DataExchange.h>
#include <MediaSounds.h>
#endif


#ifdef __VOS__
// libbe cannot link media2 (media2 already depends on libbe); resolve on demand.
typedef status_t (*beep_ensure_fn)(void);
typedef status_t (*beep_add_fn)(const char*, uint32);
typedef status_t (*beep_play_fn)(const char*);

static image_id		sMedia2Image = -1;
static beep_ensure_fn	sEnsureEvents = NULL;
static beep_add_fn	sAddEvent = NULL;
static beep_play_fn	sPlayEvent = NULL;


static status_t
_ResolveMedia2()
{
	if (sMedia2Image >= 0)
		return B_OK;

	const char* paths[] = {
		"/lib/libmedia2.so",
		"/system/lib/libmedia2.so",
		"libmedia2.so",
		NULL
	};

	for (int32 i = 0; paths[i] != NULL; i++) {
		image_id id = load_add_on(paths[i]);
		if (id < 0)
			continue;

		if (get_image_symbol(id, "vos_system_beep_ensure_events",
				B_SYMBOL_TYPE_TEXT, (void**)&sEnsureEvents) != B_OK
			|| get_image_symbol(id, "vos_system_beep_add_event",
				B_SYMBOL_TYPE_TEXT, (void**)&sAddEvent) != B_OK
			|| get_image_symbol(id, "vos_system_beep_play",
				B_SYMBOL_TYPE_TEXT, (void**)&sPlayEvent) != B_OK) {
			unload_add_on(id);
			continue;
		}

		sMedia2Image = id;
		return B_OK;
	}

	return B_ERROR;
}
#endif


status_t
system_beep(const char* eventName)
{
#ifndef __VOS__
	BMessenger messenger("application/x-vnd.Be.addon-host");
	if (!messenger.IsValid())
		return B_ERROR;

	BMessage msg(MEDIA_ADD_ON_SERVER_PLAY_MEDIA), reply;
	msg.AddString(MEDIA_NAME_KEY, eventName ? eventName : MEDIA_SOUNDS_BEEP);
	msg.AddString(MEDIA_TYPE_KEY, MEDIA_TYPE_SOUNDS);

	status_t status = messenger.SendMessage(&msg, &reply);
	if (status != B_OK || reply.FindInt32("error", &status) != B_OK)
		status = B_BAD_REPLY;

	return status;
#else
	if (_ResolveMedia2() != B_OK)
		return B_ERROR;
	return sPlayEvent(eventName ? eventName : "Beep");
#endif
}


status_t
beep()
{
	return system_beep(NULL);
}


status_t
add_system_beep_event(const char* name, uint32 flags)
{
#ifndef __VOS__
	BMessenger messenger("application/x-vnd.Be.media-server");
	if (!messenger.IsValid())
		return B_ERROR;

	BMessage msg(MEDIA_SERVER_ADD_SYSTEM_BEEP_EVENT), reply;
	msg.AddString(MEDIA_NAME_KEY, name);
	msg.AddString(MEDIA_TYPE_KEY, MEDIA_TYPE_SOUNDS);
	msg.AddInt32(MEDIA_FLAGS_KEY, flags);

	status_t status = messenger.SendMessage(&msg, &reply);
	if (status != B_OK || reply.FindInt32("error", &status) != B_OK)
		status = B_BAD_REPLY;

	return status;
#else
	if (_ResolveMedia2() != B_OK)
		return B_ERROR;
	return sAddEvent(name, flags);
#endif
}
