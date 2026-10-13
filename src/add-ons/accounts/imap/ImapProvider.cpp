/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ImapProvider.h"

#include <stdlib.h>

#include <Button.h>
#include <Key.h>
#include <KeyStore.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <AccountRoster.h>
#include <AccountsDefs.h>


enum {
	msg_save			= 'ImSA',
	msg_cancel			= 'ImCA'
};


class ImapSetupWindow : public BWindow {
public:
								ImapSetupWindow(const BAccount& account,
									const BMessenger& replyTo);

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Quit();

private:
			void				_Save();

			BAccount			fAccount;
			BMessenger			fReplyTo;
			bool				fCommitted;
			BTextControl*		fLabel;
			BTextControl*		fImapServer;
			BTextControl*		fImapPort;
			BTextControl*		fSmtpServer;
			BTextControl*		fSmtpPort;
			BTextControl*		fUserName;
			BTextControl*		fPassword;
};


ImapSetupWindow::ImapSetupWindow(const BAccount& account,
	const BMessenger& replyTo)
	:
	BWindow(BRect(120, 120, 470, 400), "IMAP and SMTP Account",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE),
	fAccount(account),
	fReplyTo(replyTo),
	fCommitted(false)
{
	BView* parent = new BView(Bounds(), "parent", B_FOLLOW_ALL, 0);
	parent->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(parent);

	fLabel = new BTextControl(BRect(10, 10, 340, 32), "label", "Label:",
		account.Label(), NULL);
	parent->AddChild(fLabel);

	fImapServer = new BTextControl(BRect(10, 40, 340, 62), "imapserver",
		"IMAP server:", NULL, NULL);
	parent->AddChild(fImapServer);

	fImapPort = new BTextControl(BRect(10, 70, 340, 92), "imapport",
		"IMAP port:", "993", NULL);
	parent->AddChild(fImapPort);

	fSmtpServer = new BTextControl(BRect(10, 100, 340, 122), "smtpserver",
		"SMTP server:", NULL, NULL);
	parent->AddChild(fSmtpServer);

	fSmtpPort = new BTextControl(BRect(10, 130, 340, 152), "smtpport",
		"SMTP port:", "587", NULL);
	parent->AddChild(fSmtpPort);

	fUserName = new BTextControl(BRect(10, 160, 340, 182), "username",
		"User name:", account.UserName(), NULL);
	parent->AddChild(fUserName);

	fPassword = new BTextControl(BRect(10, 190, 340, 212), "password",
		"Password:", NULL, NULL);
	parent->AddChild(fPassword);

	BString value;
	if (account.GetSetting("imap_server", value) == B_OK)
		fImapServer->SetText(value.String());
	if (account.GetSetting("smtp_server", value) == B_OK)
		fSmtpServer->SetText(value.String());

	int32 port = 0;
	if (account.GetSetting("imap_port", port) == B_OK) {
		BString text;
		text << port;
		fImapPort->SetText(text.String());
	}
	if (account.GetSetting("smtp_port", port) == B_OK) {
		BString text;
		text << port;
		fSmtpPort->SetText(text.String());
	}

	BString password;
	if (account.RetrievePassword(password) == B_OK)
		fPassword->SetText(password.String());

	BButton* save = new BButton(BRect(130, 235, 230, 260), "save", "Save",
		new BMessage(msg_save));
	save->MakeDefault(true);
	parent->AddChild(save);

	BButton* cancel = new BButton(BRect(240, 235, 340, 260), "cancel",
		"Cancel", new BMessage(msg_cancel));
	parent->AddChild(cancel);
}


void
ImapSetupWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case msg_save:
			_Save();
			break;

		case msg_cancel:
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
ImapSetupWindow::Quit()
{
	if (!fCommitted) {
		BMessage reply(kAccountEditResult);
		reply.AddInt32(kAccountsStatusField, B_ERROR);
		fReplyTo.SendMessage(&reply);
	}

	BWindow::Quit();
}


void
ImapSetupWindow::_Save()
{
	if (fImapServer->TextLength() == 0 || fUserName->TextLength() == 0)
		return;

	fAccount.SetLabel(fLabel->Text());
	fAccount.SetUserName(fUserName->Text());
	fAccount.SetSetting("imap_server", fImapServer->Text());
	fAccount.SetSetting("imap_port", (int32)atoi(fImapPort->Text()));
	fAccount.SetSetting("smtp_server", fSmtpServer->Text());
	fAccount.SetSetting("smtp_port", (int32)atoi(fSmtpPort->Text()));

	status_t status = fAccount.StorePassword(fPassword->Text());
	if (status == B_OK) {
		BAccountRoster* roster = BAccountRoster::Default();
		if (roster != NULL) {
			BAccount existing;
			if (roster->FindAccount(fAccount.Id(), existing) == B_OK)
				status = roster->UpdateAccount(fAccount);
			else
				status = roster->AddAccount(fAccount);
		} else
			status = B_NO_INIT;
	}

	fCommitted = status == B_OK;

	BMessage reply(kAccountEditResult);
	reply.AddInt32(kAccountsStatusField, status);
	fReplyTo.SendMessage(&reply);

	PostMessage(B_QUIT_REQUESTED);
}


status_t
ImapProvider::EditAccount(BAccount& account, const BMessenger& replyTo)
{
	ImapSetupWindow* window = new ImapSetupWindow(account, replyTo);
	window->Show();
	return B_OK;
}


status_t
ImapProvider::Authenticate(BAccount& account, const BMessage& /*parameters*/)
{
	BString password;
	return account.RetrievePassword(password);
}


status_t
ImapProvider::RemoveAccount(BAccount& account)
{
	BKeyStore store;
	BKey key(B_KEY_PURPOSE_NETWORK, account.Id(), "password");
	store.RemoveKey("Accounts", key);
	return B_OK;
}


const char*
ImapProvider::Id() const
{
	return "imap-smtp";
}


const char*
ImapProvider::DisplayName() const
{
	return "IMAP and SMTP";
}


uint32
ImapProvider::Capabilities() const
{
	return kAccountCapabilityMail;
}


extern "C" BAccountProvider*
instantiate_account_provider()
{
	return new ImapProvider();
}
