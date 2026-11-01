/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SettingsWindow.h"
#include "Accounts.h"
#include "OAuth2.h"
#include <MailProviders.h>
#include "WebMailMessages.h"
#include <MailAutoconfig.h>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <View.h>

#include <stdlib.h>

#include <thread>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SettingsWindow"


namespace {

enum {
	kRowKind = 0,
	kRowProvider,
	kRowAddress,
	kRowPassword,
	kRowUrl,
	kRowName,
	kRowSignature,
	kRowImap,
	kRowPop3,
	kRowSmtp,
	kRowIdle,
	kRowOAuth,
	kRowClientId,
	kRowButtons
};


const uint32 kMsgKindWeb		= 'WsK1';
const uint32 kMsgKindImap		= 'WsK2';
const uint32 kMsgKindPop3		= 'WsK3';
const uint32 kMsgProviderPicked= 'WsP1';
const uint32 kMsgSave			= 'WsSv';
const uint32 kMsgAutoconfig		= 'WsAc';
const uint32 kMsgOAuth			= 'WsOa';
const uint32 kMsgNone			= 'Ws00';


BString
SelectedProviderId(BMenuField* field)
{
	BMenuItem* item = field->Menu()->FindMarked();
	return item != NULL ? BString(item->Message()->GetString("id", ""))
		: BString();
}


} // namespace


SettingsWindow::SettingsWindow(const BMessenger& target)
	:
	BWindow(BRect(150, 150, 520, 400), B_TRANSLATE("Add Account"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fGrid(NULL),
	fKindWeb(NULL),
	fKindImap(NULL),
	fKindPop3(NULL),
	fProviderField(NULL),
	fOauthProviderField(NULL),
	fAddress(NULL),
	fPassword(NULL),
	fUrl(NULL),
	fIdentityName(NULL),
	fSignature(NULL),
	fImapHost(NULL),
	fImapPort(NULL),
	fPop3Host(NULL),
	fPop3Port(NULL),
	fSmtpHost(NULL),
	fSmtpPort(NULL),
	fImapTls(NULL),
	fPop3Tls(NULL),
	fSmtpTls(NULL),
	fUseIdle(NULL),
	fClientId(NULL),
	fAutoconfigButton(NULL),
	fOAuthButton(NULL),
	fSaveButton(NULL)
{
	// Provider menus: every catalog entry plus custom; OAuth2 gets
	// the three providers whose mail services accept installed-app
	// clients.
	BPopUpMenu* providerMenu = new BPopUpMenu("providers");
	std::vector<WebProvider> providers;
	GetWebProviders(providers);
	for (const WebProvider& provider : providers) {
		BMessage* pick = new BMessage(kMsgProviderPicked);
		pick->AddString("id", provider.id);
		providerMenu->AddItem(new BMenuItem(provider.name, pick));
	}
	BMessage* custom = new BMessage(kMsgProviderPicked);
	custom->AddString("id", "custom");
	providerMenu->AddItem(new BMenuItem(B_TRANSLATE("Custom URL"), custom));
	providerMenu->SetLabelFromMarked(true);
	if (providerMenu->ItemAt(0) != NULL)
		providerMenu->ItemAt(0)->SetMarked(true);

	BPopUpMenu* oauthMenu = new BPopUpMenu("oauth");
	const char* oauthIds[] = { "google", "microsoft", "yahoo", NULL };
	const char* oauthNames[] = { "Google", "Microsoft", "Yahoo", NULL };
	for (int32 i = 0; oauthIds[i] != NULL; i++) {
		BMessage* pick = new BMessage(kMsgProviderPicked);
		pick->AddString("oauth", oauthIds[i]);
		oauthMenu->AddItem(new BMenuItem(oauthNames[i], pick));
	}
	BMessage* noOauth = new BMessage(kMsgProviderPicked);
	noOauth->AddString("oauth", "");
	BMenuItem* noneItem = new BMenuItem(B_TRANSLATE("None (app password)"),
		noOauth);
	oauthMenu->AddItem(noneItem);
	oauthMenu->SetLabelFromMarked(true);
	noneItem->SetMarked(true);

	fProviderField = new BMenuField(B_TRANSLATE("Provider:"), providerMenu);
	fOauthProviderField = new BMenuField(B_TRANSLATE("OAuth2 provider:"),
		oauthMenu);

	fAddress = _NewField(B_TRANSLATE("Address:"));
	fPassword = _NewField(B_TRANSLATE("Password:"));
	fUrl = _NewField(B_TRANSLATE("Webmail URL:"));
	fIdentityName = _NewField(B_TRANSLATE("Your name:"));
	fSignature = _NewField(B_TRANSLATE("Signature:"));
	fImapHost = _NewField(B_TRANSLATE("IMAP server:"));
	fImapPort = _NewField(B_TRANSLATE("Port:"));
	fPop3Host = _NewField(B_TRANSLATE("POP3 server:"));
	fPop3Port = _NewField(B_TRANSLATE("Port:"));
	fSmtpHost = _NewField(B_TRANSLATE("SMTP server:"));
	fSmtpPort = _NewField(B_TRANSLATE("Port:"));
	fClientId = _NewField(B_TRANSLATE("OAuth2 client ID:"));
	fImapTls = new BCheckBox(B_TRANSLATE("TLS"), new BMessage(kMsgNone));
	fPop3Tls = new BCheckBox(B_TRANSLATE("TLS"), new BMessage(kMsgNone));
	fSmtpTls = new BCheckBox(B_TRANSLATE("TLS"), new BMessage(kMsgNone));
	fUseIdle = new BCheckBox(B_TRANSLATE("Use IMAP IDLE (push)"),
		new BMessage(kMsgNone));

	float portWidth = fImapPort->StringWidth("00000") + 16;
	BTextControl* ports[] = { fImapPort, fPop3Port, fSmtpPort };
	for (BTextControl* port : ports) {
		port->SetExplicitMaxSize(BSize(portWidth, B_SIZE_UNSET));
		port->SetExplicitMinSize(BSize(portWidth, B_SIZE_UNSET));
	}

	fKindWeb = new BRadioButton(B_TRANSLATE("Web account"),
		new BMessage(kMsgKindWeb));
	fKindImap = new BRadioButton("IMAP", new BMessage(kMsgKindImap));
	fKindPop3 = new BRadioButton("POP3", new BMessage(kMsgKindPop3));
	fAutoconfigButton = new BButton(B_TRANSLATE("Autoconfig"),
		new BMessage(kMsgAutoconfig));
	fOAuthButton = new BButton(B_TRANSLATE("Sign In" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOAuth));
	fSaveButton = new BButton(B_TRANSLATE("Save"), new BMessage(kMsgSave));
	SetDefaultButton(fSaveButton);

	BLayoutBuilder::Grid<> grid(this, B_USE_DEFAULT_SPACING,
		B_USE_HALF_ITEM_SPACING);
	grid.SetInsets(B_USE_WINDOW_INSETS);
	grid.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0, kRowKind, 5)
		.Add(fKindWeb)
		.Add(fKindImap)
		.Add(fKindPop3)
		.AddGlue()
		.End();
	grid.GetLayout(&fGrid);

	_AddMenuRow(fProviderField, kRowProvider);
	_AddTextRow(fAddress, kRowAddress);
	_AddTextRow(fPassword, kRowPassword);
	_AddTextRow(fUrl, kRowUrl);
	_AddTextRow(fIdentityName, kRowName);
	_AddTextRow(fSignature, kRowSignature);
	_AddServerRow(fImapHost, fImapPort, fImapTls, kRowImap);
	_AddServerRow(fPop3Host, fPop3Port, fPop3Tls, kRowPop3);
	_AddServerRow(fSmtpHost, fSmtpPort, fSmtpTls, kRowSmtp);
	fGrid->AddView(fUseIdle, 1, kRowIdle, 4);
	fRowViews[kRowIdle].push_back(fUseIdle);
	_AddMenuRow(fOauthProviderField, kRowOAuth);
	_AddTextRow(fClientId, kRowClientId);

	grid.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0, kRowButtons, 5)
		.Add(fAutoconfigButton)
		.Add(fOAuthButton)
		.AddGlue()
		.Add(fSaveButton)
		.End();

	fKindWeb->SetValue(B_CONTROL_ON);
	fImapTls->SetValue(B_CONTROL_ON);
	fPop3Tls->SetValue(B_CONTROL_ON);
	fSmtpTls->SetValue(B_CONTROL_ON);
	fUseIdle->SetValue(B_CONTROL_ON);
	fPassword->SetModificationMessage(new BMessage(kMsgNone));

	if (BMenuItem* first = providerMenu->ItemAt(0)) {
		const char* id = NULL;
		if (first->Message()->FindString("id", &id) == B_OK)
			_FillFromProvider(id);
	}
	_UpdateKind();

	CenterOnScreen();
}


BTextControl*
SettingsWindow::_NewField(const char* label)
{
	return new BTextControl(NULL, label, "", new BMessage(kMsgNone));
}


void
SettingsWindow::_AddTextRow(BTextControl* control, int32 row)
{
	fGrid->AddItem(control->CreateLabelLayoutItem(), 0, row);
	fGrid->AddItem(control->CreateTextViewLayoutItem(), 1, row, 4);
	fRowViews[row].push_back(control);
}


void
SettingsWindow::_AddMenuRow(BMenuField* field, int32 row)
{
	fGrid->AddItem(field->CreateLabelLayoutItem(), 0, row);
	fGrid->AddItem(field->CreateMenuBarLayoutItem(), 1, row, 4);
	fRowViews[row].push_back(field);
}


void
SettingsWindow::_AddServerRow(BTextControl* host, BTextControl* port,
	BCheckBox* tls, int32 row)
{
	fGrid->AddItem(host->CreateLabelLayoutItem(), 0, row);
	fGrid->AddItem(host->CreateTextViewLayoutItem(), 1, row);
	fGrid->AddItem(port->CreateLabelLayoutItem(), 2, row);
	fGrid->AddItem(port->CreateTextViewLayoutItem(), 3, row);
	fGrid->AddView(tls, 4, row);
	fRowViews[row].push_back(host);
	fRowViews[row].push_back(port);
	fRowViews[row].push_back(tls);
}


void
SettingsWindow::_ShowRow(int32 row, bool show)
{
	for (BView* view : fRowViews[row]) {
		if (view->IsHidden(view) == show) {
			if (show)
				view->Show();
			else
				view->Hide();
		}
	}
}


void
SettingsWindow::_UpdateKind()
{
	bool web = fKindWeb->Value() == B_CONTROL_ON;
	bool imap = fKindImap->Value() == B_CONTROL_ON;
	bool pop3 = fKindPop3->Value() == B_CONTROL_ON;

	_ShowRow(kRowProvider, web);
	_ShowRow(kRowUrl, web);
	_ShowRow(kRowImap, imap);
	_ShowRow(kRowPop3, pop3);
	_ShowRow(kRowSmtp, imap || pop3);
	_ShowRow(kRowIdle, imap);
	fAutoconfigButton->SetEnabled(!web);
	fUrl->SetEnabled(web);
}


void
SettingsWindow::_FillFromProvider(const char* providerId)
{
	const WebProvider* provider = FindWebProvider(providerId);
	if (provider == NULL) {
		fUrl->SetEnabled(true);
		return;
	}
	fUrl->SetText(provider->url);
	fUrl->SetEnabled(strcmp(providerId, "custom") == 0);
	BString oauth = provider->oauthProvider != NULL
		? provider->oauthProvider : "";
	BMenuItem* item = fOauthProviderField->Menu()->ItemAt(0);
	for (int32 i = 0; i < fOauthProviderField->Menu()->CountItems(); i++) {
		item = fOauthProviderField->Menu()->ItemAt(i);
		const char* id = NULL;
		item->Message()->FindString("oauth", &id);
		if (id != NULL && oauth == id) {
			item->SetMarked(true);
			break;
		}
	}
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgKindWeb:
		case kMsgKindImap:
		case kMsgKindPop3:
			_UpdateKind();
			break;
		case kMsgProviderPicked:
		{
			const char* id = NULL;
			if (message->FindString("id", &id) == B_OK)
				_FillFromProvider(id);
			break;
		}
		case kMsgAutoconfig:
			_RunAutoconfig();
			break;
		case kMsgOAuth:
			_RunOAuth();
			break;
		case kMsgSave:
			_Save();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
SettingsWindow::_RunAutoconfig()
{
	BString address = fAddress->Text();
	if (address.Length() == 0) {
		(new BAlert("Mail", "Enter your email address first.", "OK"))
			->Go();
		return;
	}
	BButton* button = fAutoconfigButton;
	button->SetEnabled(false);

	AutoconfigResult result;
	BString error;
	// Network lookup: worker thread, result marshalled back.
	std::thread([this, address, button]() {
		AutoconfigResult result;
		BString error;
		bool ok = ResolveAutoconfig(address.String(), result, error);
		BMessage update(kMsgAutoconfig);
		update.AddBool("ok", ok);
		if (!ok) {
			update.AddString(WEBMAIL_ERROR_FIELD, error.String());
		} else {
			if (result.hasImap) {
				update.AddString("imap:host", result.imap.host.String());
				update.AddInt32("imap:port", result.imap.port);
				update.AddBool("imap:tls", result.imap.socketType);
			}
			if (result.hasPop3) {
				update.AddString("pop3:host", result.pop3.host.String());
				update.AddInt32("pop3:port", result.pop3.port);
				update.AddBool("pop3:tls", result.pop3.socketType);
			}
			if (result.hasSmtp) {
				update.AddString("smtp:host", result.smtp.host.String());
				update.AddInt32("smtp:port", result.smtp.port);
				update.AddBool("smtp:tls", result.smtp.socketType);
			}
		}
		fTarget.SendMessage(&update);
	}).detach();
	(void)result;
	(void)error;
}


void
SettingsWindow::_RunOAuth()
{
	BString provider = SelectedProviderId(fOauthProviderField);
	if (provider.Length() == 0) {
		(new BAlert("Mail", "Pick an OAuth2 provider first.", "OK"))
			->Go();
		return;
	}
	BString clientId = fClientId->Text();
	OAuth2SetClientIdFor(provider.String(), clientId.String());
	// The consent flow posts MSG_OAUTH_FINISHED to the main window;
	// the account is saved first so the token lands on something.
	if (fAddress->Text()[0] != '\0')
		_Save();
	OAuth2RunConsentFlow(provider.String(), fTarget, "", BString());
	Close();
}


void
SettingsWindow::_Save()
{
	Account account;
	account.address = fAddress->Text();
	account.identityName = fIdentityName->Text();
	account.signature = fSignature->Text();
	account.provider = SelectedProviderId(fProviderField);
	account.oauthProvider = SelectedProviderId(fOauthProviderField);
	account.useIDLE = fUseIdle->Value() == B_CONTROL_ON;

	if (fKindWeb->Value() == B_CONTROL_ON) {
		account.kind = WEB_ACCOUNT;
		if (account.provider.Length() == 0)
			account.provider = "custom";
		account.url = fUrl->Text();
		account.name = account.provider;
		if (account.provider == "custom" && account.url.Length() > 0) {
			account.name = account.url;
		} else {
			const WebProvider* provider = FindWebProvider(
				account.provider.String());
			if (provider != NULL)
				account.name = provider->name;
		}
	} else if (fKindImap->Value() == B_CONTROL_ON) {
		account.kind = IMAP_ACCOUNT;
		account.name = account.address;
		account.imapHost = fImapHost->Text();
		account.imapPort = atoi(fImapPort->Text());
		account.imapTLS = fImapTls->Value() == B_CONTROL_ON;
		account.smtpHost = fSmtpHost->Text();
		account.smtpPort = atoi(fSmtpPort->Text());
		account.smtpTLS = fSmtpTls->Value() == B_CONTROL_ON;
		account.authMethod = account.oauthProvider.Length() > 0
			? "oauth2" : "password";
	} else {
		account.kind = POP3_ACCOUNT;
		account.name = account.address;
		account.pop3Host = fPop3Host->Text();
		account.pop3Port = atoi(fPop3Port->Text());
		account.pop3TLS = fPop3Tls->Value() == B_CONTROL_ON;
		account.smtpHost = fSmtpHost->Text();
		account.smtpPort = atoi(fSmtpPort->Text());
		account.smtpTLS = fSmtpTls->Value() == B_CONTROL_ON;
		account.authMethod = "password";
	}

	account.id = account.MakeId();
	AccountsService* accounts = DefaultAccountsService();
	if (accounts->SaveAccount(account) != B_OK) {
		(new BAlert("Mail", "Cannot save the account.", "OK"))->Go();
		return;
	}
	const char* password = fPassword->Text();
	if (password != NULL && password[0] != '\0') {
		status_t secret = accounts->SetSecret(account.id.String(),
			"password", password);
		if (secret == B_OK)
			secret = accounts->SetSecret(account.id.String(),
				"imap-password", password);
		if (secret == B_OK)
			secret = accounts->SetSecret(account.id.String(),
				"smtp-password", password);
		if (secret != B_OK) {
			(new BAlert("Mail",
				"The key store refused to store the password.",
				"OK"))->Go();
			return;
		}
	}

	BMessage changed(MSG_ACCOUNTS_CHANGED);
	fTarget.SendMessage(&changed);
	Close();
}
