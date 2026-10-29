/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <GLRendererPriority.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>


namespace GLRendererPriority {

const char* kSettingsPath = "opengl/render_priority";
const char* kPriorityKey = "render_priority";
const char* kRendererDir = "/system/add-ons/opengl";


status_t
GetInstalledRenderers(BStringList& renderers)
{
	BDirectory dir(kRendererDir);
	status_t status = dir.InitCheck();
	if (status != B_OK) {
		// No add-on directory yet: no renderers, but not a failure.
		return B_OK;
	}

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		if (!entry.IsFile())
			continue;

		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) == B_OK && name[0] != '\0')
			renderers.Add(BString(name));
	}

	return B_OK;
}


status_t
Load(BStringList& priority)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(kSettingsPath);

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		// Absent (or unreadable) priority file: caller falls back to
		// directory order. Not an error.
		return B_OK;
	}

	BMessage message;
	if (message.Unflatten(&file) != B_OK) {
		// Corrupt file: same fallback as absent.
		return B_OK;
	}

	const char* renderer;
	for (int32 index = 0; message.FindString(kPriorityKey, index, &renderer)
			== B_OK; index++) {
		priority.Add(BString(renderer));
	}

	return B_OK;
}


status_t
Save(const BStringList& priority)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(kSettingsPath);

	// Create the enclosing "opengl" directory if it is not there yet.
	BPath parent;
	if (path.GetParent(&parent) == B_OK)
		create_directory(parent.Path(), 0755);

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;

	BMessage message;
	for (int32 index = 0; index < priority.CountStrings(); index++)
		message.AddString(kPriorityKey, priority.StringAt(index).String());

	return message.Flatten(&file);
}


status_t
GetEffective(BStringList& priority)
{
	BStringList installed;
	status_t status = GetInstalledRenderers(installed);
	if (status != B_OK)
		return status;

	BStringList saved;
	status = Load(saved);
	if (status != B_OK)
		return status;

	// Saved entries that are still installed, in saved order...
	for (int32 index = 0; index < saved.CountStrings(); index++) {
		BString name = saved.StringAt(index);
		if (installed.HasString(name) && !priority.HasString(name))
			priority.Add(name);
	}

	// ...then installed renderers the saved list does not mention, in
	// directory order (what BGLView would try without a priority file).
	for (int32 index = 0; index < installed.CountStrings(); index++) {
		BString name = installed.StringAt(index);
		if (!priority.HasString(name))
			priority.Add(name);
	}

	return B_OK;
}

} // namespace GLRendererPriority
