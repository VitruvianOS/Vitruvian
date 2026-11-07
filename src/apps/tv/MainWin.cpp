/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "MainWin.h"

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Rect.h>
#include <String.h>

#include <string.h>

#include "VideoView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWin"


enum {
	M_QUIT = 1,
	M_SOURCE_FIRST = 0x1000,
	M_SOURCE_LAST  = 0x1fff,
	M_START,
	M_STOP
};


MainWin::MainWin(BRect frame)
	:
	BWindow(frame, "TV", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE),
	fMenuBar(NULL),
	fVideoView(NULL),
	fSourceMenu(NULL)
{
	BRect bounds = Bounds();
	fMenuBar = new BMenuBar(bounds, "menu");
	AddChild(fMenuBar);

	BRect videoFrame = bounds;
	videoFrame.top = fMenuBar->Frame().Height() + 1;
	fVideoView = new VideoView(videoFrame, "video", B_FOLLOW_ALL,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE);
	AddChild(fVideoView);

	fController.SetVideoView(fVideoView);

	_BuildMenu();
	_PopulateSourceMenu();
}


MainWin::~MainWin()
{
}


void
MainWin::_BuildMenu()
{
	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(M_QUIT), 'Q'));
	fMenuBar->AddItem(fileMenu);

	fSourceMenu = new BMenu(B_TRANSLATE("Source"));
	fSourceMenu->AddItem(new BMenuItem(B_TRANSLATE("Start"),
		new BMessage(M_START)));
	fSourceMenu->AddItem(new BMenuItem(B_TRANSLATE("Stop"),
		new BMessage(M_STOP)));
	fMenuBar->AddItem(fSourceMenu);
}


void
MainWin::_PopulateSourceMenu()
{
	if (fSourceMenu == NULL)
		return;

	// Drop previous source entries; keep Start/Stop.
	while (fSourceMenu->CountItems() > 2)
		delete fSourceMenu->RemoveItem(2);

	int32 count = fController.CountSources();
	if (count == 0) {
		BMenuItem* none = new BMenuItem(
			B_TRANSLATE("No capture devices"), NULL);
		none->SetEnabled(false);
		fSourceMenu->AddItem(none);
		return;
	}

	for (int32 i = 0; i < count; i++) {
		const char* name = fController.SourceName(i);
		BMessage* msg = new BMessage(M_SOURCE_FIRST + i);
		BMenuItem* item = new BMenuItem(
			name != NULL ? name : B_TRANSLATE("Unknown"), msg);
		fSourceMenu->AddItem(item);
		if (i == fController.CurrentSource())
			item->SetMarked(true);
	}
}


void
MainWin::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case M_QUIT:
		PostMessage(B_QUIT_REQUESTED);
		break;
	case M_START:
		_StartCapture();
		break;
	case M_STOP:
		_StopCapture();
		break;
	default:
		if (message->what >= M_SOURCE_FIRST
				&& message->what <= M_SOURCE_LAST) {
			fController.SelectSource(message->what - M_SOURCE_FIRST);
			_PopulateSourceMenu();
			break;
		}
		BWindow::MessageReceived(message);
		break;
	}
}


void
MainWin::_StartCapture()
{
	status_t err = fController.StartCapture();
	if (err == B_OK)
		return;

	// Honest seam: media2 has no video capture path to draw from yet.
	BString text("Video capture is not available in media2 yet.\n\n");
	text << "Devices listed from the PipeWire graph can be selected, "
		"but the kit has no path that delivers raw video frames to a view.";
	if (err != B_NOT_SUPPORTED)
		text << "\n\nError: " << strerror(err);

	BAlert* alert = new BAlert("TV", text.String(), B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


void
MainWin::_StopCapture()
{
	fController.StopCapture();
}


bool
MainWin::QuitRequested()
{
	_StopCapture();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
