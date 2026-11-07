/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MailAccountWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <thread>

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <Directory.h>
#include <GridLayout.h>
#include <Key.h>
#include <KeyStore.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <String.h>
#include <TextControl.h>

#include <AccountRoster.h>

#include <MailProviders.h>
#include <MailAutoconfig.h>


namespace {

enum {
	kRowKind = 0,
	kRowProvider,
	kRowAddress,
	kRowPassword,
	kRowUrl,
	kRowName,
	kRowImap,
	kRowPop3,
	kRowSmtp,
	kRowOAuth,
	kRowClientId,
	kRowButtons
};

const uint32 kMsgKindWeb		= 'McK1';
const uint32 kMsgKindImap		= 'McK2';
const uint32 kMsgKindPop3		= 'McK3';
const uint32 kMsgProviderPicked	= 'McPp';
const uint32 kMsgOAuthPicked	= 'McOp';
const uint32 kMsgSave			= 'McSv';
const uint32 kMsgAutoconfig		= 'McAc';
const uint32 kMsgNone			= 'Mc00';

const char* const kSecretNames[]
	= { "password", "imap-password", "smtp-password", "tokens" };


BString
SecretId(const char* id, const char* name)
{
	return BString("webmail:") << id << ":" << name;
}


status_t
SetSecret(const char* id, const char* name, const char* value)
{
	BKey key(B_KEY_PURPOSE_NETWORK, SecretId(id, name).String(), "password",
		(const uint8*)value, strlen(value));
	BKeyStore store;
	store.RemoveKey(key);
	return store.AddKey(key);
}


BString
SelectedId(BMenuField* field, const char* name)
{
	BMenuItem* item = field->Menu()->FindMarked();
	if (item == NULL)
		return BString();
	return BString(item->Message()->GetString(name, ""));
}


BString
ClientIdPath()
{
	const char* home = getenv("HOME");
	return BString(home != NULL ? home : "/tmp")
		<< "/config/settings/WebMail/oauth-ids";
}


// The client ids live in a "provider=id" file shared with WebMail.
BString
ReadClientId(const char* provider)
{
	BString result;
	FILE* file = fopen(ClientIdPath().String(), "r");
	if (file == NULL)
		return result;

	char line[512];
	size_t length = strlen(provider);
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, provider, length) != 0 || line[length] != '=')
			continue;
		result = line + length + 1;
		result.RemoveAll("\n");
		break;
	}
	fclose(file);
	return result;
}


void
WriteClientId(const char* provider, const char* clientId)
{
	std::map<BString, BString> entries;
	FILE* file = fopen(ClientIdPath().String(), "r");
	if (file != NULL) {
		char line[512];
		while (fgets(line, sizeof(line), file) != NULL) {
			BString text(line);
			text.RemoveAll("\n");
			int32 split = text.FindFirst('=');
			if (split <= 0)
				continue;
			BString name;
			text.CopyInto(name, 0, split);
			entries[name] = text.String() + split + 1;
		}
		fclose(file);
	}

	if (clientId[0] == '\0')
		entries.erase(provider);
	else
		entries[provider] = clientId;

	BString directory(ClientIdPath());
	directory.Truncate(directory.FindLast('/'));
	create_directory(directory.String(), 0755);

	file = fopen(ClientIdPath().String(), "w");
	if (file == NULL)
		return;
	for (std::map<BString, BString>::iterator it = entries.begin();
			it != entries.end(); ++it) {
		fprintf(file, "%s=%s\n", it->first.String(), it->second.String());
	}
	fclose(file);
}


BString
MakeId(const BString& provider, const BString& address)
{
	BString base = provider;
	if (base.Length() == 0)
		base = "account";
	base << "-" << address;
	if (base.Length() > 80)
		base.Truncate(80);

	BString id;
	for (int32 i = 0; i < base.Length(); i++) {
		char c = base[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '.') {
			id << c;
		} else
			id << '_';
	}
	return id;
}


void
ShowAlert(const char* text)
{
	(new BAlert("Mail account", text, "OK"))->Go();
}

} // namespace


MailAccountWindow::MailAccountWindow(const BMessenger& target,
	const BAccount* account)
	:
	BWindow(BRect(0, 0, 10, 10), account != NULL
			? "Edit mail account" : "Add mail account",
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS),
	fTarget(target),
	fEditing(account != NULL),
	fGrid(NULL)
{
	BPopUpMenu* providerMenu = new BPopUpMenu("providers");
	std::vector<WebProvider> providers;
	GetWebProviders(providers);
	for (size_t i = 0; i < providers.size(); i++) {
		BMessage* pick = new BMessage(kMsgProviderPicked);
		pick->AddString("id", providers[i].id);
		providerMenu->AddItem(new BMenuItem(providers[i].name, pick));
	}
	BMessage* custom = new BMessage(kMsgProviderPicked);
	custom->AddString("id", "custom");
	providerMenu->AddItem(new BMenuItem("Custom URL", custom));
	providerMenu->SetLabelFromMarked(true);
	if (providerMenu->ItemAt(0) != NULL)
		providerMenu->ItemAt(0)->SetMarked(true);

	BPopUpMenu* oauthMenu = new BPopUpMenu("oauth");
	const char* oauthIds[] = { "google", "microsoft", "yahoo", NULL };
	const char* oauthNames[] = { "Google", "Microsoft", "Yahoo", NULL };
	for (int32 i = 0; oauthIds[i] != NULL; i++) {
		BMessage* pick = new BMessage(kMsgOAuthPicked);
		pick->AddString("oauth", oauthIds[i]);
		oauthMenu->AddItem(new BMenuItem(oauthNames[i], pick));
	}
	BMessage* noOauth = new BMessage(kMsgOAuthPicked);
	noOauth->AddString("oauth", "");
	BMenuItem* noneItem = new BMenuItem("None (app password)", noOauth);
	oauthMenu->AddItem(noneItem);
	oauthMenu->SetLabelFromMarked(true);
	noneItem->SetMarked(true);

	fProviderField = new BMenuField("Provider:", providerMenu);
	fOauthProviderField = new BMenuField("OAuth2 provider:", oauthMenu);

	fAddress = _NewField("Address:");
	fPassword = _NewField("Password:");
	fPassword->TextView()->HideTyping(true);
	fUrl = _NewField("Webmail URL:");
	fIdentityName = _NewField("Your name:");
	fImapHost = _NewField("IMAP server:");
	fImapPort = _NewField("Port:");
	fPop3Host = _NewField("POP3 server:");
	fPop3Port = _NewField("Port:");
	fSmtpHost = _NewField("SMTP server:");
	fSmtpPort = _NewField("Port:");
	fClientId = _NewField("OAuth2 client ID:");
	fImapTls = new BCheckBox("TLS", new BMessage(kMsgNone));
	fPop3Tls = new BCheckBox("TLS", new BMessage(kMsgNone));
	fSmtpTls = new BCheckBox("TLS", new BMessage(kMsgNone));

	if (fEditing)
		fPassword->SetToolTip("Leave empty to keep the stored password.");

	float portWidth = fImapPort->StringWidth("00000") + 16;
	BTextControl* ports[] = { fImapPort, fPop3Port, fSmtpPort };
	for (size_t i = 0; i < 3; i++) {
		ports[i]->SetExplicitMaxSize(BSize(portWidth, B_SIZE_UNSET));
		ports[i]->SetExplicitMinSize(BSize(portWidth, B_SIZE_UNSET));
	}

	fKindWeb = new BRadioButton("Web account", new BMessage(kMsgKindWeb));
	fKindImap = new BRadioButton("IMAP", new BMessage(kMsgKindImap));
	fKindPop3 = new BRadioButton("POP3", new BMessage(kMsgKindPop3));
	fAutoconfigButton = new BButton("Autoconfig", new BMessage(kMsgAutoconfig));
	fSaveButton = new BButton("Save", new BMessage(kMsgSave));
	BButton* cancelButton = new BButton("Cancel",
		new BMessage(B_QUIT_REQUESTED));
	SetDefaultButton(fSaveButton);

	float buttonWidth = max_c(fSaveButton->StringWidth("Cancel"),
		fSaveButton->StringWidth("Save")) + 32;
	fSaveButton->SetExplicitMinSize(BSize(buttonWidth, B_SIZE_UNSET));
	cancelButton->SetExplicitMinSize(BSize(buttonWidth, B_SIZE_UNSET));

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
	_AddServerRow(fImapHost, fImapPort, fImapTls, kRowImap);
	_AddServerRow(fPop3Host, fPop3Port, fPop3Tls, kRowPop3);
	_AddServerRow(fSmtpHost, fSmtpPort, fSmtpTls, kRowSmtp);
	_AddMenuRow(fOauthProviderField, kRowOAuth);
	_AddTextRow(fClientId, kRowClientId);

	grid.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0, kRowButtons, 5)
		.Add(fAutoconfigButton)
		.AddGlue()
		.Add(cancelButton)
		.Add(fSaveButton)
		.End();

	fKindWeb->SetValue(B_CONTROL_ON);
	fImapTls->SetValue(B_CONTROL_ON);
	fPop3Tls->SetValue(B_CONTROL_ON);
	fSmtpTls->SetValue(B_CONTROL_ON);

	if (fEditing) {
		fAccount = *account;
		_Load(fAccount);
	} else if (providerMenu->ItemAt(0) != NULL) {
		const char* id = NULL;
		if (providerMenu->ItemAt(0)->Message()->FindString("id", &id) == B_OK)
			_FillFromProvider(id);
	}
	_UpdateKind();
	_UpdateClientId();

	CenterOnScreen();
}


bool
MailAccountWindow::IsMailAccount(const BAccount& account)
{
	bool mine = false;
	return account.GetSetting("webmail", mine) == B_OK && mine;
}


void
MailAccountWindow::DescribeAccount(const BAccount& account, BString& text)
{
	BString kind;
	account.GetSetting("kind", kind);
	const char* type = "Web";
	if (kind == "imap")
		type = "IMAP";
	else if (kind == "pop3")
		type = "POP3";

	text = account.UserName();
	if (text.Length() == 0)
		text = account.Label();
	if (text.Length() == 0)
		text = account.Id();
	text << " (Mail, " << type << ")";
}


void
MailAccountWindow::RemoveSecrets(const char* id)
{
	BKeyStore store;
	for (size_t i = 0; i < sizeof(kSecretNames) / sizeof(kSecretNames[0]);
			i++) {
		BKey key(B_KEY_PURPOSE_NETWORK, SecretId(id, kSecretNames[i]).String(),
			"password");
		store.RemoveKey(key);
	}
}


BTextControl*
MailAccountWindow::_NewField(const char* label)
{
	return new BTextControl(NULL, label, "", new BMessage(kMsgNone));
}


void
MailAccountWindow::_AddTextRow(BTextControl* control, int32 row)
{
	fGrid->AddItem(control->CreateLabelLayoutItem(), 0, row);
	fGrid->AddItem(control->CreateTextViewLayoutItem(), 1, row, 4);
	fRowViews[row].push_back(control);
}


void
MailAccountWindow::_AddMenuRow(BMenuField* field, int32 row)
{
	fGrid->AddItem(field->CreateLabelLayoutItem(), 0, row);
	fGrid->AddItem(field->CreateMenuBarLayoutItem(), 1, row, 4);
	fRowViews[row].push_back(field);
}


void
MailAccountWindow::_AddServerRow(BTextControl* host, BTextControl* port,
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
MailAccountWindow::_ShowRow(int32 row, bool show)
{
	std::vector<BView*>& views = fRowViews[row];
	for (size_t i = 0; i < views.size(); i++) {
		BView* view = views[i];
		if (view->IsHidden(view) == show) {
			if (show)
				view->Show();
			else
				view->Hide();
		}
	}
}


void
MailAccountWindow::_UpdateKind()
{
	bool web = fKindWeb->Value() == B_CONTROL_ON;
	bool imap = fKindImap->Value() == B_CONTROL_ON;
	bool pop3 = fKindPop3->Value() == B_CONTROL_ON;

	_ShowRow(kRowProvider, web);
	_ShowRow(kRowUrl, web);
	_ShowRow(kRowImap, imap);
	_ShowRow(kRowPop3, pop3);
	_ShowRow(kRowSmtp, imap || pop3);
	fAutoconfigButton->SetEnabled(!web);
	fUrl->SetEnabled(web);

	if (Lock()) {
		ResizeToPreferred();
		Unlock();
	}
}


void
MailAccountWindow::_SelectOAuth(const char* oauth)
{
	BMenu* menu = fOauthProviderField->Menu();
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (strcmp(item->Message()->GetString("oauth", ""), oauth) == 0) {
			item->SetMarked(true);
			break;
		}
	}
}


void
MailAccountWindow::_UpdateClientId()
{
	BString provider = SelectedId(fOauthProviderField, "oauth");
	fClientId->SetEnabled(provider.Length() > 0);
	fClientId->SetText(provider.Length() > 0
		? ReadClientId(provider.String()).String() : "");
}


void
MailAccountWindow::_FillFromProvider(const char* providerId)
{
	const WebProvider* provider = FindWebProvider(providerId);
	if (provider == NULL) {
		fUrl->SetEnabled(true);
		return;
	}
	fUrl->SetText(provider->url);
	fUrl->SetEnabled(strcmp(providerId, "custom") == 0);
	_SelectOAuth(provider->oauthProvider != NULL
		? provider->oauthProvider : "");
	_UpdateClientId();
}


void
MailAccountWindow::_Load(const BAccount& account)
{
	BString text;
	int32 integer = 0;
	bool flag = false;

	BMenu* menu = fProviderField->Menu();
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (strcmp(item->Message()->GetString("id", ""),
				account.ProviderId()) == 0) {
			item->SetMarked(true);
			break;
		}
	}

	fAddress->SetText(account.UserName());

	BString kind;
	account.GetSetting("kind", kind);
	fKindWeb->SetValue(B_CONTROL_OFF);
	if (kind == "imap")
		fKindImap->SetValue(B_CONTROL_ON);
	else if (kind == "pop3")
		fKindPop3->SetValue(B_CONTROL_ON);
	else
		fKindWeb->SetValue(B_CONTROL_ON);

	if (account.GetSetting("url", text) == B_OK)
		fUrl->SetText(text.String());
	fUrl->SetEnabled(strcmp(account.ProviderId(), "custom") == 0);

	if (account.GetSetting("identity_name", text) == B_OK)
		fIdentityName->SetText(text.String());

	struct {
		const char* name;
		BTextControl* host;
		BTextControl* port;
		BCheckBox* tls;
	} servers[] = {
		{ "imap", fImapHost, fImapPort, fImapTls },
		{ "pop3", fPop3Host, fPop3Port, fPop3Tls },
		{ "smtp", fSmtpHost, fSmtpPort, fSmtpTls }
	};
	for (size_t i = 0; i < 3; i++) {
		BString key;
		key << servers[i].name << "_host";
		if (account.GetSetting(key.String(), text) == B_OK)
			servers[i].host->SetText(text.String());
		key.SetTo(servers[i].name);
		key << "_port";
		if (account.GetSetting(key.String(), integer) == B_OK && integer > 0) {
			BString port;
			port << integer;
			servers[i].port->SetText(port.String());
		}
		key.SetTo(servers[i].name);
		key << "_tls";
		if (account.GetSetting(key.String(), flag) == B_OK)
			servers[i].tls->SetValue(flag ? B_CONTROL_ON : B_CONTROL_OFF);
	}

	if (account.GetSetting("oauth_provider", text) == B_OK)
		_SelectOAuth(text.String());
}


void
MailAccountWindow::MessageReceived(BMessage* message)
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

		case kMsgOAuthPicked:
			_UpdateClientId();
			break;

		case kMsgAutoconfig:
			if (message->HasBool("ok"))
				_ApplyAutoconfig(message);
			else
				_RunAutoconfig();
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
MailAccountWindow::_RunAutoconfig()
{
	BString address = fAddress->Text();
	if (address.Length() == 0) {
		ShowAlert("Enter your email address first.");
		return;
	}
	fAutoconfigButton->SetEnabled(false);

	// Network lookup: worker thread, result marshalled back.
	BMessenger self(this);
	std::thread([self, address]() {
		AutoconfigResult result;
		BString error;
		bool ok = ResolveAutoconfig(address.String(), result, error);
		BMessage update(kMsgAutoconfig);
		update.AddBool("ok", ok);
		if (!ok)
			update.AddString("error", error.String());
		else {
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
		self.SendMessage(&update);
	}).detach();
}


void
MailAccountWindow::_ApplyAutoconfig(const BMessage* message)
{
	fAutoconfigButton->SetEnabled(fKindWeb->Value() != B_CONTROL_ON);

	if (!message->GetBool("ok", false)) {
		BString text("Autoconfig failed");
		const char* error = message->GetString("error", "");
		if (error[0] != '\0')
			text << ": " << error;
		ShowAlert(text.String());
		return;
	}

	struct {
		const char* name;
		BTextControl* host;
		BTextControl* port;
		BCheckBox* tls;
	} servers[] = {
		{ "imap", fImapHost, fImapPort, fImapTls },
		{ "pop3", fPop3Host, fPop3Port, fPop3Tls },
		{ "smtp", fSmtpHost, fSmtpPort, fSmtpTls }
	};
	for (size_t i = 0; i < 3; i++) {
		BString key(servers[i].name);
		key << ":host";
		const char* host = NULL;
		if (message->FindString(key.String(), &host) != B_OK)
			continue;
		servers[i].host->SetText(host);

		key.SetTo(servers[i].name);
		key << ":port";
		BString port;
		port << message->GetInt32(key.String(), 0);
		servers[i].port->SetText(port.String());

		key.SetTo(servers[i].name);
		key << ":tls";
		servers[i].tls->SetValue(message->GetBool(key.String(), true)
			? B_CONTROL_ON : B_CONTROL_OFF);
	}
}


void
MailAccountWindow::_Save()
{
	BAccountRoster* roster = BAccountRoster::Default();
	if (roster == NULL || roster->InitCheck() != B_OK) {
		ShowAlert("Accounts are unavailable.");
		return;
	}

	bool web = fKindWeb->Value() == B_CONTROL_ON;
	bool imap = fKindImap->Value() == B_CONTROL_ON;

	BString address = fAddress->Text();
	if (!web && address.Length() == 0) {
		ShowAlert("Enter your email address first.");
		return;
	}

	BString provider = SelectedId(fProviderField, "id");
	BString oauth = SelectedId(fOauthProviderField, "oauth");

	BString label = address;
	BString kind = "pop3";
	if (web) {
		kind = "web";
		if (provider.Length() == 0)
			provider = "custom";
		label = provider;
		if (provider == "custom" && fUrl->TextView()->TextLength() > 0)
			label = fUrl->Text();
		else {
			const WebProvider* known = FindWebProvider(provider.String());
			if (known != NULL)
				label = known->name;
		}
	} else if (imap)
		kind = "imap";

	// An edited account keeps its id and every setting not shown here.
	BAccount account;
	if (fEditing)
		account = fAccount;
	else {
		account.SetId(MakeId(provider, address).String());
		account.SetSetting("signature", "");
		account.SetSetting("use_idle", true);
		account.SetSetting("leave_on_server", true);
		account.SetSetting("delete_after_days", (int32)0);
	}

	account.SetProviderId(provider.String());
	account.SetLabel(label.String());
	account.SetUserName(address.String());
	account.SetSetting("webmail", true);
	account.SetSetting("kind", kind.String());
	account.SetSetting("identity_name", fIdentityName->Text());
	account.SetSetting("oauth_provider", oauth.String());

	account.SetSetting("url", web ? fUrl->Text() : "");

	account.SetSetting("imap_host", imap ? fImapHost->Text() : "");
	account.SetSetting("imap_port", imap ? (int32)atoi(fImapPort->Text()) : 0);
	account.SetSetting("imap_tls", imap && fImapTls->Value() == B_CONTROL_ON);
	bool pop3 = fKindPop3->Value() == B_CONTROL_ON;
	account.SetSetting("pop3_host", pop3 ? fPop3Host->Text() : "");
	account.SetSetting("pop3_port", pop3 ? (int32)atoi(fPop3Port->Text()) : 0);
	account.SetSetting("pop3_tls", pop3 && fPop3Tls->Value() == B_CONTROL_ON);
	account.SetSetting("smtp_host", web ? "" : fSmtpHost->Text());
	account.SetSetting("smtp_port", web ? 0 : (int32)atoi(fSmtpPort->Text()));
	account.SetSetting("smtp_tls", !web && fSmtpTls->Value() == B_CONTROL_ON);

	const char* auth = "password";
	if (imap && oauth.Length() > 0)
		auth = "oauth2";
	account.SetSetting("auth", web ? "" : auth);

	BAccount existing;
	status_t status;
	if (roster->FindAccount(account.Id(), existing) == B_OK)
		status = roster->UpdateAccount(account);
	else
		status = roster->AddAccount(account);
	if (status != B_OK) {
		ShowAlert("Cannot save the account.");
		return;
	}

	const char* password = fPassword->Text();
	if (password != NULL && password[0] != '\0') {
		status = SetSecret(account.Id(), "password", password);
		if (status == B_OK)
			status = SetSecret(account.Id(), "imap-password", password);
		if (status == B_OK)
			status = SetSecret(account.Id(), "smtp-password", password);
		if (status != B_OK) {
			ShowAlert("The key store refused to store the password.");
			return;
		}
	}

	if (oauth.Length() > 0)
		WriteClientId(oauth.String(), fClientId->Text());

	Quit();
}
