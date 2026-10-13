/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AccountsWindow.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Alert.h>
#include <Button.h>
#include <ListView.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <SupportDefs.h>
#include <Url.h>

#include <AccountProvider.h>
#include <AccountRoster.h>
#include <AccountsDefs.h>


enum {
	msg_add_account		= 'LAdd',
	msg_remove_account	= 'LDel',
	msg_edit_account	= 'LEdt',
	msg_request_token	= 'LTok',
	msg_selection		= 'LSel',
	msg_open_url		= 'OpnU'
};


static void
_GenerateId(BString& id)
{
	uint8 bytes[8];
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		read(fd, bytes, sizeof(bytes));
		close(fd);
	} else
		memset(bytes, 0, sizeof(bytes));

	id = "ac";
	for (size_t i = 0; i < sizeof(bytes); i++)
		id << BString().SetToFormat("%02x", bytes[i]).String();
}


AccountsWindow::AccountsWindow()
	:
	BWindow(BRect(80, 80, 640, 500), "Accounts", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE)
{
	BView* parent = new BView(Bounds(), "parent", B_FOLLOW_ALL, 0);
	parent->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	AddChild(parent);

	fList = new BListView(BRect(10, 30, 550, 330), "accounts",
		B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL);
	fList->SetSelectionMessage(new BMessage(msg_selection));

	BScrollView* scroll = new BScrollView("scroll", fList, B_FOLLOW_ALL, 0,
		false, true);
	parent->AddChild(scroll);

	fStatus = new BStringView(BRect(10, 385, 550, 405), "status", "",
		B_FOLLOW_BOTTOM);
	parent->AddChild(fStatus);

	fRemoveButton = new BButton(BRect(120, 345, 230, 370), "remove", "Remove",
		new BMessage(msg_remove_account), B_FOLLOW_BOTTOM);
	fRemoveButton->SetEnabled(false);
	parent->AddChild(fRemoveButton);

	fEditButton = new BButton(BRect(240, 345, 350, 370), "edit", "Edit...",
		new BMessage(msg_edit_account), B_FOLLOW_BOTTOM);
	fEditButton->SetEnabled(false);
	parent->AddChild(fEditButton);

	fTokenButton = new BButton(BRect(360, 345, 470, 370), "token", "Get Token",
		new BMessage(msg_request_token), B_FOLLOW_BOTTOM);
	fTokenButton->SetEnabled(false);
	parent->AddChild(fTokenButton);

	BButton* addButton = new BButton(BRect(10, 345, 110, 370), "add", "Add...",
		new BMessage(msg_add_account), B_FOLLOW_BOTTOM);
	parent->AddChild(addButton);

	BAccountRoster* roster = BAccountRoster::Default();
	if (roster != NULL)
		roster->StartWatching(BMessenger(this));

	_Refresh();
}


AccountsWindow::~AccountsWindow()
{
	BAccountRoster* roster = BAccountRoster::Default();
	if (roster != NULL)
		roster->StopWatching(BMessenger(this));
}


void
AccountsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case msg_add_account:
			_AddAccount();
			break;

		case msg_remove_account:
			_RemoveAccount();
			break;

		case msg_edit_account:
			_EditAccount();
			break;

		case msg_request_token:
			_RequestToken();
			break;

		case msg_selection:
		{
			bool hasSelection = fList->CurrentSelection() >= 0;
			fRemoveButton->SetEnabled(hasSelection);
			fEditButton->SetEnabled(hasSelection);
			fTokenButton->SetEnabled(hasSelection);
			break;
		}

		case msg_open_url:
		{
			const char* url = NULL;
			if (message->FindString(kAccountsUrlField, &url) == B_OK) {
				BUrl target;
				target.SetUrlString(url, true);
				target.OpenWithPreferredApplication();
			}
			break;
		}

		case kAccountAdded:
		case kAccountRemoved:
		case kAccountChanged:
			_Refresh();
			break;

		case kAccountEditResult:
		{
			int32 status = B_ERROR;
			message->FindInt32(kAccountsStatusField, &status);
			if (status != B_OK)
				fStatus->SetText("Setup failed");
			_Refresh();
			break;
		}

		case kAccountConsentRequest:
		{
			const char* url = NULL;
			if (message->FindString(kAccountsUrlField, &url) != B_OK)
				break;

			BString text("Open this page in a browser to authorize:\n");
			text << url;

			BAlert* alert = new BAlert("Consent", text.String(), "Open",
				"Close", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(1, B_ESCAPE);
			if (alert->Go() == 0) {
				BMessage open(msg_open_url);
				open.AddString(kAccountsUrlField, url);
				PostMessage(&open);
			}
			break;
		}

		case kAccountTokenResult:
		{
			int32 status = B_ERROR;
			message->FindInt32(kAccountsStatusField, &status);
			if (status != B_OK) {
				const char* reason = NULL;
				message->FindString(kAccountsReasonField, &reason);
				BString text("Token failed");
				if (reason != NULL)
					text << ": " << reason;
				fStatus->SetText(text.String());
				break;
			}

			const char* token = NULL;
			message->FindString(kAccountsTokenField, &token);
			BString text("Token: ");
			if (token != NULL && token[0] != '\0') {
				text << BString().SetToFormat("%.16s...", token);
			} else
				text << "(empty)";
			fStatus->SetText(text.String());
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
AccountsWindow::_AddAccount()
{
	BAccountRoster* roster = BAccountRoster::Default();
	if (roster == NULL) {
		fStatus->SetText("Accounts are unavailable");
		return;
	}

	BObjectList<BAccountProvider> providers;
	roster->GetProviders(providers);
	if (providers.CountItems() == 0) {
		fStatus->SetText("No provider add-ons installed");
		return;
	}

	BPopUpMenu menu("provider");
	for (int32 i = 0; i < providers.CountItems(); i++) {
		menu.AddItem(new BMenuItem(providers.ItemAt(i)->DisplayName(),
			new BMessage(msg_add_account)));
	}

	BMenuItem* selected = menu.Go(BPoint(120, 400));
	if (selected == NULL)
		return;

	BAccountProvider* provider = NULL;
	for (int32 i = 0; i < providers.CountItems(); i++) {
		if (strcmp(providers.ItemAt(i)->DisplayName(),
				selected->Label()) == 0) {
			provider = providers.ItemAt(i);
			break;
		}
	}

	if (provider == NULL)
		return;

	BAccount account;
	BString id;
	_GenerateId(id);
	account.SetId(id.String());
	account.SetProviderId(provider->Id());

	status_t status = provider->EditAccount(account, BMessenger(this));
	if (status != B_OK)
		fStatus->SetText("Setup was cancelled");
}


void
AccountsWindow::_RemoveAccount()
{
	BAccount* account = _SelectedAccount();
	if (account == NULL)
		return;

	BAccountRoster* roster = BAccountRoster::Default();
	if (roster == NULL)
		return;

	BAccountProvider* provider = NULL;
	if (roster->GetProvider(provider, account->ProviderId()) == B_OK)
		provider->RemoveAccount(*account);

	roster->RemoveAccount(account->Id());
	_Refresh();
}


void
AccountsWindow::_EditAccount()
{
	BAccount* account = _SelectedAccount();
	if (account == NULL)
		return;

	BAccountRoster* roster = BAccountRoster::Default();
	if (roster == NULL)
		return;

	BAccountProvider* provider = NULL;
	if (roster->GetProvider(provider, account->ProviderId()) != B_OK) {
		fStatus->SetText("Provider add-on not installed");
		return;
	}

	provider->EditAccount(*account, BMessenger(this));
}


void
AccountsWindow::_RequestToken()
{
	BAccount* account = _SelectedAccount();
	if (account == NULL)
		return;

	fStatus->SetText("Requesting token...");
	account->GetTokenAsync(BMessenger(this));
}


void
AccountsWindow::_Refresh()
{
	fList->MakeEmpty();
	fAccounts.MakeEmpty();

	BAccountRoster* roster = BAccountRoster::Default();
	if (roster == NULL)
		return;

	BAccount account;
	uint32 cookie = 0;
	while (roster->GetNextAccount(cookie, account) == B_OK) {
		fAccounts.AddItem(new BAccount(account));

		BString text(account.Label());
		if (text.Length() == 0)
			text = account.Id();
		text << " (" << account.ProviderId() << ")";
		fList->AddItem(new BStringItem(text.String()));
	}

	BAccount* selected = _SelectedAccount();
	bool hasSelection = selected != NULL;
	fRemoveButton->SetEnabled(hasSelection);
	fEditButton->SetEnabled(hasSelection);
	fTokenButton->SetEnabled(hasSelection);
}


BAccount*
AccountsWindow::_SelectedAccount()
{
	int32 index = fList->CurrentSelection();
	if (index < 0)
		return NULL;

	return fAccounts.ItemAt(index);
}
