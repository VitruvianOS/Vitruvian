/*
 * Copyright 2026, the Vitruvian project.
 * Distributed under the terms of the MIT License.
 */

#include "ExpandVolumeApp.h"

#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ExpandVolumeWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ExpandVolume"


static const char* kAppSignature
	= "application/x-vnd.vos-ExpandVolume";

static const char* kDoneMarkerName = "expandvolume-done";


const char*
ExpandVolumeApp::Signature()
{
	return kAppSignature;
}


static BPath
_DoneMarkerPath()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return BPath();
	if (path.Append(kDoneMarkerName) != B_OK)
		return BPath();
	return path;
}


bool
ExpandVolumeDoneMarkerExists()
{
	BPath path = _DoneMarkerPath();
	if (path.Path() == NULL)
		return false;
	struct stat st;
	return stat(path.Path(), &st) == 0;
}


status_t
WriteExpandVolumeDoneMarker()
{
	BPath path = _DoneMarkerPath();
	if (path.Path() == NULL)
		return B_ERROR;

	BFile file(path.Path(), B_CREATE_FILE | B_WRITE_ONLY | B_TRUNCATE_END);
	status_t status = file.InitCheck();
	if (status == B_OK)
		file.SetPermissions(0644);
	return status;
}


// #pragma mark -


ExpandVolumeApp::ExpandVolumeApp()
	:
	BApplication(kAppSignature),
	fOnlyIfExpandable(false)
{
}


void
ExpandVolumeApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--if-expandable") == 0)
			fOnlyIfExpandable = true;
	}
}


void
ExpandVolumeApp::ReadyToRun()
{
	new ExpandVolumeWindow(fOnlyIfExpandable);
}


void
ExpandVolumeApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_SHOW_WINDOW:
		case B_ARGV_RECEIVED:
		case B_REFS_RECEIVED:
		{
			// A second launch (launcher script, Deskbar, terminal)
			// focuses the popup instead of stacking a second one.
			for (int32 i = 0; i < be_app->CountWindows(); i++) {
				BWindow* window = be_app->WindowAt(i);
				if (window != NULL)
					window->PostMessage(new BMessage(MSG_SHOW_WINDOW));
			}
			break;
		}

		default:
			BApplication::MessageReceived(message);
	}
}


bool
ExpandVolumeApp::QuitRequested()
{
	return true;
}


int
main()
{
	ExpandVolumeApp app;
	app.Run();
	return 0;
}
