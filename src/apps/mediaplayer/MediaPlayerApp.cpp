/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MediaPlayerApp.h"

#include <Catalog.h>
#include <Entry.h>
#include <Message.h>
#include <Rect.h>
#include <String.h>

#include "MainWin.h"
#include "MediaMessages.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MediaPlayer"


const char* kMediaPlayerSignature = "application/x-vnd.Haiku-MediaPlayer";


MediaPlayerApp::MediaPlayerApp()
	:
	BApplication(kMediaPlayerSignature),
	fMainWin(NULL)
{
	B_TRANSLATE_MARK_SYSTEM_NAME_VOID("MediaPlayer");
}


MediaPlayerApp::~MediaPlayerApp()
{
}


void
MediaPlayerApp::ReadyToRun()
{
	if (fMainWin == NULL)
		fMainWin = new MainWin(BRect(40, 40, 400, 340));
	if (fMainWin->IsHidden())
		fMainWin->Show();
}


void
MediaPlayerApp::ArgvReceived(int32 argc, char** argv)
{
	BMessage refs(B_REFS_RECEIVED);
	bool hasRefs = false;
	for (int32 i = 1; i < argc; i++) {
		BEntry entry(argv[i]);
		if (entry.InitCheck() != B_OK)
			continue;
		entry_ref ref;
		if (entry.GetRef(&ref) == B_OK) {
			refs.AddRef("refs", &ref);
			hasRefs = true;
		}
	}
	if (hasRefs) {
		ReadyToRun();
		fMainWin->PostMessage(&refs);
	}
}


void
MediaPlayerApp::RefsReceived(BMessage* message)
{
	ReadyToRun();
	fMainWin->PostMessage(message);
}


void
MediaPlayerApp::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_OPEN:
		case MSG_PLAY_CD:
		case MSG_PLAYLIST_SHOW:
			ReadyToRun();
			fMainWin->PostMessage(message);
			break;

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


int
main()
{
	MediaPlayerApp app;
	app.Run();
	return 0;
}
