/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Oauth2Provider.h"

#include <time.h>

#include <Button.h>
#include <String.h>
#include <TextControl.h>
#include <Window.h>

#include <AccountRoster.h>
#include <AccountsDefs.h>

#include "Oauth2Flow.h"


enum {
	msg_save			= 'OaSA',
	msg_cancel			= 'OaCA'
};


class Oauth2SetupWindow : public BWindow {
public:
								Oauth2SetupWindow(const BAccount& account,
									const BMessenger& replyTo);

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Quit();

private:
			void				_Save();

			BAccount			fAccount;
			BMessenger			fReplyTo;
			bool				fCommitted;
			BTextControl*		fLabel;
			BTextControl*		fClientId;
			BTextControl*		fAuthUri;
			BTextControl*		fTokenUri;
			BTextControl*		fScope;
};


Oauth2SetupWindow::Oauth2SetupWindow(const BAccount& account,
	const BMessenger& replyTo)
	:
	BWindow(BRect(120, 120, 490, 340), "OAuth2 Account",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE),
	fAccount(account),
	fReplyTo(replyTo),
	fCommitted(false)
{
	BView* parent = new BView(Bounds(), "parent", B_FOLLOW_ALL, 0);
	parent->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(parent);

	fLabel = new BTextControl(BRect(10, 10, 360, 32), "label", "Label:",
		account.Label(), NULL);
	parent->AddChild(fLabel);

	fClientId = new BTextControl(BRect(10, 40, 360, 62), "clientid",
		"Client id:", NULL, NULL);
	parent->AddChild(fClientId);

	fAuthUri = new BTextControl(BRect(10, 70, 360, 92), "authuri",
		"Authorization endpoint:", NULL, NULL);
	parent->AddChild(fAuthUri);

	fTokenUri = new BTextControl(BRect(10, 100, 360, 122), "tokenuri",
		"Token endpoint:", NULL, NULL);
	parent->AddChild(fTokenUri);

	fScope = new BTextControl(BRect(10, 130, 360, 152), "scope", "Scope:",
		NULL, NULL);
	parent->AddChild(fScope);

	BString value;
	if (account.GetSetting("client_id", value) == B_OK)
		fClientId->SetText(value.String());
	if (account.GetSetting("auth_uri", value) == B_OK)
		fAuthUri->SetText(value.String());
	if (account.GetSetting("token_uri", value) == B_OK)
		fTokenUri->SetText(value.String());
	if (account.GetSetting("scope", value) == B_OK)
		fScope->SetText(value.String());

	BButton* save = new BButton(BRect(140, 175, 240, 200), "save", "Save",
		new BMessage(msg_save));
	save->MakeDefault(true);
	parent->AddChild(save);

	BButton* cancel = new BButton(BRect(250, 175, 350, 200), "cancel",
		"Cancel", new BMessage(msg_cancel));
	parent->AddChild(cancel);
}


void
Oauth2SetupWindow::MessageReceived(BMessage* message)
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
Oauth2SetupWindow::Quit()
{
	if (!fCommitted) {
		BMessage reply(kAccountEditResult);
		reply.AddInt32(kAccountsStatusField, B_ERROR);
		fReplyTo.SendMessage(&reply);
	}

	BWindow::Quit();
}


void
Oauth2SetupWindow::_Save()
{
	if (fClientId->TextLength() == 0 || fAuthUri->TextLength() == 0
		|| fTokenUri->TextLength() == 0) {
		return;
	}

	fAccount.SetLabel(fLabel->Text());
	fAccount.SetSetting("client_id", fClientId->Text());
	fAccount.SetSetting("auth_uri", fAuthUri->Text());
	fAccount.SetSetting("token_uri", fTokenUri->Text());
	fAccount.SetSetting("scope", fScope->Text());

	status_t status = B_NO_INIT;
	BAccountRoster* roster = BAccountRoster::Default();
	if (roster != NULL) {
		BAccount existing;
		if (roster->FindAccount(fAccount.Id(), existing) == B_OK)
			status = roster->UpdateAccount(fAccount);
		else
			status = roster->AddAccount(fAccount);
	}

	fCommitted = status == B_OK;

	BMessage reply(kAccountEditResult);
	reply.AddInt32(kAccountsStatusField, status);
	fReplyTo.SendMessage(&reply);

	PostMessage(B_QUIT_REQUESTED);
}


status_t
Oauth2Provider::EditAccount(BAccount& account, const BMessenger& replyTo)
{
	Oauth2SetupWindow* window = new Oauth2SetupWindow(account, replyTo);
	window->Show();
	return B_OK;
}


status_t
Oauth2Provider::GetToken(BAccount& account, const BMessenger& consentTo,
	BMessage& token)
{
	Oauth2Flow flow;

	BString accessToken;
	BString refreshToken;
	bigtime_t expiry = 0;
	bigtime_t now = (bigtime_t)time(NULL) * 1000000LL;
	if (Oauth2Flow::Load(account, accessToken, refreshToken, expiry) == B_OK
		&& expiry > now + 60000000LL && accessToken.Length() > 0) {
		token.AddString(kAccountsTokenField, accessToken);
		token.AddInt64(kAccountsExpiryField, expiry);
		return B_OK;
	}

	if (refreshToken.Length() > 0 && flow.Refresh(account, token) == B_OK)
		return B_OK;

	return flow.RunInteractive(account, consentTo, token);
}


status_t
Oauth2Provider::RemoveAccount(BAccount& account)
{
	Oauth2Flow::Remove(account);
	return B_OK;
}


const char*
Oauth2Provider::Id() const
{
	return "oauth2";
}


const char*
Oauth2Provider::DisplayName() const
{
	return "OAuth2";
}


uint32
Oauth2Provider::Capabilities() const
{
	return 0;
}


extern "C" BAccountProvider*
instantiate_account_provider()
{
	return new Oauth2Provider();
}
