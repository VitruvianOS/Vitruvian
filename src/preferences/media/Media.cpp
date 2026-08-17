/*
 * Copyright 2026, Vitruvian Project.
 * Media preferences — application entry point.
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

#include <stdio.h>

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
			char buffer[256];
			ssize_t size = file.Read(buffer, sizeof(buffer) - 1);
			if (size > 0) {
				buffer[size] = '\0';
				int32 l, t, r, b, s;
				if (sscanf(buffer, "rect = %d,%d,%d,%d\nsection = %d",
						&l, &t, &r, &b, &s) == 5) {
					BRect saved(l, t, r, b);



					if (r > l && b > t && saved.Width() >= 200.0f
						&& saved.Height() >= 150.0f
						&& saved.Intersects(BScreen().Frame())) {
						rect = saved;
						section = s;
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
