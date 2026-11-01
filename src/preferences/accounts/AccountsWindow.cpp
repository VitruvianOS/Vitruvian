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
#include <LayoutBuilder.h>
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

#include "AccountsPreflet.h"
#include "MailAccountWindow.h"


enum {
	msg_add_account		= 'LAdd',
	msg_remove_account	= 'LDel',
	msg_edit_account	= 'LEdt',
	msg_request_token	= 'LTok',
	msg_selection		= 'LSel',
	msg_open_url		= 'OpnU',
	msg_add_mail		= 'LAml'
};


static void
_GenerateId(BString& id)
{
	uint8 bytes[8];
	int fd = open("/dev/urandom", O_RDONLY);
	ssize_t got = -1;
	if (fd >= 0) {
		got = read(fd, bytes, sizeof(bytes));
		close(fd);
	}
	if (got != (ssize_t)sizeof(bytes))
		memset(bytes, 0, sizeof(bytes));

	id = "ac";
	for (size_t i = 0; i < sizeof(bytes); i++)
		id << BString().SetToFormat("%02x", bytes[i]).String();
}


// List that shows a hint while it has no rows.
class HintListView : public BListView {
public:
	HintListView(const char* name, const char* hint)
		:
		BListView(name, B_SINGLE_SELECTION_LIST),
		fHint(hint)
	{
	}

	virtual void Draw(BRect updateRect)
	{
		BListView::Draw(updateRect);
		if (CountItems() > 0)
			return;

		SetHighColor(tint_color(ui_color(B_PANEL_TEXT_COLOR),
			B_LIGHTEN_1_TINT));
		font_height height;
		GetFontHeight(&height);
		BRect bounds = Bounds();
		float x = (bounds.Width() - StringWidth(fHint.String())) / 2;
		DrawString(fHint.String(), BPoint(max_c(x, 4),
			bounds.Height() / 2 + height.ascent / 2));
	}

private:
	BString	fHint;
};


AccountsWindow::AccountsWindow()
	:
	BWindow(BRect(0, 0, 560, 420), "Accounts", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE)
{
	fList = new HintListView("accounts",
		"No accounts yet. Use Add to create one.");
	fList->SetSelectionMessage(new BMessage(msg_selection));

	BScrollView* scroll = new BScrollView("scroll", fList, 0, false, true,
		B_FANCY_BORDER);
	scroll->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		be_plain_font->Size() * 15));

	fStatus = new BStringView("status", "");
	fStatus->Hide();

	fAddButton = new BButton("add", "Add" B_UTF8_ELLIPSIS,
		new BMessage(msg_add_account));
	fRemoveButton = new BButton("remove", "Remove",
		new BMessage(msg_remove_account));
	fRemoveButton->SetEnabled(false);
	fEditButton = new BButton("edit", "Edit" B_UTF8_ELLIPSIS,
		new BMessage(msg_edit_account));
	fEditButton->SetEnabled(false);
	fList->SetInvocationMessage(new BMessage(msg_edit_account));
	fTokenButton = new BButton("token", "Get token",
		new BMessage(msg_request_token));
	fTokenButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fAddButton)
			.Add(fRemoveButton)
			.Add(fEditButton)
			.Add(fTokenButton)
			.AddGlue()
		.End()
		.Add(fStatus);

	float unit = be_plain_font->Size();
	ResizeTo(unit * 34, unit * 28);
	CenterOnScreen();

	BAccountRoster* roster = BAccountRoster::Default();
	if (roster != NULL)
		roster->StartWatching(BMessenger(this));

	_Refresh();
}


void
AccountsWindow::_SetStatus(const char* text)
{
	fStatus->SetText(text);
	bool empty = text == NULL || text[0] == '\0';
	if (empty != fStatus->IsHidden(fStatus)) {
		if (empty)
			fStatus->Hide();
		else
			fStatus->Show();
	}
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
			_UpdateButtons();
			break;

		case msg_add_mail:
		case kMsgAccountsAdd:
			if (message->what == msg_add_mail
				|| strcmp(message->GetString(kAccountsTypeField, ""),
					kAccountTypeMail) == 0) {
				Activate();
				(new MailAccountWindow(BMessenger(this)))->Show();
			}
			break;

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
				_SetStatus("Setup failed");
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
				_SetStatus(text.String());
				break;
			}

			const char* token = NULL;
			message->FindString(kAccountsTokenField, &token);
			BString text("Token: ");
			if (token != NULL && token[0] != '\0') {
				text << BString().SetToFormat("%.16s...", token);
			} else
				text << "(empty)";
			_SetStatus(text.String());
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
		_SetStatus("Accounts are unavailable");
		return;
	}

	BObjectList<BAccountProvider> providers;
	roster->GetProviders(providers);

	BPopUpMenu menu("type");
	menu.AddItem(new BMenuItem("Mail", new BMessage(msg_add_mail)));
	if (providers.CountItems() > 0)
		menu.AddSeparatorItem();
	for (int32 i = 0; i < providers.CountItems(); i++) {
		menu.AddItem(new BMenuItem(providers.ItemAt(i)->DisplayName(),
			new BMessage(msg_add_account)));
	}

	BMenuItem* selected = menu.Go(fAddButton->ConvertToScreen(
		BPoint(0, fAddButton->Bounds().Height())));
	if (selected == NULL)
		return;

	if (selected->Message()->what == msg_add_mail) {
		PostMessage(msg_add_mail);
		return;
	}

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
		_SetStatus("Setup was cancelled");
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

	if (MailAccountWindow::IsMailAccount(*account)) {
		BString text("Remove the account ");
		text << account->UserName() << " and its stored passwords?";
		BAlert* alert = new BAlert("Remove", text.String(), "Cancel",
			"Remove", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() != 1)
			return;
		MailAccountWindow::RemoveSecrets(account->Id());
	} else {
		BAccountProvider* provider = NULL;
		if (roster->GetProvider(provider, account->ProviderId()) == B_OK)
			provider->RemoveAccount(*account);
	}

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

	if (MailAccountWindow::IsMailAccount(*account)) {
		(new MailAccountWindow(BMessenger(this), account))->Show();
		return;
	}

	BAccountProvider* provider = NULL;
	if (roster->GetProvider(provider, account->ProviderId()) != B_OK) {
		_SetStatus("Provider add-on not installed");
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

	_SetStatus("Requesting token...");
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

		BString text;
		if (MailAccountWindow::IsMailAccount(account))
			MailAccountWindow::DescribeAccount(account, text);
		else {
			text = account.Label();
			if (text.Length() == 0)
				text = account.Id();
			text << " (" << account.ProviderId() << ")";
		}
		fList->AddItem(new BStringItem(text.String()));
	}

	_UpdateButtons();
}


void
AccountsWindow::_UpdateButtons()
{
	BAccount* selected = _SelectedAccount();
	fRemoveButton->SetEnabled(selected != NULL);
	fEditButton->SetEnabled(selected != NULL);
	fTokenButton->SetEnabled(selected != NULL
		&& !MailAccountWindow::IsMailAccount(*selected));
}


BAccount*
AccountsWindow::_SelectedAccount()
{
	int32 index = fList->CurrentSelection();
	if (index < 0)
		return NULL;

	return fAccounts.ItemAt(index);
}
