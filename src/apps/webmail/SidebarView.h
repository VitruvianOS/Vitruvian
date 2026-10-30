/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WEB_MAIL_SIDEBAR_VIEW_H
#define WEB_MAIL_SIDEBAR_VIEW_H


#include "Accounts.h"

#include <ListView.h>
#include <StringItem.h>
#include <View.h>

#include <vector>


class BScrollView;


// One row per account with its unread count; selection drives the
// content view. The list only talks to its window through messages.
class AccountItem : public BStringItem {
public:
						AccountItem(const Account& account);

			const Account&	GetAccount() const { return fAccount; }
			void		SetUnread(int32 unread);
			int32		Unread() const { return fUnread; }

private:
			Account		fAccount;
			int32		fUnread;
};


class SidebarView : public BView {
public:
						SidebarView();

			void		SetAccounts(const std::vector<Account>& accounts);
			void		SetUnread(const char* accountId, int32 unread);
			AccountItem*	SelectedItem() const;
			void		SelectFirst();
			int32		CountAccounts() const;
			AccountItem*	ItemAt(int32 index) const;

private:
		BListView*		fList;
		BScrollView*	fScroll;
};


#endif // WEB_MAIL_SIDEBAR_VIEW_H
