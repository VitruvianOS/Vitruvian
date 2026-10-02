/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ServicesApp.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <Screen.h>

#include "ServicesWindow.h"


static const char* const kSettingsFile = "Services_settings";
static const BRect kDefaultFrame(80, 80, 920, 680);


static bool
settings_path(BPath* path)
{
	if (find_directory(B_USER_SETTINGS_DIRECTORY, path) != B_OK)
		return false;

	return path->Append(kSettingsFile) == B_OK;
}


ServicesApp::ServicesApp()
	:
	Inherited(kServicesSignature),
	fWindow(NULL)
{
}


void
ServicesApp::ReadyToRun()
{
	BRect frame = kDefaultFrame;

	BPath path;
	if (settings_path(&path)) {
		BFile file(path.Path(), B_READ_ONLY);
		BMessage settings;
		if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK) {
			BRect stored;
			if (settings.FindRect("window frame", &stored) == B_OK) {
				BScreen screen;
				if (screen.Frame().Contains(stored) && stored.IsValid()
						&& stored.Width() >= 500
						&& stored.Height() >= 360) {
					frame = stored;
				}
			}
		}
	}

	fWindow = new ServicesWindow(frame);
	fWindow->Show();
}


bool
ServicesApp::QuitRequested()
{
	if (fWindow != NULL) {
		BPath path;
		if (settings_path(&path)) {
			BMessage settings;
			settings.AddRect("window frame", fWindow->Frame());
			BFile file(path.Path(),
				B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
			if (file.InitCheck() == B_OK)
				settings.Flatten(&file);
		}
	}
	return Inherited::QuitRequested();
}
