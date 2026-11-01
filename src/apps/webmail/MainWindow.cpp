/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "WebMailStrings.h"
#include "MainWindow.h"
#include "MailView.h"
#include "OAuth2.h"
#include <MailProviders.h>
#include "SettingsWindow.h"
#include "SidebarView.h"
#include "WebMailApp.h"
#include "WebMailMessages.h"

#include <Alert.h>
#include <Application.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <GroupLayout.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <Notification.h>
#include <Path.h>
#include <Roster.h>
#include <ScrollView.h>
#include <String.h>
#include <View.h>
#include <WebView.h>
#include <WebDefs.h>

#include <cstdio>
#include <vector>


namespace {


const uint32 kMsgCompose = 'WmC1';


} // namespace


MainWindow::MainWindow(BRect frame)
	:
	BWindow(frame, "Mail", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fMenuBar(NULL),
	fSidebar(NULL),
	fContent(NULL),
	fAccounts(DefaultAccountsService()),
	fSettingsWindow(NULL)
{
	fMenuBar = new BMenuBar("menubar");
	_BuildMenus();

	fSidebar = new SidebarView();
	fContent = new BGroupLayout(B_VERTICAL);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddSplit(B_HORIZONTAL, 0.0f)
			.Add(fSidebar, 0.25f)
			.AddGroup(B_VERTICAL, 0)
				.Add(fContent, 1.0f)
			.End()
		.End();

	_ReloadAccounts();

	if (fSidebar->CountAccounts() > 0) {
		AccountItem* item = fSidebar->ItemAt(0);
		_SelectAccount(item->GetAccount().id.String());
	}
}


void
MainWindow::_BuildMenus()
{
	BMenu* fileMenu = new BMenu("File");
	fileMenu->AddItem(new BMenuItem("Add Account" B_UTF8_ELLIPSIS,
		new BMessage(MSG_ADD_ACCOUNT), 'N'));
	fileMenu->AddItem(new BMenuItem("Remove Account",
		new BMessage(MSG_REMOVE_ACCOUNT)));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("Compose",
		new BMessage(kMsgCompose), 'M'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("Quit",
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	fMenuBar->AddItem(fileMenu);

	BMenu* accountMenu = new BMenu("Account");
	accountMenu->AddItem(new BMenuItem("Check Mail",
		new BMessage(MSG_NEW_MAIL), 'Y'));
	accountMenu->AddItem(new BMenuItem("Sign In (OAuth2)" B_UTF8_ELLIPSIS,
		new BMessage(MSG_OAUTH_FINISHED)));
	fMenuBar->AddItem(accountMenu);

	BMenu* helpMenu = new BMenu("Help");
	helpMenu->AddItem(new BMenuItem("About Mail",
		new BMessage(B_ABOUT_REQUESTED)));
	fMenuBar->AddItem(helpMenu);
}


void
MainWindow::_ReloadAccounts()
{
	std::vector<Account> accounts;
	fAccounts->GetAccounts(accounts);
	fSidebar->SetAccounts(accounts);
}


MailView*
MainWindow::_CurrentView() const
{
	AccountItem* item = fSidebar->SelectedItem();
	if (item == NULL)
		return NULL;
	for (int32 i = 0; i < (int32)fViews.size(); i++) {
		if (fViews[i]->GetAccount().id == item->GetAccount().id)
			return fViews[i];
	}
	return NULL;
}


void
MainWindow::_SwitchTo(MailView* view)
{
	// Remove the current child, keep the views alive for reuse.
	while (fContent->CountItems() > 0)
		fContent->RemoveItem((int32)0);
	if (view->Parent() == NULL)
		fContent->AddView(view, 1.0f);
	view->Show();
	Activate();
}


void
MainWindow::_SelectAccount(const char* accountId)
{
	if (accountId == NULL)
		return;

	for (int32 i = 0; i < (int32)fViews.size(); i++) {
		if (fViews[i]->GetAccount().id == accountId) {
			_SwitchTo(fViews[i]);
			return;
		}
	}

	Account account;
	if (fAccounts->LoadAccount(accountId, account) != B_OK) {
		BString error = BString("Cannot load account ") << accountId;
		(new BAlert("Mail", error.String(), "OK"))->Go();
		return;
	}

	MailView* view = new MailView(account);
	fViews.push_back(view);
	_SwitchTo(view);
}


void
MainWindow::_UpdateSidebarUnread(const char* accountId, int32 unread)
{
	fSidebar->SetUnread(accountId, unread);
}


void
MainWindow::_NotifyNewMail(const char* accountId, const char* subject)
{
	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup(BString("webmail:") << accountId);
	notification.SetTitle("New mail");
	BString content = subject != NULL && subject[0] != '\0'
		? subject : "You have new mail";
	notification.SetContent(content);
	notification.SetOnClickApp("application/x-vnd.vos-webmail");
	notification.Send();
}


void
MainWindow::_HandleMailto(const char* url)
{
	MailView* view = _CurrentView();
	if (view == NULL) {
		(new BAlert("Mail", "Select an account first.", "OK"))->Go();
		return;
	}

	// mailto:user@host?subject=...&body=...
	BString rest(url + 7);
	BString to = rest;
	BString subject;
	BString body;
	int32 query = rest.FindFirst('?');
	if (query != B_ERROR) {
		to = StringLeft(rest, query);
		BString params = StringMid(rest, query + 1);
		int32 subjectAt = params.IFindFirst("subject=");
		if (subjectAt >= 0) {
			int32 subjectEnd = params.FindFirst('&', subjectAt);
			subject = StringMid(params, subjectAt + 8,
				subjectEnd != B_ERROR
					? subjectEnd - subjectAt - 8 : -1);
		}
		int32 bodyAt = params.IFindFirst("body=");
		if (bodyAt >= 0)
			body = StringMid(params, bodyAt + 5);
	}
	view->Compose(to, subject, body);
}


void
MainWindow::_HandleOAuthFinished(BMessage* message)
{
	const char* accountId = NULL;
	message->FindString(WEBMAIL_ACCOUNT_ID_FIELD, &accountId);
	const char* error = NULL;
	if (message->FindString(WEBMAIL_ERROR_FIELD, &error) == B_OK
		&& error != NULL) {
		BString text = BString("Sign-in failed: ") << error;
		(new BAlert("Mail", text.String(), "OK"))->Go();
		return;
	}

	const char* accessToken = NULL;
	message->FindString(WEBMAIL_CODE_FIELD, &accessToken);
	const char* refreshToken = NULL;
	message->FindString("webmail:refresh", &refreshToken);
	if (accountId == NULL || accessToken == NULL)
		return;

	// Persist the refresh token; the session picks the access token
	// up through SetAccessToken.
	if (refreshToken != NULL && refreshToken[0] != '\0') {
		Account account;
		if (fAccounts->LoadAccount(accountId, account) == B_OK) {
			// The token blob is JSON so a future keyring-backed
			// store can move it wholesale.
			BString blob;
			blob << "{\"access_token\":\"" << accessToken
				<< "\",\"refresh_token\":\"" << refreshToken << "\"}";
			fAccounts->SetTokenBlob(accountId, blob.String());
		}
	}
	for (int32 i = 0; i < (int32)fViews.size(); i++) {
		if (fViews[i]->GetAccount().id == accountId)
			fViews[i]->CheckMail();
	}
}


void
MainWindow::ShowSettings()
{
	if (fSettingsWindow == NULL || fSettingsWindow->Lock() == false) {
		fSettingsWindow = new SettingsWindow(BMessenger(this));
		fSettingsWindow->Show();
	} else {
		fSettingsWindow->Activate();
		fSettingsWindow->Unlock();
	}
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_ACCOUNT_SELECTED:
		{
			const char* accountId = NULL;
			message->FindString(WEBMAIL_ACCOUNT_ID_FIELD, &accountId);
			_SelectAccount(accountId);
			break;
		}
		case MSG_ACCOUNTS_CHANGED:
			_ReloadAccounts();
			break;
		case MSG_ADD_ACCOUNT:
			ShowSettings();
			break;
		case MSG_REMOVE_ACCOUNT:
		{
			AccountItem* item = fSidebar->SelectedItem();
			if (item == NULL)
				break;
			BString id = item->GetAccount().id;
			fAccounts->DeleteAccount(id.String());
			for (int32 i = 0; i < (int32)fViews.size(); i++) {
				if (fViews[i]->GetAccount().id == id) {
					fContent->RemoveView(fViews[i]);
					delete fViews[i];
					fViews.erase(fViews.begin() + i);
					break;
				}
			}
			_ReloadAccounts();
			break;
		}
		case MSG_ACCOUNT_UNREAD:
		{
			const char* accountId = NULL;
			message->FindString(WEBMAIL_ACCOUNT_ID_FIELD, &accountId);
			int32 unread = 0;
			message->FindInt32(WEBMAIL_UNREAD_FIELD, &unread);
			_UpdateSidebarUnread(accountId, unread);
			break;
		}
		case MSG_NEW_MAIL:
		{
			MailView* view = _CurrentView();
			if (view != NULL)
				view->CheckMail();
			break;
		}
		case kMsgCompose:
		{
			MailView* view = _CurrentView();
			if (view != NULL)
				view->Compose();
			break;
		}
		case MSG_OPEN_MAILTO:
		{
			const char* url = NULL;
			message->FindString(WEBMAIL_URL_FIELD, &url);
			_HandleMailto(url);
			break;
		}
		case MSG_OAUTH_FINISHED:
		{
			// Both the menu command (empty account) and the consent
			// flow land here; the consent result carries the fields.
			if (message->HasString(WEBMAIL_ACCOUNT_ID_FIELD))
				_HandleOAuthFinished(message);
			else {
				AccountItem* item = fSidebar->SelectedItem();
				if (item != NULL) {
					BString provider = item->GetAccount().oauthProvider;
					if (provider.Length() == 0)
						provider = item->GetAccount().provider;
					BString tokens;
					fAccounts->GetTokenBlob(item->GetAccount().id.String(),
						tokens);
					int32 at = tokens.IFindFirst("\"refresh_token\":\"");
					BString refreshToken;
					if (at >= 0)
						refreshToken = StringMid(tokens, at + 17,
							tokens.FindLast('"') - at - 17);
					OAuth2RunConsentFlow(provider.String(),
						BMessenger(this),
						item->GetAccount().id.String(), refreshToken);
				}
			}
			break;
		}
		case MSG_NATIVE_EVENT:
		{
			const char* accountId = NULL;
			message->FindString(WEBMAIL_ACCOUNT_ID_FIELD, &accountId);
			for (int32 i = 0; i < (int32)fViews.size(); i++) {
				if (fViews[i]->GetAccount().id == accountId) {
					fViews[i]->HandleNativeEvent(message);
					break;
				}
			}
			break;
		}
		case B_WEB_SCRIPT_MESSAGE:
		{
			// The handler name tells us which view posted; the
			// webmail handler is per-view, so route by the current
			// view (one native view at a time is the v1 rule) and by
			// URL for web views.
			const char* handler = NULL;
			message->FindString(B_WEB_HANDLER_FIELD, &handler);
			if (handler != NULL && strcmp(handler, "webmail") == 0) {
				MailView* view = _CurrentView();
				if (view != NULL)
					view->HandleScriptMessage(message);
			}
			break;
		}
		case B_WEB_PAGE_NEW_WINDOW:
		{
			const char* url = NULL;
			message->FindString(B_WEB_URL_FIELD, &url);
			if (url == NULL)
				break;
			if (strncmp(url, "mailto:", 7) == 0) {
				_HandleMailto(url);
				break;
			}
			// External links open in Net+.
			int32 argc = 1;
			const char* argv[] = { url };
			be_roster->Launch("application/x-vnd.Vitruvian-NetPlus",
				argc, argv);
			break;
		}
		case MSG_COMPOSE_REQUESTED:
		{
			// Attachment picker for the native compose form.
			MailView* view = _CurrentView();
			if (view == NULL)
				break;
			BFilePanel* panel = new BFilePanel(B_OPEN_PANEL,
				new BMessenger(this), NULL, B_FILE_NODE, false);
			panel->Show();
			break;
		}
		case B_REFS_RECEIVED:
		{
			// The save/open panel answer for attachments.
			entry_ref ref;
			if (message->FindRef("refs", 0, &ref) == B_OK) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK) {
					MailView* view = _CurrentView();
					if (view != NULL)
						view->AttachPath(path.Path());
				}
			}
			break;
		}
		case B_WEB_NOTIFICATION_ASK:
		{
			// Web notifications from webmail pages: allow them, they
			// are how web providers surface new mail.
			BMessage reply(*message);
			reply.AddBool(B_WEB_PERMISSION_ALLOWED, true);
			message->SendReply(&reply);
			break;
		}
		case B_ABOUT_REQUESTED:
		{
			BAlert* alert = new BAlert("About Mail",
				"Mail 0.1\nWebMail for V\\OS\n"
				"WPE WebKit + libetpan",
				"OK");
			alert->Go();
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
MainWindow::QuitRequested()
{
	for (int32 i = 0; i < (int32)fViews.size(); i++)
		delete fViews[i];
	fViews.clear();
	WebMailApp* app = static_cast<WebMailApp*>(be_app);
	app->WindowClosed(this);
	return true;
}
