/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
trademarks of Be Incorporated in the United States and other countries. Other
brand product names are registered trademarks or trademarks of their respective
holders.
All rights reserved.
*/


#include "DeskbarUtils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AppFileInfo.h>
#include <Alert.h>
#include <Catalog.h>
#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <List.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>
#include <String.h>
#include <SymLink.h>
#include <Window.h>

#include <libroot/libroot_private.h>

#include "BarMenuBar.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BarApp"


const char* const kDeskbarMenuEntriesFileName = "menu_entries";

static const char* const kDeskbarDirectoryName = "deskbar";


void
AddRefsToDeskbarMenu(const BMessage* m, entry_ref* subdirectory)
{
	if (m) {
		int32 count = 0;
		uint32 type = 0;
		entry_ref ref;

		m->GetInfo("refs", &type, &count);
		if (count <= 0)
			return;

		BPath path;
		BSymLink link;
		BDirectory dir;
		if (subdirectory) {
			ref = *subdirectory;
			BEntry entry(&ref);
			if (entry.Exists()) {
				// if the ref is a file get the parent and convert it to a ref
				if (entry.IsFile()) {
					BEntry parent;
					entry.GetParent(&parent);
					parent.GetRef(&ref);
				}
			} else
				return;

			dir.SetTo(&ref);
		} else {
			if (find_directory(B_USER_DESKBAR_DIRECTORY, &path) == B_OK)
				dir.SetTo(path.Path());
			else
				return;
		}

		for (long i = 0; i < count; i++) {
			if (m->FindRef("refs", i, &ref) == B_NO_ERROR) {
				BEntry entry(&ref);
				entry.GetPath(&path);

				dir.CreateSymLink(ref.name, path.Path(), &link);
			}
		}
	}
}


status_t
GetDeskbarSettingsDirectory(BPath& _path, bool create)
{
	status_t error = find_directory(B_USER_SETTINGS_DIRECTORY, &_path, create);
	if (error != B_OK)
		return error;

	error = _path.Append(kDeskbarDirectoryName);
	if (error != B_OK)
		return error;

	return create ? create_directory(_path.Path(), 0755) : error;
}


status_t
GetDeskbarDataDirectory(BPath& _path)
{
	status_t error = find_directory(B_SYSTEM_DATA_DIRECTORY, &_path, false);
	if (error != B_OK)
		return error;

	return _path.Append(kDeskbarDirectoryName);
}


void
ShowLaunchAlert(const char* signature, status_t error)
{
	if (error == B_OK || error == B_ALREADY_RUNNING)
		return;

	// prefer the app's file name over its signature in the alert
	const char* name = signature;
	BPath path;
	app_info info;
	if (signature != NULL
		&& be_roster->GetAppInfo(signature, &info) == B_OK) {
		name = info.ref.name;
		BEntry entry(&info.ref, true);
		entry.GetPath(&path);
	}

	BString text;
	if (error == B_MISSING_LIBRARY && path.Path() != NULL) {
		char loaderError[256];
		if (__loader_check_binary(path.Path(), loaderError,
				sizeof(loaderError)) == B_MISSING_LIBRARY
			&& loaderError[0] != '\0') {
			text = B_TRANSLATE("Could not launch \"%name\" "
				"(Missing library: %library).");
			text.ReplaceFirst("%library", loaderError);
		}
	}

	if (text.Length() == 0) {
		text = B_TRANSLATE("Could not launch \"%name\" (%error).");
		text.ReplaceFirst("%error", strerror(error));
	}

	if (name == NULL)
		name = B_TRANSLATE("<unknown>");
	text.ReplaceFirst("%name", name);

	BAlert* alert = new BAlert("", text.String(), B_TRANSLATE("OK"), NULL,
		NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}
