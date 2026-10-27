/*
 * Copyright 2026, Vitruvian Project.
 * Media preferences: application entry point.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>
#include <Catalog.h>
#include <File.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <Message.h>
#include <Path.h>
#include <Screen.h>

#include <algorithm>

#include "MediaMessages.h"
#include "MediaWindow.h"


static const char* kSignature = kMediaAppSignature;


class Application : public BApplication {
public:
								Application();
	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);

private:
			MediaWindow*		fWindow;
};


Application::Application()
	:
	BApplication(kSignature),
	fWindow(NULL)
{
}


void
Application::ReadyToRun()
{
	BRect rect(100, 100, 880, 580);
	int32 section = 0;

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append(kMediaSettingsFile);
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK) {
			BMessage settings;
			if (settings.Unflatten(&file) == B_OK) {
				BRect saved;
				int32 savedSection = 0;
				if (settings.FindRect("frame", &saved) == B_OK
					&& settings.FindInt32("section", &savedSection) == B_OK) {
					BRect screen = BScreen().Frame();
					if (saved.right > saved.left
						&& saved.bottom > saved.top
						&& saved.Width() >= 200.0f
						&& saved.Height() >= 150.0f
						&& saved.Intersects(screen)) {
						// Pull a partly off-screen frame back on screen.
						saved.OffsetBy(
							std::max(0.0f, screen.left - saved.left)
								+ std::min(0.0f, screen.right - saved.right),
							std::max(0.0f, screen.top - saved.top)
								+ std::min(0.0f, screen.bottom - saved.bottom));
						rect = saved;
						section = savedSection;
					}
				}
			}
		}
	}

	fWindow = new MediaWindow(rect, section);
	fWindow->Show();
}


void
Application::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSelectSection:
			if (fWindow != NULL)
				fWindow->PostMessage(message);
			break;
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


int
main()
{
	Application* app = new Application();
	app->Run();
	delete app;
	return 0;
}
