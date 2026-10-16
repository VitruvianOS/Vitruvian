/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "DeskbarEntry.h"

#include <Bitmap.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <Node.h>
#include <NodeInfo.h>
#include <String.h>
#include <SymLink.h>
#include <TranslationUtils.h>

#include <stdio.h>
#include <string.h>


namespace {


// Nearest-neighbour resize into an RGBA32 bitmap: the manifest icon
// arrives at web sizes; the node attrs want exactly 32 and 16.
BBitmap*
ResizeIcon(const BBitmap* source, int size)
{
	if (source == NULL || source->InitCheck() != B_OK)
		return NULL;

	color_space space = source->ColorSpace();
	if (space != B_RGBA32 && space != B_RGB32 && space != B_RGB24
		&& space != B_CMAP8)
		return NULL;

	BBitmap* target = new BBitmap(BRect(0, 0, size - 1, size - 1),
		B_RGBA32);
	if (target == NULL || target->InitCheck() != B_OK) {
		delete target;
		return NULL;
	}

	int srcWidth = (int)source->Bounds().Width() + 1;
	int srcHeight = (int)source->Bounds().Height() + 1;
	const uint8* srcBits = (const uint8*)source->Bits();
	int32 srcRow = source->BytesPerRow();
	uint8* dstBits = (uint8*)target->Bits();
	int32 dstRow = target->BytesPerRow();

	const color_map* palette = NULL;
	if (space == B_CMAP8)
		palette = system_colors();

	for (int y = 0; y < size; y++) {
		int sy = y * srcHeight / size;
		if (sy >= srcHeight)
			sy = srcHeight - 1;
		const uint8* srcRowBits = srcBits + sy * srcRow;
		uint8* dstRowBits = dstBits + y * dstRow;
		for (int x = 0; x < size; x++) {
			int sx = x * srcWidth / size;
			if (sx >= srcWidth)
				sx = srcWidth - 1;
			rgb_color color;
			if (space == B_CMAP8)
				color = palette->color_list[srcRowBits[sx]];
			else if (space == B_RGB24) {
				const uint8* p = srcRowBits + sx * 3;
				color.red = p[0];
				color.green = p[1];
				color.blue = p[2];
				color.alpha = 255;
			} else {
				const uint8* p = srcRowBits + sx * 4;
				if (space == B_RGBA32) {
					color.red = p[0];
					color.green = p[1];
					color.blue = p[2];
					color.alpha = p[3];
				} else {
					color.red = p[1];
					color.green = p[2];
					color.blue = p[3];
					color.alpha = 255;
				}
			}
			uint8* pixel = dstRowBits + x * 4;
			pixel[0] = color.red;
			pixel[1] = color.green;
			pixel[2] = color.blue;
			pixel[3] = color.alpha;
		}
	}
	return target;
}


void
StampIcon(BNode* node, const char* iconPath)
{
	if (iconPath == NULL || iconPath[0] == '\0')
		return;

	BFile file(iconPath, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BBitmap* decoded = BTranslationUtils::GetBitmap(&file);
	if (decoded == NULL)
		return;

	BBitmap* large = ResizeIcon(decoded, B_LARGE_ICON);
	BBitmap* mini = ResizeIcon(decoded, B_MINI_ICON);
	delete decoded;

	if (large != NULL) {
		BNodeInfo info(node);
		info.SetIcon(large, B_LARGE_ICON);
	}
	if (mini != NULL) {
		BNodeInfo info(node);
		info.SetIcon(mini, B_MINI_ICON);
	}
	delete large;
	delete mini;
}


} // namespace


status_t
DeskbarEntry::EntryDirectory(BPath* into, bool create)
{
	if (into == NULL)
		return B_BAD_VALUE;

	status_t error = find_directory(B_USER_SETTINGS_DIRECTORY, into,
		create);
	if (error != B_OK)
		return error;
	error = into->Append("deskbar");
	if (error != B_OK)
		return error;
	if (create)
		create_directory(into->Path(), 0755);
	error = into->Append("menu_entries");
	if (error != B_OK)
		return error;
	if (create)
		create_directory(into->Path(), 0755);
	return B_OK;
}


status_t
DeskbarEntry::WebAppStorePath(BPath* into)
{
	if (into == NULL)
		return B_BAD_VALUE;

	status_t error = find_directory(B_USER_SETTINGS_DIRECTORY, into,
		true);
	if (error != B_OK)
		return error;
	error = into->Append("NetPlus");
	if (error != B_OK)
		return error;
	create_directory(into->Path(), 0755);
	error = into->Append("webapps");
	if (error != B_OK)
		return error;
	create_directory(into->Path(), 0755);
	return B_OK;
}


status_t
DeskbarEntry::Create(const char* name, const char* targetPath,
	const char* iconPath)
{
	if (name == NULL || name[0] == '\0' || targetPath == NULL
		|| targetPath[0] == '\0')
		return B_BAD_VALUE;

	BPath dir;
	status_t error = EntryDirectory(&dir, true);
	if (error != B_OK)
		return error;

	// Relinking an existing entry: drop the old link first.
	Remove(name);

	BDirectory directory(dir.Path());
	error = directory.InitCheck();
	if (error != B_OK)
		return error;

	BSymLink link;
	error = directory.CreateSymLink(name, targetPath, &link);
	if (error != B_OK)
		return error;

	if (iconPath != NULL && iconPath[0] != '\0') {
		// BNode on a link ref stays on the link itself, so the
		// icon is the entry's, not NetPlus's.
		BEntry entry(&directory, name, false);
		if (entry.InitCheck() == B_OK) {
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK) {
				BNode node(&ref);
				if (node.InitCheck() == B_OK)
					StampIcon(&node, iconPath);
			}
		}
	}

	return B_OK;
}


status_t
DeskbarEntry::Remove(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	BPath dir;
	status_t error = EntryDirectory(&dir, false);
	if (error != B_OK)
		return error;

	BDirectory directory(dir.Path());
	error = directory.InitCheck();
	if (error != B_OK)
		return error;

	BEntry entry(&directory, name, false);
	if (entry.InitCheck() != B_OK || !entry.Exists())
		return B_ENTRY_NOT_FOUND;
	return entry.Remove();
}
