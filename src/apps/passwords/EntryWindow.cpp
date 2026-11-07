/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "EntryWindow.h"

#include "VosCrypto.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <string.h>
#include <time.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "EntryWindow"


static const uint32 kMsgSave			= 'Save';
static const uint32 kMsgShowPassword	= 'ShwP';
static const uint32 kMsgCopyPassword	= 'CpyP';
static const uint32 kMsgGenerate		= 'GenP';
static const uint32 kMsgRefreshTotp		= 'RefT';
static const uint32 kMsgTotpChanged		= 'TotC';

static const char* kTotpSuffix = "/totp";
static const int32 kNotesLines = 4;
static const int32 kClipboardSeconds = 12;


EntryWindow::EntryWindow(const BMessenger& target, const char* keyring,
	const char* title, const char* username, const char* password,
	const char* totp, const char* url, const char* notes)
	:
	BWindow(BRect(0, 0, 10, 10),
		title != NULL && title[0] != '\0' ? title : B_TRANSLATE("New entry"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fTarget(target),
	fKeyring(keyring),
	fOrigTitle(title),
	fOrigUsername(username),
	fTotpRunner(NULL)
{
	fTitleControl = new BTextControl(B_TRANSLATE("Title:"), title, NULL);
	fUserControl = new BTextControl(B_TRANSLATE("User name:"), username, NULL);
	fPasswordControl = new BTextControl(B_TRANSLATE("Password:"), password,
		NULL);
	fPasswordControl->TextView()->HideTyping(true);
	fUrlControl = new BTextControl(B_TRANSLATE("URL:"), url, NULL);
	fTotpControl = new BTextControl(B_TRANSLATE("Two-factor key:"), totp,
		new BMessage(kMsgTotpChanged));
	fTotpControl->SetModificationMessage(new BMessage(kMsgTotpChanged));

	fNotesControl = new BTextView("Notes");
	fNotesControl->SetText(notes);
	BScrollView* notesScroll = new BScrollView("Notes scroll", fNotesControl,
		0, false, true, B_FANCY_BORDER);
	font_height fh;
	be_plain_font->GetHeight(&fh);
	float lineHeight = ceilf(fh.ascent + fh.descent + fh.leading);
	notesScroll->SetExplicitMinSize(
		BSize(B_SIZE_UNSET, lineHeight * kNotesLines));

	BStringView* notesLabel = new BStringView("Notes label",
		B_TRANSLATE("Notes:"));
	notesLabel->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	fShowCheck = new BCheckBox(B_TRANSLATE("Show"),
		new BMessage(kMsgShowPassword));
	BButton* copyButton = new BButton(B_TRANSLATE("Copy"),
		new BMessage(kMsgCopyPassword));
	BButton* generateButton = new BButton(B_TRANSLATE("Generate"),
		new BMessage(kMsgGenerate));

	fTotpCodeLabel = new BStringView("Totp label",
		B_TRANSLATE("Current code:"));
	fTotpCodeView = new BStringView("Totp code", "");

	BButton* cancelButton = new BButton(B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	BButton* saveButton = new BButton(B_TRANSLATE("Save"),
		new BMessage(kMsgSave));
	saveButton->MakeDefault(true);
	float buttonWidth = max_c(cancelButton->PreferredSize().width,
		saveButton->PreferredSize().width);
	cancelButton->SetExplicitSize(BSize(buttonWidth, B_SIZE_UNSET));
	saveButton->SetExplicitSize(BSize(buttonWidth, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGrid(B_USE_HALF_ITEM_SPACING, B_USE_HALF_ITEM_SPACING)
			.AddTextControl(fTitleControl, 0, 0,
				B_ALIGN_HORIZONTAL_UNSET, 1, 2)
			.AddTextControl(fUserControl, 0, 1,
				B_ALIGN_HORIZONTAL_UNSET, 1, 2)
			.AddTextControl(fPasswordControl, 0, 2)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING, 2, 2)
				.Add(fShowCheck)
				.Add(copyButton)
				.Add(generateButton)
			.End()
			.AddTextControl(fUrlControl, 0, 3,
				B_ALIGN_HORIZONTAL_UNSET, 1, 2)
			.Add(notesLabel, 0, 4)
			.Add(notesScroll, 1, 4, 2)
			.AddTextControl(fTotpControl, 0, 5,
				B_ALIGN_HORIZONTAL_UNSET, 1, 2)
			.Add(fTotpCodeLabel, 0, 6)
			.Add(fTotpCodeView, 1, 6, 2)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.AddGlue()
			.Add(cancelButton)
			.Add(saveButton)
		.End();

	_UpdateTotp();
	fTotpRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgRefreshTotp), 1000000, -1);

	fTitleControl->MakeFocus(true);
	CenterOnScreen();
}


EntryWindow::~EntryWindow()
{
	delete fTotpRunner;
}


bool
EntryWindow::QuitRequested()
{
	BMessage closed(kMsgEntryClosed);
	closed.AddPointer("window", this);
	fTarget.SendMessage(&closed);
	return true;
}


void
EntryWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSave:
			_Save();
			break;

		case kMsgShowPassword:
			fPasswordControl->TextView()->HideTyping(
				fShowCheck->Value() != B_CONTROL_ON);
			fPasswordControl->TextView()->Invalidate();
			break;

		case kMsgCopyPassword:
		{
			BMessage copy(kMsgEntryCopy);
			copy.AddString("text", fPasswordControl->Text());
			copy.AddInt32("seconds", kClipboardSeconds);
			fTarget.SendMessage(&copy);
			break;
		}

		case kMsgGenerate:
			_GeneratePassword();
			break;

		case kMsgRefreshTotp:
		case kMsgTotpChanged:
			_UpdateTotp();
			break;

		case kMsgEntryActivate:
			Activate();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
EntryWindow::_Save()
{
	BString title = fTitleControl->Text();
	if (title.Length() == 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Save entry"),
			B_TRANSLATE("Title is required."), B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	if (fKeyring.Length() == 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Save entry"),
			B_TRANSLATE("Select a keyring first."), B_TRANSLATE("OK"), NULL,
			NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	BString username = fUserControl->Text();
	BString password = fPasswordControl->Text();
	BString totp = fTotpControl->Text();
	BString url = fUrlControl->Text();
	BString notes = fNotesControl->Text();

	const char* keyringName = fKeyring.String();
	BPasswordKey passwordKey(password.String(), B_KEY_PURPOSE_GENERIC,
		title.String(), username.String());
	status_t result = fKeyStore.AddKey(keyringName, passwordKey);
	if (result != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Save entry"),
			strerror(result), B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	if (fOrigTitle.Length() > 0
		&& (fOrigTitle != title || fOrigUsername != username))
		_RemoveEntry(fOrigTitle, fOrigUsername);

	_StoreExtra(title, kTotpSuffix, username, totp);
	_StoreExtra(title, kUrlSuffix, username, url);
	_StoreExtra(title, kNotesSuffix, username, notes);
	fOrigTitle = title;
	fOrigUsername = username;

	BMessage saved(kMsgEntrySaved);
	saved.AddPointer("window", this);
	saved.AddString("title", title);
	saved.AddString("username", username);
	saved.AddString("keyring", fKeyring);
	fTarget.SendMessage(&saved);

	PostMessage(B_QUIT_REQUESTED);
}


void
EntryWindow::_StoreExtra(const BString& title, const char* suffix,
	const BString& username, const BString& value)
{
	BString identifier = title;
	identifier << suffix;
	if (value.Length() > 0) {
		BPasswordKey key(value.String(), B_KEY_PURPOSE_GENERIC,
			identifier.String(), username.String());
		fKeyStore.AddKey(fKeyring.String(), key);
		return;
	}

	BPasswordKey old;
	if (fKeyStore.GetKey(fKeyring.String(), B_KEY_TYPE_PASSWORD,
			identifier.String(), username.String(), old) == B_OK)
		fKeyStore.RemoveKey(fKeyring.String(), old);
}


void
EntryWindow::_RemoveEntry(const BString& title, const BString& username)
{
	BPasswordKey old;
	if (fKeyStore.GetKey(fKeyring.String(), B_KEY_TYPE_PASSWORD,
			title.String(), username.String(), old) == B_OK)
		fKeyStore.RemoveKey(fKeyring.String(), old);

	_StoreExtra(title, kTotpSuffix, username, "");
	_StoreExtra(title, kUrlSuffix, username, "");
	_StoreExtra(title, kNotesSuffix, username, "");
}


void
EntryWindow::_UpdateTotp()
{
	const char* secret = fTotpControl->Text();
	bool haveKey = secret != NULL && secret[0] != '\0';

	if (!haveKey) {
		if (!fTotpCodeView->IsHidden(fTotpCodeView)) {
			fTotpCodeLabel->Hide();
			fTotpCodeView->Hide();
		}
		return;
	}

	if (fTotpCodeView->IsHidden(fTotpCodeView)) {
		fTotpCodeLabel->Show();
		fTotpCodeView->Show();
	}

	char code[9];
	time_t now = time(NULL);
	BString text;
	if (vos_TotpCode(secret, (uint64_t)(now / 30), 6, code) == 0) {
		BString digits = code;
		if (digits.Length() == 6)
			digits.Insert(" ", 1, 3);
		text << digits << "  (";
		char seconds[16];
		snprintf(seconds, sizeof(seconds), "%d", (int)(30 - now % 30));
		BString format = B_TRANSLATE("%seconds% s");
		format.ReplaceAll("%seconds%", seconds);
		text << format << ")";
	} else
		text = B_TRANSLATE("bad key");
	fTotpCodeView->SetText(text.String());
}


void
EntryWindow::_GeneratePassword()
{
	BPasswordKey password;
	if (fKeyStore.GeneratePassword(password, 20, 0x0f) != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Generate password"),
			B_TRANSLATE("Password generation failed."), B_TRANSLATE("OK"),
			NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}
	fPasswordControl->SetText(password.Password());
}
