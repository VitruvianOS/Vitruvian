/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_MAIN_WINDOW_H
#define WEB_MAIL_MAIN_WINDOW_H


#include "Accounts.h"

#include <String.h>
#include <Window.h>

#include <vector>


class BMenuBar;
class BGroupLayout;
class BSeparatorView;
class BView;
class EmptyStateView;
class MailView;
class SidebarView;
class SettingsWindow;


// One window: account sidebar on the left, the account's mail view on
// the right. Web accounts run their provider's own webmail in a
// per-account persistent session; native accounts run the bundled UI
// over vos-mail: backed by IMAP/POP3/SMTP.
class MainWindow : public BWindow {
public:
						MainWindow(BRect frame);
	virtual				~MainWindow();

	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();

			void		ShowSettings();

private:
			void		_BuildMenus();
			void		_ReloadAccounts();
			void		_AddAccount();
			void		_DropView(const char* accountId);
			void		_SelectAccount(const char* accountId);
			void		_SwitchTo(BView* view);
			void		_UpdateSidebarUnread(const char* accountId,
								int32 unread);
			void		_NotifyNewMail(const char* accountId,
								const char* subject);
			void		_HandleMailto(const char* url);
			void		_HandleOAuthFinished(BMessage* message);
			MailView*	_CurrentView() const;

	BMenuBar*			fMenuBar;
	SidebarView*		fSidebar;
			BSeparatorView*		fSidebarSeparator;
	BGroupLayout*		fContent;
	EmptyStateView*		fEmptyState;
	std::vector<MailView*>	fViews;
	AccountsService*	fAccounts;
	SettingsWindow*		fSettingsWindow;
};


#endif // WEB_MAIL_MAIN_WINDOW_H
