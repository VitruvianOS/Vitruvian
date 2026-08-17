/*
 * Copyright 2003-2008, Haiku, Inc. (original HWindow logic)
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */

#include "SoundsSectionView.h"

#include <stdio.h>
#include <string.h>

#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <FileGameSound.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MediaFiles.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PathFinder.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringView.h>

#include "../sounds/HEventList.h"
#include "../sounds/SoundFilePanel.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SoundsSectionView"


enum {
	M_PLAY_MESSAGE   = 'MPLM',
	M_STOP_MESSAGE   = 'MSTO',
	M_ITEM_MESSAGE   = 'MITE',
	M_OTHER_MESSAGE  = 'MOTH',
	M_NONE_MESSAGE   = 'MNON'
};

extern const char* kPlayLabel;
extern const char* kStopLabel;


SoundsSectionView::SoundsSectionView()
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fEventList(NULL),
	fFilePanel(NULL),
	fPlayButton(NULL),
	fPlayer(NULL)
{
	GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fEventList = new HEventList();
	fEventList->SetType(BMediaFiles::B_SOUNDS);
	fEventList->SetSelectionMode(B_SINGLE_SELECTION_LIST);

	BMenu* menu = new BMenu("file");
	menu->SetRadioMode(true);
	menu->SetLabelFromMarked(true);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("<none>"),
		new BMessage(M_NONE_MESSAGE)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Other" B_UTF8_ELLIPSIS),
		new BMessage(M_OTHER_MESSAGE)));

	BString label(B_TRANSLATE("Sound file:"));
	BMenuField* menuField = new BMenuField("filemenu", label, menu);
	menuField->SetDivider(menuField->StringWidth(label) + 10);
	menuField->SetEnabled(false);

	BSize buttonsSize(be_plain_font->Size() * 2.5, be_plain_font->Size() * 2.5);

	BButton* stopButton = new BButton("stop", kStopLabel,
		new BMessage(M_STOP_MESSAGE));
	stopButton->SetEnabled(false);
	stopButton->SetExplicitSize(buttonsSize);

	fPlayButton = new BButton("play", kPlayLabel, new BMessage(M_PLAY_MESSAGE));
	fPlayButton->SetEnabled(false);
	fPlayButton->SetExplicitSize(buttonsSize);



	BLayoutBuilder::Group<>(GroupLayout())
		.Add(fEventList)
		.AddGroup(B_HORIZONTAL)
			.Add(menuField)
			.AddGroup(B_HORIZONTAL, 0)
				.Add(fPlayButton)
				.Add(stopButton)
			.End()
		.End();




	SetFlags(Flags() | B_PULSE_NEEDED);


	BPathFinder pathFinder;
	BStringList paths;
	pathFinder.FindPaths(B_FIND_PATH_SOUNDS_DIRECTORY, paths);
	for (int i = 0; i < paths.CountStrings(); ++i) {
		BEntry entry(paths.StringAt(i));
		if (entry.Exists()) {
			entry.GetRef(&fPathRef);
			break;
		}
	}

	fFilePanel = new SoundFilePanel(this);
	BEntry entry(&fPathRef);
	if (entry.Exists())
		fFilePanel->SetPanelDirectory(&fPathRef);
}


SoundsSectionView::~SoundsSectionView()
{
	delete fFilePanel;
	delete fPlayer;
}


void
SoundsSectionView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fFilePanel->SetTarget(this);
	_SetupMenuField();

	BMenuField* menufield = dynamic_cast<BMenuField*>(FindView("filemenu"));
	if (menufield != NULL) {
		BMenuItem* noneItem = menufield->Menu()->FindItem(B_TRANSLATE("<none>"));
		if (noneItem != NULL)
			noneItem->SetMarked(true);
	}
}


void
SoundsSectionView::Pulse()
{
	BButton* stop = dynamic_cast<BButton*>(FindView("stop"));
	if (stop == NULL)
		return;
	stop->SetEnabled(fPlayer != NULL && fPlayer->IsPlaying());
}


void
SoundsSectionView::_SetupMenuField()
{
	BMenuField* menufield = dynamic_cast<BMenuField*>(FindView("filemenu"));
	if (menufield == NULL)
		return;
	BMenu* menu = menufield->Menu();
	int32 count = fEventList->CountRows();
	for (int32 i = 0; i < count; i++) {
		HEventRow* row = (HEventRow*)fEventList->RowAt(i);
		if (row == NULL)
			continue;

		BPath path(row->Path());
		if (path.InitCheck() != B_OK)
			continue;
		if (menu->FindItem(path.Leaf()))
			continue;

		BMessage* msg = new BMessage(M_ITEM_MESSAGE);
		entry_ref ref;
		::get_ref_for_path(path.Path(), &ref);
		msg->AddRef("refs", &ref);
		menu->AddItem(new BMenuItem(path.Leaf(), msg), 0);
	}

	directory_which whichDirectories[] = {
		B_SYSTEM_SOUNDS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_SOUNDS_DIRECTORY,
		B_USER_SOUNDS_DIRECTORY,
		B_USER_NONPACKAGED_SOUNDS_DIRECTORY,
	};

	for (size_t i = 0;
			i < sizeof(whichDirectories) / sizeof(whichDirectories[0]); i++) {
		BPath path;
		BDirectory dir;
		BEntry entry;
		BPath itemPath;

		status_t err = find_directory(whichDirectories[i], &path);
		if (err == B_OK)
			err = dir.SetTo(path.Path());
		while (err == B_OK) {
			err = dir.GetNextEntry(&entry, true);
			if (entry.InitCheck() != B_NO_ERROR)
				break;
			if (entry.IsDirectory())
				continue;

			entry.GetPath(&itemPath);
			if (menu->FindItem(itemPath.Leaf()))
				continue;

			BMessage* msg = new BMessage(M_ITEM_MESSAGE);
			entry_ref ref;
			::get_ref_for_path(itemPath.Path(), &ref);
			msg->AddRef("refs", &ref);
			menu->AddItem(new BMenuItem(itemPath.Leaf(), msg), 0);
		}
	}
}


void
SoundsSectionView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case M_OTHER_MESSAGE:
		{
			BMenuField* menufield = dynamic_cast<BMenuField*>(FindView("filemenu"));
			if (menufield == NULL)
				return;
			BMenu* menu = menufield->Menu();

			HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
			if (row != NULL) {
				BPath path(row->Path());
				BMenuItem* item = (path.InitCheck() != B_OK)
					? menu->FindItem(B_TRANSLATE("<none>"))
					: menu->FindItem(path.Leaf());
				if (item != NULL)
					item->SetMarked(true);
			}
			fFilePanel->Show();
			break;
		}

		case B_CANCEL:
			fFilePanel->SetPanelDirectory(&fPathRef);
			break;

		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
			if (message->FindRef("refs", &ref) == B_OK && row != NULL) {
				BMenuField* menufield = dynamic_cast<BMenuField*>(FindView("filemenu"));
				if (menufield == NULL)
					return;
				BMenu* menu = menufield->Menu();

				BMessage* msg = new BMessage(M_ITEM_MESSAGE);
				BPath path(&ref);
				msg->AddRef("refs", &ref);
				BMenuItem* menuitem = menu->FindItem(path.Leaf());
				if (menuitem == NULL)
					menu->AddItem(menuitem = new BMenuItem(path.Leaf(), msg), 0);
				fEventList->SetPath(BPath(&ref).Path());
				if (menuitem != NULL)
					menuitem->SetMarked(true);

				path.GetParent(&path);
				get_ref_for_path(path.Path(), &fPathRef);
				fFilePanel->SetPanelDirectory(&fPathRef);
				fPlayButton->SetEnabled(true);
			}
			break;
		}

		case M_PLAY_MESSAGE:
		{
			HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
			if (row != NULL) {
				const char* path = row->Path();
				if (path != NULL) {
					entry_ref ref;
					::get_ref_for_path(path, &ref);
					delete fPlayer;
					fPlayer = new BFileGameSound(&ref, false);
					fPlayer->StartPlaying();
				}
			}
			break;
		}

		case M_STOP_MESSAGE:
			if (fPlayer != NULL && fPlayer->IsPlaying()) {
				fPlayer->StopPlaying();
				delete fPlayer;
				fPlayer = NULL;
			}
			break;

		case M_EVENT_CHANGED:
		{
			BMenuField* menufield = dynamic_cast<BMenuField*>(FindView("filemenu"));
			if (menufield == NULL)
				return;
			menufield->SetEnabled(true);

			const char* filePath;
			if (message->FindString("path", &filePath) == B_OK) {
				BMenu* menu = menufield->Menu();
				BPath path(filePath);
				BMenuItem* item = (path.InitCheck() != B_OK)
					? menu->FindItem(B_TRANSLATE("<none>"))
					: menu->FindItem(path.Leaf());
				if (item != NULL)
					item->SetMarked(true);

				HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
				if (row != NULL) {
					const char* rowPath = row->Path();
					fPlayButton->SetEnabled(rowPath != NULL
						&& strcmp(rowPath, "") != 0);
				} else {
					menufield->SetEnabled(false);
					fPlayButton->SetEnabled(false);
				}
			}
			break;
		}

		case M_ITEM_MESSAGE:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				fEventList->SetPath(BPath(&ref).Path());
				HEventRow* row = (HEventRow*)fEventList->CurrentSelection();
				fPlayButton->SetEnabled(row != NULL && row->Path() != NULL);
			}
			break;
		}

		case M_NONE_MESSAGE:
			fPlayButton->SetEnabled(false);
			fEventList->SetPath(NULL);
			break;

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}
