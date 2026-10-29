/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LocalSendApp.h"

#include <Alert.h>
#include <Deskbar.h>
#include <Entry.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LocalSendManager.h"
#include "LocalSendMessages.h"
#include "LocalSendSettings.h"
#include "LocalSendWindow.h"


const char* kSignature = "application/x-vnd.vos-LocalSend";
const char* kDeskbarSignature = "application/x-vnd.Be-TSKB";
const char* kDeskbarItemName = "LocalSend";


//	#pragma mark - LocalSendReplicant


LocalSendReplicant::LocalSendReplicant(BRect frame)
	:
	BView(frame, kDeskbarItemName, B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_PULSE_NEEDED)
{
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);
}


status_t
LocalSendReplicant::Archive(BMessage* into, bool deep) const
{
	BView::Archive(into, deep);
	into->AddString("class", "LocalSendReplicant");
	into->AddString("add_on", kSignature);
	return B_OK;
}


/* static */ BArchivable*
LocalSendReplicant::Instantiate(BMessage* into)
{
	if (!validate_instantiation(into, "LocalSendReplicant"))
		return NULL;
	return new LocalSendReplicant(BRect(0, 0, 15, 15));
}


void
LocalSendReplicant::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);
}


void
LocalSendReplicant::Draw(BRect updateRect)
{
	// A small send/receive glyph: two opposed chevrons.
	BRect bounds = Bounds();
	FillRect(bounds, B_SOLID_LOW);

	SetHighColor(0, 100, 180);
	SetPenSize(2);
	float midY = bounds.top + bounds.Height() / 2;
	float midX = bounds.left + bounds.Width() / 2;

	// Outgoing arrow (right).
	StrokeLine(BPoint(midX - 5, midY), BPoint(midX + 4, midY));
	StrokeLine(BPoint(midX + 1, midY - 3), BPoint(midX + 4, midY));
	StrokeLine(BPoint(midX + 1, midY + 3), BPoint(midX + 4, midY));
}


void
LocalSendReplicant::MouseDown(BPoint /*where*/)
{
	// Single click toggles the main window in the running app.
	_ToggleAppWindow();
}


void
LocalSendReplicant::_ToggleAppWindow()
{
	BMessenger messenger(kSignature);
	if (messenger.IsValid()) {
		BMessage message(kMsgToggleWindow);
		messenger.SendMessage(&message);
		return;
	}

	// App not running: launch it.
	be_roster->Launch(kSignature);
}


//	#pragma mark - deskbar entry points


extern "C" BView*
instantiate_deskbar_item(float /*maxWidth*/, float maxHeight)
{
	// Square, driven by height, like NetworkStatus's tray item.
	return new LocalSendReplicant(BRect(0, 0, maxHeight - 1, maxHeight - 1));
}


extern "C" status_t
instantiate_deskbar_item_for_width(float* width, float* height)
{
	*width = 16;
	*height = 16;
	return B_OK;
}


//	#pragma mark - LocalSendApp


LocalSendApp::LocalSendApp()
	:
	BApplication(kSignature),
	fQuitImmediately(false)
{
	fSettings = new LocalSendSettings();
	fSettings->Load();

	fManager = new LocalSendManager(fSettings);
	fWindow = new LocalSendWindow(BRect(80, 80, 640, 520), fManager,
		fSettings);
	fManager->SetWindowMessenger(BMessenger(fWindow));
}


LocalSendApp::~LocalSendApp()
{
	delete fManager;
	delete fSettings;
}


void
LocalSendApp::ArgvReceived(int32 argc, char** argv)
{
	if (argc <= 1)
		return;

	if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
		puts("LocalSend for V\\OS\n"
			"--help\t\tprint this info and exit\n"
			"--deskbar\tadd the tray icon to the Deskbar and exit");
		fQuitImmediately = true;
		return;
	}

	if (strcmp(argv[1], "--deskbar") == 0) {
		BDeskbar deskbar;
		if (deskbar.IsRunning()
			&& !deskbar.HasItem(kDeskbarItemName)) {
			LocalSendReplicant* replicant
				= new LocalSendReplicant(BRect(0, 0, 15, 15));
			deskbar.AddItem(replicant);
			delete replicant;
		}
		fQuitImmediately = true;
	}
}


void
LocalSendApp::ReadyToRun()
{
	if (fQuitImmediately) {
		Quit();
		return;
	}

	if (fManager->Start() != B_OK) {
		BAlert* alert = new BAlert("Error",
			"LocalSend could not start its network services.\n"
			"Another application may be using the LocalSend port.",
			"Quit", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	fWindow->Show();

	// Auto-install the tray icon once; harmless if the Deskbar is
	// not running yet.
	BDeskbar deskbar;
	if (deskbar.IsRunning() && !deskbar.HasItem(kDeskbarItemName)) {
		LocalSendReplicant* replicant
			= new LocalSendReplicant(BRect(0, 0, 15, 15));
		deskbar.AddItem(replicant);
		delete replicant;
	}
}


void
LocalSendApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleWindow:
			_ToggleWindow();
			break;

		// Prompt answers forwarded by ReceivePromptWindow land
		// here when sent to the app; route them to the window.
		case 'pacc':
		case 'pdec':
		case 'palw':
			if (fWindow != NULL)
				fWindow->PostMessage(message);
			break;

		// Manager messages target the window directly; nothing to
		// do here beyond the default.
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
LocalSendApp::RefsReceived(BMessage* message)
{
	_OpenFilesFromRefs(message);
}


bool
LocalSendApp::QuitRequested()
{
	// Stop networking before the objects go away in ~LocalSendApp.
	if (fManager != NULL)
		fManager->Stop();
	return true;
}


void
LocalSendApp::_ToggleWindow()
{
	if (fWindow == NULL)
		return;

	if (fWindow->IsHidden()) {
		fWindow->Show();
		fWindow->Activate();
	} else {
		fWindow->Hide();
	}
}


void
LocalSendApp::_OpenFilesFromRefs(BMessage* message)
{
	// Files dropped on the app icon: reveal the window and stash
	// the refs for the next send.
	if (fWindow == NULL)
		return;

	if (fWindow->IsHidden())
		fWindow->Show();
	fWindow->Activate();
	fWindow->PostMessage(message);
}


int
main()
{
	LocalSendApp app;
	app.Run();
	return 0;
}
