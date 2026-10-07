/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PlaylistWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <ListView.h>
#include <Locale.h>
#include <Message.h>
#include <ScrollView.h>
#include <String.h>

#include "Controller.h"
#include "MediaMessages.h"
#include "Playlist.h"
#include "PlaylistItem.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaylistWindow"


PlaylistWindow::PlaylistWindow(BRect frame, Controller* controller,
		Playlist* playlist)
	:
	BWindow(frame, B_TRANSLATE("Playlist"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fController(controller),
	fPlaylist(playlist),
	fListView(NULL),
	fAddButton(NULL),
	fRemoveButton(NULL),
	fClearButton(NULL)
{
	BRect bounds = Bounds();

	BView* top = new BView(bounds, "top", B_FOLLOW_ALL, 0);
	top->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(top);

	fListView = new BListView(BRect(8, 8, bounds.Width() - 8,
		bounds.Height() - 48), "playlist", B_SINGLE_SELECTION_LIST,
		B_FOLLOW_ALL);
	fListView->SetInvocationMessage(new BMessage(MSG_PLAYLIST_ITEM_INVOKED));
	BScrollView* scroller = new BScrollView("scroller", fListView,
		B_FOLLOW_ALL, 0, false, true);
	top->AddChild(scroller);

	fAddButton = new BButton(BRect(8, bounds.Height() - 40, 100,
		bounds.Height() - 8), "add", B_TRANSLATE("Add"), 
		new BMessage(MSG_PLAYLIST_ADD));
	fRemoveButton = new BButton(BRect(108, bounds.Height() - 40, 200,
		bounds.Height() - 8), "remove", B_TRANSLATE("Remove"),
		new BMessage('rmov'));
	fClearButton = new BButton(BRect(208, bounds.Height() - 40, 300,
		bounds.Height() - 8), "clear", B_TRANSLATE("Clear"),
		new BMessage(MSG_PLAYLIST_CLEAR));
	top->AddChild(fAddButton);
	top->AddChild(fRemoveButton);
	top->AddChild(fClearButton);

	_UpdateList();
}


PlaylistWindow::~PlaylistWindow()
{
}


void
PlaylistWindow::_UpdateList()
{
	if (fListView == NULL || fPlaylist == NULL)
		return;

	fListView->MakeEmpty();
	int32 current = fPlaylist->CurrentItemIndex();
	for (int32 i = 0; i < fPlaylist->CountItems(); i++) {
		PlaylistItem* item = fPlaylist->ItemAt(i);
		if (item == NULL)
			continue;
		BString line;
		line << (i + 1) << ". " << item->Name();
		fListView->AddItem(new BStringItem(line.String()));
		if (i == current)
			fListView->Select(i);
	}
}


void
PlaylistWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_PLAYLIST_ITEMS_CHANGED:
		case MSG_PLAYLIST_CURRENT_CHANGED:
			_UpdateList();
			break;

		case MSG_PLAYLIST_ADD:
			// Handled by the main window's file panel.
			break;

		case 'rmov':
		{
			int32 index = fListView != NULL ? fListView->CurrentSelection() : -1;
			if (index >= 0 && fPlaylist != NULL)
				fPlaylist->RemoveItem(index);
			break;
		}

		case MSG_PLAYLIST_CLEAR:
			if (fPlaylist != NULL)
				fPlaylist->Clear();
			break;

		case MSG_PLAYLIST_ITEM_INVOKED:
		{
			int32 index = fListView != NULL ? fListView->CurrentSelection() : -1;
			if (index >= 0 && fPlaylist != NULL)
				fPlaylist->SetCurrentItemIndex(index, true);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
PlaylistWindow::QuitRequested()
{
	Hide();
	return false;
}
