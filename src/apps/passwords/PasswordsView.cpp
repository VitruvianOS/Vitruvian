/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PasswordsView.h"

#include "KdbxFile.h"
#include "PasswordDialog.h"
#include "VosCrypto.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Clipboard.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Path.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>
#include <time.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PasswordsView"


static const uint32 kMsgGroupSelected		= 'GrpS';
static const uint32 kMsgEntrySelected		= 'EntS';
static const uint32 kMsgSearchChanged		= 'Srch';
static const uint32 kMsgSaveEntry			= 'SvEn';
static const uint32 kMsgNewEntry			= 'NwEn';
static const uint32 kMsgDeleteEntry			= 'DelE';
static const uint32 kMsgCopyPassword		= 'CpyP';
static const uint32 kMsgCopyUser			= 'CpyU';
static const uint32 kMsgGenerate			= 'GenP';
static const uint32 kMsgLockKeyring			= 'Lock';
static const uint32 kMsgExport				= 'ExpK';
static const uint32 kMsgImport				= 'ImpK';
static const uint32 kMsgClearClipboard		= 'ClrC';
static const uint32 kMsgRefreshTotp			= 'RefT';
static const uint32 kMsgFilePanelSave		= 'FpSv';
static const uint32 kMsgFilePanelOpen		= 'FpOp';

static const char* kTotpSuffix = "/totp";


class EntryItem {
public:
								EntryItem(const BString& title,
									const BString& username,
									const BString& password,
									const BString& totp)
									:
									fTitle(title),
									fUsername(username),
									fPassword(password),
									fTotp(totp)
								{
								}

			const BString&		Title() const { return fTitle; }
			const BString&		Username() const { return fUsername; }
			const BString&		Password() const { return fPassword; }
			const BString&		Totp() const { return fTotp; }

private:
			BString				fTitle;
			BString				fUsername;
			BString				fPassword;
			BString				fTotp;
};


PasswordsView::PasswordsView()
	:
	BGroupView(B_VERTICAL, 0.0f),
	fHaveCurrent(false),
	fTotpRunner(NULL),
	fClipboardRunner(NULL),
	fClipboardMessageWhat(0)
{
	SetName(B_TRANSLATE("Passwords"));

	fGroupList = new BColumnListView("Groups", 0, B_FANCY_BORDER);
	fGroupList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fGroupList->SetSelectionMessage(new BMessage(kMsgGroupSelected));
	fEntryList = new BColumnListView("Entries", 0, B_FANCY_BORDER);
	fEntryList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fEntryList->SetSelectionMessage(new BMessage(kMsgEntrySelected));

	fSearchControl = new BTextControl(B_TRANSLATE("Search"), "",
		new BMessage(kMsgSearchChanged));
	fTitleControl = new BTextControl(B_TRANSLATE("Title"), "", NULL);
	fUserControl = new BTextControl(B_TRANSLATE("User"), "", NULL);
	fPasswordControl = new BTextControl(B_TRANSLATE("Password"), "", NULL);
	fUrlControl = new BTextControl(B_TRANSLATE("URL"), "", NULL);
	fNotesControl = new BTextView(BRect(0, 0, 300, 70), "Notes",
		BRect(4, 4, 296, 66), B_WILL_DRAW | B_FRAME_EVENTS);
	fTotpControl = new BTextControl(B_TRANSLATE("TOTP secret"), "", NULL);
	fTotpCodeView = new BStringView("TOTP code", B_TRANSLATE("------"));
	fStatusLabel = new BStringView("Status",
		B_TRANSLATE("Select a keyring"));

	fGroupList->AddColumn(new BStringColumn(B_TRANSLATE("Keyring"), 140, 80, 200,
		B_TRUNCATE_END, B_ALIGN_LEFT), 0);
	fEntryList->AddColumn(new BStringColumn(B_TRANSLATE("Title"), 160, 80, 240,
		B_TRUNCATE_END, B_ALIGN_LEFT), 0);
	fEntryList->AddColumn(new BStringColumn(B_TRANSLATE("User"), 120, 60, 180,
		B_TRUNCATE_END, B_ALIGN_LEFT), 1);

	BButton* saveButton = new BButton(B_TRANSLATE("Save"), new BMessage(kMsgSaveEntry));
	BButton* newButton = new BButton(B_TRANSLATE("New"), new BMessage(kMsgNewEntry));
	BButton* deleteButton = new BButton(B_TRANSLATE("Delete"),
		new BMessage(kMsgDeleteEntry));
	BButton* copyPasswordButton = new BButton(B_TRANSLATE("Copy password"),
		new BMessage(kMsgCopyPassword));
	BButton* copyUserButton = new BButton(B_TRANSLATE("Copy user"),
		new BMessage(kMsgCopyUser));
	BButton* generateButton = new BButton(B_TRANSLATE("Generate"),
		new BMessage(kMsgGenerate));
	BButton* lockButton = new BButton(B_TRANSLATE("Lock"), new BMessage(kMsgLockKeyring));
	BButton* exportButton = new BButton(B_TRANSLATE("Export KDBX"),
		new BMessage(kMsgExport));
	BButton* importButton = new BButton(B_TRANSLATE("Import KDBX"),
		new BMessage(kMsgImport));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0.0f)
		.AddStrut(4.0f)
		.AddGroup(B_HORIZONTAL, 4.0f)
			.Add(fGroupList)
			.Add(fEntryList)
			.AddGroup(B_VERTICAL, 4.0f)
				.Add(fSearchControl)
				.Add(fTitleControl)
				.Add(fUserControl)
				.Add(fPasswordControl)
				.Add(fUrlControl)
				.Add(fNotesControl)
				.AddGroup(B_HORIZONTAL, 4.0f)
					.Add(fTotpControl)
					.Add(fTotpCodeView)
				.End()
				.AddGroup(B_HORIZONTAL, 4.0f)
					.Add(saveButton)
					.Add(newButton)
					.Add(deleteButton)
					.Add(copyPasswordButton)
					.Add(copyUserButton)
					.Add(generateButton)
				.End()
				.AddGroup(B_HORIZONTAL, 4.0f)
					.Add(lockButton)
					.Add(exportButton)
					.Add(importButton)
				.End()
				.Add(fStatusLabel)
			.End()
		.End()
		.AddStrut(4.0f);
}


PasswordsView::~PasswordsView()
{
	delete fTotpRunner;
	delete fClipboardRunner;
}


void
PasswordsView::AttachedToWindow()
{
	fSearchControl->SetTarget(BMessenger(this));
	_LoadKeyrings();

	fTotpRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgRefreshTotp), 1000000, -1);
}


static BRow*
_CreateRow(const char* column0, const char* column1 = NULL)
{
	BRow* row = new BRow();
	row->SetField(new BStringField(column0), 0);
	if (column1 != NULL)
		row->SetField(new BStringField(column1), 1);
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

	if (fKeyrings.CountItems() == 0)
		fStatusLabel->SetText(B_TRANSLATE("No keyrings found"));
	else
		fStatusLabel->SetText(B_TRANSLATE("Select a keyring"));
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

	uint32 cookie = 0;
	BPasswordKey key;
	while (true) {
		status_t result = fKeyStore.GetNextKey(keyring, B_KEY_TYPE_ANY,
			B_KEY_PURPOSE_ANY, cookie, key);
		if (result == B_ENTRY_NOT_FOUND)
			break;
		if (result != B_OK) {
			fStatusLabel->SetText(strerror(result));
			return;
		}

		BString identifier = key.Identifier();
		if (identifier.EndsWith(kTotpSuffix))
			continue;

		BString username = key.SecondaryIdentifier();
		BString password;
		if (key.DataLength() > 0 && key.Data() != NULL)
			password.SetTo((const char*)key.Data());

		BString totp;
		BPasswordKey totpKey;
		BString totpIdentifier = identifier;
		totpIdentifier << kTotpSuffix;
		if (fKeyStore.GetKey(keyring, B_KEY_TYPE_PASSWORD,
				totpIdentifier.String(), username.String(), totpKey) == B_OK
			&& totpKey.DataLength() > 0)
			totp.SetTo((const char*)totpKey.Data());

		if (filter.Length() > 0) {
			BString haystack = identifier;
			haystack << " " << username;
			haystack.ToLower();
			if (haystack.FindFirst(filter) < 0)
				continue;
		}

		EntryItem* item = new EntryItem(identifier, username, password, totp);
		fEntries.AddItem(item);
		BRow* row = _CreateRow(identifier.String(), username.String());
		fEntryList->AddRow(row);
		fEntryRows.AddItem(row);
	}

	fHaveCurrent = false;
	if (fEntries.CountItems() == 0)
		fStatusLabel->SetText(B_TRANSLATE("No entries in this keyring"));
	else
		fStatusLabel->SetText(B_TRANSLATE("No entry selected"));
}


void
PasswordsView::_ShowEntry(int32 index)
{
	EntryItem* item = fEntries.ItemAt(index);
	if (item == NULL) {
		fHaveCurrent = false;
		return;
	}

	fTitleControl->SetText(item->Title().String());
	fUserControl->SetText(item->Username().String());
	fPasswordControl->SetText(item->Password().String());
	fTotpControl->SetText(item->Totp().String());
	fUrlControl->SetText("");
	fNotesControl->SetText("");
	fHaveCurrent = true;
	_UpdateTotp();
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
PasswordsView::_SaveCurrentEntry()
{
	if (!fHaveCurrent) {
		fStatusLabel->SetText("No entry selected");
		return;
	}

	if (fCurrentKeyring.Length() == 0) {
		fStatusLabel->SetText(B_TRANSLATE("Select a keyring first"));
		return;
	}

	BString title = fTitleControl->Text();
	if (title.Length() == 0) {
		fStatusLabel->SetText(B_TRANSLATE("Title is required"));
		return;
	}

	BString username = fUserControl->Text();
	BString password = fPasswordControl->Text();
	BString totp = fTotpControl->Text();

	const char* keyringName =
		fCurrentKeyring.Length() > 0 ? fCurrentKeyring.String() : NULL;
	BPasswordKey passwordKey(password.String(), B_KEY_PURPOSE_GENERIC,
		title.String(), username.String());
	status_t result = fKeyStore.AddKey(keyringName, passwordKey);
	if (result != B_OK) {
		fStatusLabel->SetText(strerror(result));
		return;
	}

	if (totp.Length() > 0) {
		BString totpIdentifier = title;
		totpIdentifier << kTotpSuffix;
		BPasswordKey totpKey(totp.String(), B_KEY_PURPOSE_GENERIC,
			totpIdentifier.String(), username.String());
		fKeyStore.AddKey(keyringName, totpKey);
	}

	_LoadEntries(keyringName);
	fStatusLabel->SetText(B_TRANSLATE("Entry saved"));
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
	fStatusLabel->SetText(B_TRANSLATE("Copied; clipboard clears shortly"));
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
	fStatusLabel->SetText(B_TRANSLATE("Clipboard cleared"));
}


void
PasswordsView::_UpdateTotp()
{
	const char* secret = fTotpControl->Text();
	if (secret == NULL || secret[0] == '\0') {
		fTotpCodeView->SetText(B_TRANSLATE("------"));
		return;
	}

	char code[9];
	time_t now = time(NULL);
	if (vos_TotpCode(secret, (uint64_t)(now / 30), 6, code) == 0)
		fTotpCodeView->SetText(code);
	else
		fTotpCodeView->SetText(B_TRANSLATE("bad secret"));
}


void
PasswordsView::_GeneratePassword()
{
	BPasswordKey password;
	if (fKeyStore.GeneratePassword(password, 20, 0x0f) != B_OK) {
		fStatusLabel->SetText(B_TRANSLATE("Password generation failed"));
		return;
	}
	fPasswordControl->SetText(password.Password());
	fStatusLabel->SetText(
		B_TRANSLATE("Generated password; save to store it"));
}


void
PasswordsView::_ExportDatabase()
{
	if (fCurrentKeyring.Length() == 0) {
		fStatusLabel->SetText(B_TRANSLATE("Select a keyring to export"));
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
		{
			BRow* row = fEntryList->CurrentSelection();
			_ShowEntry(_RowIndex(fEntryRows, row));
			break;
		}

		case kMsgSearchChanged:
			if (fCurrentKeyring.Length() > 0)
				_LoadEntries(fCurrentKeyring.String());
			break;

		case kMsgSaveEntry:
			_SaveCurrentEntry();
			break;

		case kMsgNewEntry:
			fTitleControl->SetText("");
			fUserControl->SetText("");
			fPasswordControl->SetText("");
			fUrlControl->SetText("");
			fNotesControl->SetText("");
			fTotpControl->SetText("");
			fHaveCurrent = true;
			break;

		case kMsgDeleteEntry:
		{
			if (!fHaveCurrent || fCurrentKeyring.Length() == 0)
				return;
			BString title = fTitleControl->Text();
			BString username = fUserControl->Text();
			BPasswordKey key;
			if (fKeyStore.GetKey(fCurrentKeyring.String(),
					B_KEY_TYPE_PASSWORD, title.String(), username.String(),
					key) == B_OK)
				fKeyStore.RemoveKey(fCurrentKeyring.String(), key);
			_LoadEntries(fCurrentKeyring.String());
			break;
		}

		case kMsgCopyPassword:
			if (fHaveCurrent)
				_CopyToClipboard(BString(fPasswordControl->Text()), 12);
			break;

		case kMsgCopyUser:
			if (fHaveCurrent)
				_CopyToClipboard(BString(fUserControl->Text()), 12);
			break;

		case kMsgGenerate:
			_GeneratePassword();
			break;

		case kMsgLockKeyring:
			if (fCurrentKeyring.Length() > 0) {
				fKeyStore.LockKeyring(fCurrentKeyring.String());
				fStatusLabel->SetText(B_TRANSLATE("Keyring locked"));
			}
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

		case kMsgRefreshTotp:
			_UpdateTotp();
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
			if (dialog.GetPassword(password) != B_OK) {
				fStatusLabel->SetText(B_TRANSLATE("Export canceled"));
				return;
			}

			KdbxFile file;
			status_t result = file.ExportToPath(path.Path(),
				password.String(), fCurrentKeyring.String());
			fStatusLabel->SetText(result == B_OK
				? B_TRANSLATE("Exported keyring to KDBX file")
				: strerror(result));
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
				fStatusLabel->SetText(
		B_TRANSLATE("Select a keyring before importing"));
				return;
			}

			BString password;
			PasswordDialog dialog(B_TRANSLATE("Import KDBX file"),
				B_TRANSLATE("Enter the password of the KDBX file to "
					"import."), false);
			if (dialog.GetPassword(password) != B_OK) {
				fStatusLabel->SetText(B_TRANSLATE("Import canceled"));
				return;
			}

			KdbxFile file;
			status_t result = file.ImportFromPath(path.Path(),
				password.String(), fCurrentKeyring.String(), false);
			if (result == B_OK)
				_LoadEntries(fCurrentKeyring.String());
			fStatusLabel->SetText(result == B_OK
				? B_TRANSLATE("Imported KDBX file")
				: strerror(result));
			break;
		}

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}
