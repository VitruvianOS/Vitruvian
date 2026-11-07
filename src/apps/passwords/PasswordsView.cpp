/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PasswordsView.h"

#include "EntryWindow.h"
#include "KdbxFile.h"
#include "PasswordDialog.h"
#include "VosCrypto.h"

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>
#include <time.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PasswordsView"


static const uint32 kMsgGroupSelected		= 'GrpS';
static const uint32 kMsgEntrySelected		= 'EntS';
static const uint32 kMsgSearchChanged		= 'Srch';
static const uint32 kMsgNewEntry			= 'NwEn';
static const uint32 kMsgOpenEntry			= 'OpEn';
static const uint32 kMsgDeleteEntry			= 'DelE';
static const uint32 kMsgCopyPassword		= 'CpyP';
static const uint32 kMsgCopyUser			= 'CpyU';
static const uint32 kMsgLockKeyring			= 'Lock';
static const uint32 kMsgExport				= 'ExpK';
static const uint32 kMsgImport				= 'ImpK';
static const uint32 kMsgClearClipboard		= 'ClrC';
static const uint32 kMsgFilePanelSave		= 'FpSv';
static const uint32 kMsgFilePanelOpen		= 'FpOp';

static const int32 kClipboardSeconds = 12;

static const char* kTotpSuffix = "/totp";


class EntryItem {
public:
								EntryItem(const BString& keyring,
									const BString& title,
									const BString& username,
									const BString& password,
									const BString& totp, const BString& url,
									const BString& notes)
									:
									fKeyring(keyring),
									fTitle(title),
									fUsername(username),
									fPassword(password),
									fTotp(totp),
									fUrl(url),
									fNotes(notes)
								{
								}

			const BString&		Keyring() const { return fKeyring; }
			const BString&		Title() const { return fTitle; }
			const BString&		Username() const { return fUsername; }
			const BString&		Password() const { return fPassword; }
			const BString&		Totp() const { return fTotp; }
			const BString&		Url() const { return fUrl; }
			const BString&		Notes() const { return fNotes; }

private:
			BString				fKeyring;
			BString				fTitle;
			BString				fUsername;
			BString				fPassword;
			BString				fTotp;
			BString				fUrl;
			BString				fNotes;
};


class OpenEntry {
public:
								OpenEntry(EntryWindow* window,
									const BString& id)
									:
									fWindow(window),
									fMessenger(window),
									fId(id)
								{
								}

			EntryWindow*		Window() const { return fWindow; }
			const BMessenger&	Messenger() const { return fMessenger; }
			const BString&		Id() const { return fId; }
			void				SetId(const BString& id) { fId = id; }

private:
			EntryWindow*		fWindow;
			BMessenger			fMessenger;
			BString				fId;
};


static BString
_EntryId(const BString& keyring, const BString& title, const BString& username)
{
	BString id = keyring;
	id << '\n' << title << '\n' << username;
	return id;
}


namespace {


class EmptyHintView : public BView {
public:
	EmptyHintView(const char* text)
		:
		BView("hint", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fText(text)
	{
		SetViewUIColor(B_LIST_BACKGROUND_COLOR);
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	}

	virtual void Draw(BRect updateRect)
	{
		BRect bounds = Bounds();
		font_height height;
		GetFontHeight(&height);
		SetHighUIColor(B_LIST_ITEM_TEXT_COLOR, B_LIGHTEN_1_TINT);
		DrawString(fText.String(), BPoint(
			(bounds.Width() - StringWidth(fText.String())) / 2,
			(bounds.Height() + height.ascent - height.descent) / 2));
	}

private:
	BString fText;
};


} // namespace


PasswordsView::PasswordsView()
	:
	BGroupView(B_VERTICAL, 0.0f),
	fClipboardRunner(NULL),
	fClipboardMessageWhat(0)
{
	SetName(B_TRANSLATE("Passwords"));

	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* fileMenu = new BMenu(B_TRANSLATE("File"));
	fNewItem = new BMenuItem(B_TRANSLATE("New entry"),
		new BMessage(kMsgNewEntry), 'N');
	fileMenu->AddItem(fNewItem);
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Lock"),
		new BMessage(kMsgLockKeyring)));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Import KDBX" B_UTF8_ELLIPSIS),
		new BMessage(kMsgImport)));
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Export KDBX" B_UTF8_ELLIPSIS),
		new BMessage(kMsgExport)));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menuBar->AddItem(fileMenu);

	BMenu* entryMenu = new BMenu(B_TRANSLATE("Entry"));
	fOpenItem = new BMenuItem(B_TRANSLATE("Open/Edit" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOpenEntry));
	entryMenu->AddItem(fOpenItem);
	fCopyUserItem = new BMenuItem(B_TRANSLATE("Copy user name"),
		new BMessage(kMsgCopyUser), 'B');
	entryMenu->AddItem(fCopyUserItem);
	fCopyPasswordItem = new BMenuItem(B_TRANSLATE("Copy password"),
		new BMessage(kMsgCopyPassword), 'C');
	entryMenu->AddItem(fCopyPasswordItem);
	fDeleteItem = new BMenuItem(B_TRANSLATE("Delete" B_UTF8_ELLIPSIS),
		new BMessage(kMsgDeleteEntry));
	entryMenu->AddItem(fDeleteItem);
	menuBar->AddItem(entryMenu);

	fGroupList = new BColumnListView("Groups", 0, B_FANCY_BORDER, false);
	fGroupList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fGroupList->SetSelectionMessage(new BMessage(kMsgGroupSelected));
	float groupWidth = be_plain_font->StringWidth("M") * 16;
	fGroupList->SetExplicitMinSize(BSize(groupWidth, B_SIZE_UNSET));
	fGroupList->SetExplicitMaxSize(BSize(groupWidth, B_SIZE_UNLIMITED));
	fGroupList->SetExplicitPreferredSize(BSize(groupWidth, B_SIZE_UNSET));

	fEntryList = new BColumnListView("Entries", 0, B_FANCY_BORDER, false);
	fEntryList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fEntryList->SetSelectionMessage(new BMessage(kMsgEntrySelected));
	fEntryList->SetInvocationMessage(new BMessage(kMsgOpenEntry));

	fSearchControl = new BTextControl(B_TRANSLATE("Search:"), "",
		new BMessage(kMsgSearchChanged));
	fSearchControl->SetModificationMessage(new BMessage(kMsgSearchChanged));

	float em = be_plain_font->StringWidth("M");
	fGroupList->AddColumn(new BStringColumn(B_TRANSLATE("Keyring"), em * 14,
		em * 6, em * 40, B_TRUNCATE_END, B_ALIGN_LEFT), 0);
	fEntryList->AddColumn(new BStringColumn(B_TRANSLATE("Title"), em * 16,
		em * 6, em * 40, B_TRUNCATE_END, B_ALIGN_LEFT), 0);
	fEntryList->AddColumn(new BStringColumn(B_TRANSLATE("User name"), em * 12,
		em * 6, em * 40, B_TRUNCATE_END, B_ALIGN_LEFT), 1);
	fEntryList->AddColumn(new BStringColumn(B_TRANSLATE("URL"), em * 16,
		em * 6, em * 40, B_TRUNCATE_END, B_ALIGN_LEFT), 2);

	BScrollView* hint = new BScrollView("Empty hint",
		new EmptyHintView(B_TRANSLATE("No entries. Choose File > New entry")),
		0, false, false, B_FANCY_BORDER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0.0f)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING, 1.0f)
			.SetInsets(B_USE_WINDOW_INSETS)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(fSearchControl)
			.End()
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING, 1.0f)
				.Add(fGroupList)
				.AddCards()
					.GetLayout(&fEntryCards)
					.Add(fEntryList)
					.Add(hint)
				.End()
			.End()
		.End();

	fEntryCards->SetVisibleItem((int32)1);
}


PasswordsView::~PasswordsView()
{
	delete fClipboardRunner;
}


void
PasswordsView::AttachedToWindow()
{
	fSearchControl->SetTarget(BMessenger(this));
	fGroupList->SetTarget(BMessenger(this));
	fEntryList->SetTarget(BMessenger(this));

	BMenuBar* menuBar = Window()->KeyMenuBar();
	if (menuBar == NULL)
		menuBar = (BMenuBar*)FindView("menu");
	for (int32 i = 0; i < menuBar->CountItems(); i++) {
		BMenu* menu = menuBar->SubmenuAt(i);
		for (int32 j = 0; j < menu->CountItems(); j++) {
			BMenuItem* item = menu->ItemAt(j);
			if (item->Message() == NULL)
				continue;
			if (item->Message()->what == B_QUIT_REQUESTED)
				item->SetTarget(be_app);
			else
				item->SetTarget(this);
		}
	}

	_LoadKeyrings();
	fGroupList->AddToSelection(fGroupRows.ItemAt(0) != NULL
		? (BRow*)fGroupRows.ItemAt(0) : NULL);
	_UpdateMenus();
}


static BString
_ExtraValueOf(BKeyStore& store, const char* keyring, const BString& identifier,
	const char* suffix, const BString& username)
{
	BString value;
	BString extraId = identifier;
	extraId << suffix;
	BPasswordKey key;
	if (store.GetKey(keyring, B_KEY_TYPE_PASSWORD, extraId.String(),
			username.String(), key) == B_OK && key.DataLength() > 0)
		value.SetTo((const char*)key.Data());
	return value;
}


static BRow*
_CreateRow(const char* column0, const char* column1 = NULL,
	const char* column2 = NULL)
{
	BRow* row = new BRow();
	row->SetField(new BStringField(column0), 0);
	if (column1 != NULL)
		row->SetField(new BStringField(column1), 1);
	if (column2 != NULL)
		row->SetField(new BStringField(column2), 2);
	return row;
}


void
PasswordsView::_LoadKeyrings()
{
	fEntries.MakeEmpty();
	fGroupList->Clear();
	fGroupRows.MakeEmpty();
	fKeyrings.MakeEmpty();
	BRow* allRow = _CreateRow(B_TRANSLATE("All"));
	fGroupList->AddRow(allRow);
	fGroupRows.AddItem(allRow);

	uint32 cookie = 0;
	BString keyring;
	while (true) {
		status_t result = fKeyStore.GetNextKeyring(cookie, keyring);
		if (result == B_ENTRY_NOT_FOUND)
			break;
		if (result != B_OK)
			break;
		fKeyrings.AddItem(new BString(keyring));
		BRow* row = _CreateRow(keyring.String());
		fGroupList->AddRow(row);
		fGroupRows.AddItem(row);
	}

}


void
PasswordsView::_ClearRows(BList& rows, BColumnListView* list)
{
	list->RemoveRows(&rows);
	for (int32 i = 0; i < rows.CountItems(); i++)
		delete (BRow*)rows.ItemAt(i);
	rows.MakeEmpty();
}


void
PasswordsView::_LoadEntries(const char* keyring)
{
	fEntries.MakeEmpty();
	_ClearRows(fEntryRows, fEntryList);

	BString filter = fSearchControl->Text();
	filter.ToLower();

	BObjectList<BString, true> names;
	if (keyring != NULL)
		names.AddItem(new BString(keyring));
	else {
		for (int32 i = 0; i < fKeyrings.CountItems(); i++)
			names.AddItem(new BString(*fKeyrings.ItemAt(i)));
	}

	for (int32 k = 0; k < names.CountItems(); k++) {
		const char* ring = names.ItemAt(k)->String();
		uint32 cookie = 0;
		BPasswordKey key;
		while (true) {
			status_t result = fKeyStore.GetNextKey(ring, B_KEY_TYPE_ANY,
				B_KEY_PURPOSE_ANY, cookie, key);
			if (result == B_ENTRY_NOT_FOUND)
				break;
			if (result != B_OK) {
				_Error(strerror(result));
				break;
			}

			BString identifier = key.Identifier();
			if (identifier.EndsWith(kTotpSuffix)
				|| identifier.EndsWith(kUrlSuffix)
				|| identifier.EndsWith(kNotesSuffix))
				continue;

			BString username = key.SecondaryIdentifier();
			BString password;
			if (key.DataLength() > 0 && key.Data() != NULL)
				password.SetTo((const char*)key.Data());

			BString totp = _ExtraValueOf(fKeyStore, ring, identifier,
				kTotpSuffix, username);
			BString url = _ExtraValueOf(fKeyStore, ring, identifier,
				kUrlSuffix, username);
			BString notes = _ExtraValueOf(fKeyStore, ring, identifier,
				kNotesSuffix, username);

			if (filter.Length() > 0) {
				BString haystack = identifier;
				haystack << " " << username;
				haystack.ToLower();
				if (haystack.FindFirst(filter) < 0)
					continue;
			}

			EntryItem* item = new EntryItem(ring, identifier, username,
				password, totp, url, notes);
			fEntries.AddItem(item);
			BRow* row = _CreateRow(identifier.String(), username.String(),
				url.String());
			fEntryList->AddRow(row);
			fEntryRows.AddItem(row);
		}

	}

	fEntryCards->SetVisibleItem(fEntries.CountItems() == 0 ? 1 : 0);
	_UpdateMenus();
}


static int32
_RowIndex(BList& rows, BRow* row)
{
	for (int32 i = 0; i < rows.CountItems(); i++) {
		if (rows.ItemAt(i) == row)
			return i;
	}
	return -1;
}


void
PasswordsView::_Error(const char* message)
{
	BAlert* alert = new BAlert(B_TRANSLATE("Passwords"), message,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go();
}


void
PasswordsView::_UpdateMenus()
{
	bool haveEntry = fEntryList->CurrentSelection() != NULL;
	fOpenItem->SetEnabled(haveEntry);
	fCopyUserItem->SetEnabled(haveEntry);
	fCopyPasswordItem->SetEnabled(haveEntry);
	fDeleteItem->SetEnabled(haveEntry);
}


void
PasswordsView::_OpenEntry(int32 index)
{
	EntryItem* item = index >= 0 ? fEntries.ItemAt(index) : NULL;
	BString ringName = fCurrentKeyring;
	if (item != NULL)
		ringName = item->Keyring();
	else if (ringName.Length() == 0 && fKeyrings.CountItems() > 0)
		ringName = *fKeyrings.ItemAt(0);
	const char* keyring = ringName.String();
	BString id;
	if (item != NULL) {
		id = _EntryId(ringName, item->Title(), item->Username());
		for (int32 i = 0; i < fOpenEntries.CountItems(); i++) {
			OpenEntry* open = fOpenEntries.ItemAt(i);
			if (open->Id() == id) {
				open->Messenger().SendMessage(kMsgEntryActivate);
				return;
			}
		}
	}

	EntryWindow* window;
	if (item != NULL) {
		window = new EntryWindow(BMessenger(this), keyring,
			item->Title().String(), item->Username().String(),
			item->Password().String(), item->Totp().String(),
			item->Url().String(), item->Notes().String());
	} else {
		window = new EntryWindow(BMessenger(this), keyring, "", "", "", "",
			"", "");
	}
	fOpenEntries.AddItem(new OpenEntry(window, id));
	window->Show();
}


void
PasswordsView::_DeleteEntry()
{
	int32 index = _RowIndex(fEntryRows, fEntryList->CurrentSelection());
	EntryItem* item = index >= 0 ? fEntries.ItemAt(index) : NULL;
	if (item == NULL)
		return;

	BString text = B_TRANSLATE("Delete the entry \"%title%\"?");
	text.ReplaceAll("%title%", item->Title().String());
	BAlert* alert = new BAlert(B_TRANSLATE("Delete entry"), text.String(),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Delete"), NULL, B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return;

	BPasswordKey key;
	const char* keyring = item->Keyring().String();
	if (fKeyStore.GetKey(keyring, B_KEY_TYPE_PASSWORD, item->Title().String(),
			item->Username().String(), key) == B_OK)
		fKeyStore.RemoveKey(keyring, key);

	const char* suffixes[] = { kTotpSuffix, kUrlSuffix, kNotesSuffix };
	for (int32 i = 0; i < 3; i++) {
		BString extraId = item->Title();
		extraId << suffixes[i];
		if (fKeyStore.GetKey(keyring, B_KEY_TYPE_PASSWORD, extraId.String(),
				item->Username().String(), key) == B_OK)
			fKeyStore.RemoveKey(keyring, key);
	}
	_LoadEntries(fCurrentKeyring.Length() > 0
		? fCurrentKeyring.String() : NULL);
}


void
PasswordsView::_CopyToClipboard(const BString& text, int32 clearAfterSeconds)
{
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		BMessage* data = new BMessage(B_SIMPLE_DATA);
		data->AddString("text/plain", text.String());
		be_clipboard->Commit(data);
		be_clipboard->Unlock();
	}

	delete fClipboardRunner;
	fClipboardRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgClearClipboard), 1000000LL * clearAfterSeconds, 1);
}


void
PasswordsView::_ClearClipboard()
{
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		be_clipboard->Unlock();
	}
	delete fClipboardRunner;
	fClipboardRunner = NULL;
}


void
PasswordsView::_ExportDatabase()
{
	if (fCurrentKeyring.Length() == 0) {
		_Error(B_TRANSLATE("Select a keyring to export."));
		return;
	}

	BFilePanel* panel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this),
		NULL, 0, false, new BMessage(kMsgFilePanelSave));
	panel->Show();
}


void
PasswordsView::_ImportDatabase()
{
	BFilePanel* panel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
		NULL, 0, false, new BMessage(kMsgFilePanelOpen));
	panel->Show();
}


void
PasswordsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgGroupSelected:
		{
			BRow* row = fGroupList->CurrentSelection();
			int32 index = _RowIndex(fGroupRows, row);
			if (index < 0)
				return;
			if (index == 0) {
				fCurrentKeyring = "";
			} else {
				BString* name = fKeyrings.ItemAt(index - 1);
				if (name == NULL)
					return;
				fCurrentKeyring = *name;
			}
			_LoadEntries(fCurrentKeyring.Length() > 0
				? fCurrentKeyring.String() : NULL);
			break;
		}

		case kMsgEntrySelected:
			_UpdateMenus();
			break;

		case kMsgSearchChanged:
			_LoadEntries(fCurrentKeyring.Length() > 0
				? fCurrentKeyring.String() : NULL);
			break;

		case kMsgNewEntry:
			_OpenEntry(-1);
			break;

		case kMsgOpenEntry:
			_OpenEntry(_RowIndex(fEntryRows, fEntryList->CurrentSelection()));
			break;

		case kMsgDeleteEntry:
			_DeleteEntry();
			break;

		case kMsgCopyPassword:
		case kMsgCopyUser:
		{
			int32 index = _RowIndex(fEntryRows, fEntryList->CurrentSelection());
			EntryItem* item = index >= 0 ? fEntries.ItemAt(index) : NULL;
			if (item != NULL) {
				_CopyToClipboard(message->what == kMsgCopyPassword
					? item->Password() : item->Username(), kClipboardSeconds);
			}
			break;
		}

		case kMsgEntryCopy:
		{
			BString text = message->GetString("text", "");
			_CopyToClipboard(text, message->GetInt32("seconds",
				kClipboardSeconds));
			break;
		}

		case kMsgEntrySaved:
		{
			EntryWindow* window = NULL;
			message->FindPointer("window", (void**)&window);
			for (int32 i = 0; i < fOpenEntries.CountItems(); i++) {
				OpenEntry* open = fOpenEntries.ItemAt(i);
				if (open->Window() == window) {
					open->SetId(_EntryId(message->GetString("keyring", ""),
						message->GetString("title", ""),
						message->GetString("username", "")));
					break;
				}
			}
			_LoadEntries(fCurrentKeyring.Length() > 0
				? fCurrentKeyring.String() : NULL);
			break;
		}

		case kMsgEntryClosed:
		{
			EntryWindow* window = NULL;
			message->FindPointer("window", (void**)&window);
			for (int32 i = 0; i < fOpenEntries.CountItems(); i++) {
				if (fOpenEntries.ItemAt(i)->Window() == window) {
					delete fOpenEntries.RemoveItemAt(i);
					break;
				}
			}
			break;
		}

		case kMsgLockKeyring:
			if (fCurrentKeyring.Length() > 0)
				fKeyStore.LockKeyring(fCurrentKeyring.String());
			break;

		case kMsgExport:
			_ExportDatabase();
			break;

		case kMsgImport:
			_ImportDatabase();
			break;

		case kMsgClearClipboard:
			_ClearClipboard();
			break;

		case kMsgFilePanelSave:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				return;
			if (message->GetBool("cancel", false))
				return;
			BPath path(&ref);

			BString password;
			PasswordDialog dialog(B_TRANSLATE("Export keyring"),
				B_TRANSLATE("Enter the password that will protect the "
					"exported KDBX file."), true);
			if (dialog.GetPassword(password) != B_OK)
				return;

			KdbxFile file;
			status_t result = file.ExportToPath(path.Path(),
				password.String(), fCurrentKeyring.String());
			if (result != B_OK)
				_Error(strerror(result));
			break;
		}

		case kMsgFilePanelOpen:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				return;
			if (message->GetBool("cancel", false))
				return;
			BPath path(&ref);
			if (fCurrentKeyring.Length() == 0) {
				_Error(B_TRANSLATE("Select a keyring before importing."));
				return;
			}

			BString password;
			PasswordDialog dialog(B_TRANSLATE("Import KDBX file"),
				B_TRANSLATE("Enter the password of the KDBX file to "
					"import."), false);
			if (dialog.GetPassword(password) != B_OK)
				return;

			KdbxFile file;
			status_t result = file.ImportFromPath(path.Path(),
				password.String(), fCurrentKeyring.String(), false);
			if (result == B_OK)
				_LoadEntries(fCurrentKeyring.String());
			if (result != B_OK)
				_Error(strerror(result));
			break;
		}

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}
