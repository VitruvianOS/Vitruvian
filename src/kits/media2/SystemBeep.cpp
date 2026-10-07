/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "SystemBeep.h"

#include <stdio.h>
#include <string.h>

#include <Entry.h>
#include <FindDirectory.h>
#include <MediaFiles.h>
#include <Path.h>

#include <media2/PlaySound.h>


static const struct {
	const char*	event;
	const char*	file;
} kDefaultEvents[] = {
	{ "Beep", "Beep.wav" },
	{ "Attention", "Attention.wav" },
	{ "Startup", "Startup.wav" },
	{ "WindowActivated", "WindowActivated.wav" },
	{ "KeyDown", "KeyDown.wav" },
	{ "LowBattery", "LowBattery.wav" },
	{ NULL, NULL }
};


static status_t
_FindSystemSound(const char* fileName, entry_ref* ref)
{
	if (fileName == NULL || ref == NULL)
		return B_BAD_VALUE;

	BPath path;
	if (find_directory(B_SYSTEM_SOUNDS_DIRECTORY, &path) != B_OK)
		return B_ENTRY_NOT_FOUND;
	if (path.Append(fileName) != B_OK)
		return B_ERROR;

	BEntry entry(path.Path());
	if (!entry.Exists())
		return B_ENTRY_NOT_FOUND;
	return entry.GetRef(ref);
}


static status_t
_RegisterEvent(BMediaFiles& files, const char* event)
{
	entry_ref existing;
	if (files.GetRefFor(BMediaFiles::B_SOUNDS, event, &existing) == B_OK)
		return B_OK;

	char fileName[256];
	snprintf(fileName, sizeof(fileName), "%s.wav", event);

	entry_ref ref;
	if (_FindSystemSound(fileName, &ref) == B_OK)
		return files.SetRefFor(BMediaFiles::B_SOUNDS, event, ref);

	// Empty ref keeps the event listed with no sound assigned.
	entry_ref none;
	return files.SetRefFor(BMediaFiles::B_SOUNDS, event, none);
}


extern "C" status_t
vos_system_beep_ensure_events(void)
{
	BMediaFiles files;
	for (int32 i = 0; kDefaultEvents[i].event != NULL; i++)
		_RegisterEvent(files, kDefaultEvents[i].event);
	return B_OK;
}


extern "C" status_t
vos_system_beep_add_event(const char* name, uint32 /*flags*/)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	vos_system_beep_ensure_events();

	BMediaFiles files;
	return _RegisterEvent(files, name);
}


extern "C" status_t
vos_system_beep_play(const char* name)
{
	if (name == NULL || name[0] == '\0')
		name = "Beep";

	vos_system_beep_ensure_events();

	BMediaFiles files;
	entry_ref ref;
	if (files.GetRefFor(BMediaFiles::B_SOUNDS, name, &ref) != B_OK) {
		char fileName[256];
		snprintf(fileName, sizeof(fileName), "%s.wav", name);
		if (_FindSystemSound(fileName, &ref) != B_OK)
			return B_ENTRY_NOT_FOUND;
	}

	if (ref.name == NULL || ref.name[0] == '\0')
		return B_ENTRY_NOT_FOUND;

	sound_handle handle = play_sound(&ref, true, false, false);
	if (handle < 0)
		return handle;
	return B_OK;
}
