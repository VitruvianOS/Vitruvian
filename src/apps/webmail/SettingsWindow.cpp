/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SettingsWindow.h"
#include "OAuth2.h"
#include "WebMailMessages.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <String.h>
#include <TextControl.h>

#include <stdlib.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SettingsWindow"


namespace {

const uint32 kMsgSave		= 'WsSv';
const uint32 kMsgSignIn		= 'WsOa';
const uint32 kMsgToggled	= 'WsTg';

}


SettingsWindow::SettingsWindow(const Account& account,
	const BMessenger& target)
	:
	BWindow(BRect(150, 150, 450, 350), B_TRANSLATE("Account settings"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fAccountId(account.id),
	fOAuthProvider(account.oauthProvider),
	fTarget(target),
	fSignature(NULL),
	fUseIdle(NULL),
	fLeaveOnServer(NULL),
	fDeleteAfterDays(NULL),
	fClientId(NULL),
	fOAuthButton(NULL)
{
	if (fOAuthProvider.Length() == 0 && account.authMethod != "password")
		fOAuthProvider = account.provider;

	fSignature = new BTextControl("signature", B_TRANSLATE("Signature:"),
		account.signature.String(), NULL);
	if (account.kind == IMAP_ACCOUNT) {
		fUseIdle = new BCheckBox("idle", B_TRANSLATE("Use IMAP IDLE (push)"),
			new BMessage(kMsgToggled));
		fUseIdle->SetValue(account.useIDLE ? B_CONTROL_ON : B_CONTROL_OFF);
	}
	if (account.kind == POP3_ACCOUNT) {
		fLeaveOnServer = new BCheckBox("leave",
			B_TRANSLATE("Leave messages on the server"),
			new BMessage(kMsgToggled));
		fLeaveOnServer->SetValue(account.leaveOnServer
			? B_CONTROL_ON : B_CONTROL_OFF);
		BString days;
		days << account.deleteAfterDays;
		fDeleteAfterDays = new BTextControl("days",
			B_TRANSLATE("Delete after (days):"), days.String(), NULL);
	}

	BButton* save = new BButton("save", B_TRANSLATE("Save"),
		new BMessage(kMsgSave));
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	SetDefaultButton(save);

	BLayoutBuilder::Group<> root(this, B_VERTICAL);
	root.SetInsets(B_USE_WINDOW_INSETS);

	BLayoutBuilder::Grid<> grid(B_USE_DEFAULT_SPACING,
		B_USE_HALF_ITEM_SPACING);
	int32 row = 0;
	grid.Add(fSignature->CreateLabelLayoutItem(), 0, row)
		.Add(fSignature->CreateTextViewLayoutItem(), 1, row);
	row++;
	if (account.kind == IMAP_ACCOUNT)
		grid.Add(fUseIdle, 1, row++);
	if (account.kind == POP3_ACCOUNT) {
		grid.Add(fLeaveOnServer, 1, row++);
		grid.Add(fDeleteAfterDays->CreateLabelLayoutItem(), 0, row)
			.Add(fDeleteAfterDays->CreateTextViewLayoutItem(), 1, row);
		row++;
	}
	if (fOAuthProvider.Length() > 0) {
		fClientId = new BTextControl("client", B_TRANSLATE("Client ID:"),
			OAuth2ClientIdFor(fOAuthProvider.String()).String(), NULL);
		fOAuthButton = new BButton("signin",
			B_TRANSLATE("Sign in" B_UTF8_ELLIPSIS),
			new BMessage(kMsgSignIn));
		grid.Add(fClientId->CreateLabelLayoutItem(), 0, row)
			.Add(fClientId->CreateTextViewLayoutItem(), 1, row);
		row++;
		grid.Add(fOAuthButton, 1, row++);
	}
	root.Add(grid.View());

	root.AddGroup(B_HORIZONTAL)
		.AddGlue()
		.Add(cancel)
		.Add(save)
		.End();
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSave:
			_Save();
			break;
		case kMsgSignIn:
			_SignIn();
			break;
		case kMsgToggled:
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
SettingsWindow::QuitRequested()
{
	fTarget.SendMessage(MSG_SETTINGS_CLOSED);
	return true;
}


void
SettingsWindow::_SignIn()
{
	OAuth2SetClientIdFor(fOAuthProvider.String(), fClientId->Text());
	OAuth2RunConsentFlow(fOAuthProvider.String(), fTarget,
		fAccountId.String(), BString());
}


void
SettingsWindow::_Save()
{
	AccountsService* accounts = DefaultAccountsService();
	Account account;
	if (accounts->LoadAccount(fAccountId.String(), account) != B_OK) {
		(new BAlert("WebMail", B_TRANSLATE("Cannot load the account."),
			B_TRANSLATE("OK")))->Go();
		return;
	}
	account.signature = fSignature->Text();
	if (fUseIdle != NULL)
		account.useIDLE = fUseIdle->Value() == B_CONTROL_ON;
	if (fLeaveOnServer != NULL) {
		account.leaveOnServer = fLeaveOnServer->Value() == B_CONTROL_ON;
		account.deleteAfterDays = atoi(fDeleteAfterDays->Text());
	}
	if (accounts->SaveAccount(account) != B_OK) {
		(new BAlert("WebMail", B_TRANSLATE("Cannot save the account."),
			B_TRANSLATE("OK")))->Go();
		return;
	}
	Close();
}
