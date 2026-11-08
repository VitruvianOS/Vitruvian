/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// setapptype sig [-d desc] [-l launcher] [-t type,...] [-i16 raw] [-i32 raw]
// setapptype -r sig
// Writes an application type straight into the system MIME database, so it
// works without the registrar. -l makes the launcher the type's app file
// (signature, ARGV_ONLY flags, app hint). Icons are raw BGRA pixel files.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <Bitmap.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <AppFileInfo.h>
#include <Path.h>
#include <Roster.h>


static status_t
set_icon(BNode& node, const char* file, int size)
{
	FILE* in = fopen(file, "r");
	if (in == NULL)
		return B_ENTRY_NOT_FOUND;
	BRect bounds(0, 0, size - 1, size - 1);
	BBitmap rgba(bounds, B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	size_t length = size * size * 4;
	size_t got = fread(rgba.Bits(), 1, length, in);
	fclose(in);
	if (got != length || (size_t)rgba.BitsLength() != length)
		return B_ERROR;
	BBitmap cmap(bounds, B_BITMAP_NO_SERVER_LINK, B_CMAP8);
	if (cmap.ImportBits(&rgba) != B_OK)
		return B_ERROR;
	ssize_t written = node.WriteAttr(size == 16 ? "META:M:STD_ICON"
			: "META:L:STD_ICON",
		size == 16 ? B_MINI_ICON_TYPE : B_LARGE_ICON_TYPE, 0, cmap.Bits(),
		cmap.BitsLength());
	return written == cmap.BitsLength() ? B_OK : B_ERROR;
}


static void
write_string(BNode& node, const char* attr, type_code type, const char* value)
{
	node.WriteAttr(attr, type, 0, value, strlen(value) + 1);
}


int
main(int argc, char** argv)
{
	bool remove = argc == 3 && strcmp(argv[1], "-r") == 0;
	if (argc < 2) {
		fprintf(stderr, "usage: setapptype sig [-d desc] [-l launcher] "
			"[-t type,...] [-i16 raw] [-i32 raw] | -r sig\n");
		return 1;
	}
	const char* sig = argv[remove ? 2 : 1];

	BPath dbPath;
	if (find_directory(B_SYSTEM_DATA_DIRECTORY, &dbPath) != B_OK)
		return 1;
	dbPath.Append("mime_db");
	char name[B_MIME_TYPE_LENGTH];
	strlcpy(name, sig, sizeof(name));
	for (char* c = name; *c != '\0'; c++)
		*c = tolower((unsigned char)*c);
	char* slash = strchr(name, '/');
	if (slash == NULL || slash == name || slash[1] == '\0')
		return 1;

	BPath typePath(dbPath.Path(), name);
	if (remove)
		return BEntry(typePath.Path()).Remove() == B_OK ? 0 : 1;

	*slash = '\0';
	BPath superPath(dbPath.Path(), name);
	mkdir(dbPath.Path(), 0755);
	mkdir(superPath.Path(), 0755);
	BFile file(typePath.Path(), B_READ_WRITE | B_CREATE_FILE);
	if (file.InitCheck() != B_OK) {
		fprintf(stderr, "setapptype: cannot write %s\n", typePath.Path());
		return 1;
	}
	write_string(file, "META:TYPE", B_STRING_TYPE, sig);

	BMessage supported;
	bool haveSupported = false;
	const char* launcherPath = NULL;
	for (int i = 2; i + 1 < argc; i += 2) {
		const char* arg = argv[i];
		const char* value = argv[i + 1];
		if (strcmp(arg, "-d") == 0) {
			write_string(file, "META:S:DESC", 'MSDC', value);
		} else if (strcmp(arg, "-l") == 0) {
			launcherPath = value;
			BNode launcher(value);
			if (launcher.InitCheck() != B_OK)
				return 1;
			write_string(launcher, "BEOS:APP_SIG", B_MIME_STRING_TYPE, sig);
			uint32 flags = B_SINGLE_LAUNCH | B_ARGV_ONLY;
			launcher.WriteAttr("BEOS:APP_FLAGS", 'APPF', 0, &flags,
				sizeof(flags));
			write_string(file, "META:PPATH", 'MPTH', value);
		} else if (strcmp(arg, "-t") == 0) {
			BMessage& types = supported;
			haveSupported = true;
			char* list = strdup(value);
			char* save = NULL;
			for (char* t = strtok_r(list, ",", &save); t != NULL;
					t = strtok_r(NULL, ",", &save)) {
				types.AddString("types", t);
			}
			free(list);
			ssize_t size = types.FlattenedSize();
			char* buffer = (char*)malloc(size);
			if (buffer != NULL && types.Flatten(buffer, size) == B_OK) {
				file.WriteAttr("META:FILE_TYPES", B_MESSAGE_TYPE, 0, buffer,
					size);
			}
			free(buffer);
		} else if (strcmp(arg, "-i16") == 0) {
			set_icon(file, value, 16);
		} else if (strcmp(arg, "-i32") == 0) {
			set_icon(file, value, 32);
		}
	}

	// The registrar keeps its own table of supporting apps, built at start.
	if (haveSupported && launcherPath != NULL) {
		BFile app(launcherPath, B_READ_WRITE);
		BAppFileInfo info(&app);
		if (info.InitCheck() == B_OK)
			info.SetSupportedTypes(&supported, true);
	}
	return 0;
}
