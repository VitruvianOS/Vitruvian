/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PackageManagerApp.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <Screen.h>

#include "MainWindow.h"
#include "PackageManagerDefs.h"


PackageManagerApp::PackageManagerApp()
	:
	Inherited(kPackageManagerSignature),
	fWindow(NULL)
{
}


PackageManagerApp::~PackageManagerApp()
{
}


static const char* const kSettingsFile = "PackageManager_settings";
static const BRect kDefaultFrame(50, 50, 950, 700);


static bool
settings_path(BPath* path)
{
	if (find_directory(B_USER_SETTINGS_DIRECTORY, path) != B_OK)
		return false;

	return path->Append(kSettingsFile) == B_OK;
}


void
PackageManagerApp::ReadyToRun()
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
						&& stored.Width() >= 400 && stored.Height() >= 300) {
					frame = stored;
				}
			}
		}
	}

	fWindow = new MainWindow(frame);
	fWindow->Show();
}


bool
PackageManagerApp::QuitRequested()
{
	BPath path;
	if (fWindow != NULL && settings_path(&path)) {
		BMessage settings;
		settings.AddRect("window frame", fWindow->Frame());
		BFile file(path.Path(), B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
		if (file.InitCheck() == B_OK)
			settings.Flatten(&file);
	}
	return true;
}


int
main()
{
	PackageManagerApp app;
	app.Run();
	return 0;
}
