/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "MainWindow.h"

#include <Application.h>
#include <String.h>

#include <cstring>


class NetPlusApp : public BApplication {
public:
								NetPlusApp();
	virtual						~NetPlusApp();

	virtual void				ArgvReceived(int32 argc, char** argv);
	virtual void				ReadyToRun();

private:
			BString				fStartupURL;
};


NetPlusApp::NetPlusApp()
	:
	BApplication("application/x-vnd.Vitruvian-NetPlus")
{
}


NetPlusApp::~NetPlusApp()
{
}


void
NetPlusApp::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		if (fStartupURL.IsEmpty() && argv[i][0] != '-')
			fStartupURL = argv[i];
	}
}


void
NetPlusApp::ReadyToRun()
{
	BRect frame(100, 100, 1100, 800);
	MainWindow* win = new MainWindow(frame, fStartupURL.String());
	win->Show();
}


int
main(int /*argc*/, char** /*argv*/)
{
	NetPlusApp app;
	app.Run();
	return 0;
}
