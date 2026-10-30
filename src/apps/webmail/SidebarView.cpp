/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SidebarView.h"

#include <ScrollView.h>
#include <String.h>
#include <vector>


AccountItem::AccountItem(const Account& account)
	:
	BStringItem(account.name.String()),
	fAccount(account),
	fUnread(0)
{
}


void
AccountItem::SetUnread(int32 unread)
{
	if (fUnread == unread)
		return;
	fUnread = unread;
	BString label = fAccount.name;
	if (fUnread > 0)
		label << "  (" << fUnread << ")";
	SetText(label.String());
}


SidebarView::SidebarView()
	:
	BView("sidebar", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	fList = new BListView("accounts", B_SINGLE_SELECTION_LIST);
	fScroll = new BScrollView("accounts-scroll", fList, 0, true, true,
		B_NO_BORDER);
	AddChild(fScroll);
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fList->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
SidebarView::SetAccounts(const std::vector<Account>& accounts)
{
	fList->MakeEmpty();
	for (int32 i = 0; i < accounts.size(); i++)
		fList->AddItem(new AccountItem(accounts[i]));
}


void
SidebarView::SetUnread(const char* accountId, int32 unread)
{
	for (int32 i = 0; i < fList->CountItems(); i++) {
		AccountItem* item = (AccountItem*)fList->ItemAt(i);
		if (item == NULL || strcmp(item->GetAccount().id.String(),
			accountId) != 0) {
			continue;
		}
		item->SetUnread(unread);
		fList->InvalidateItem(i);
		break;
	}
}


AccountItem*
SidebarView::SelectedItem() const
{
	// CurrentSelection() is a list index, not a pointer; with no
	// selection it is -1, which must not be handed out as an item.
	int32 index = fList->CurrentSelection();
	if (index < 0)
		return NULL;
	return (AccountItem*)fList->ItemAt(index);
}


int32
SidebarView::CountAccounts() const
{
	return fList->CountItems();
}


AccountItem*
SidebarView::ItemAt(int32 index) const
{
	return (AccountItem*)fList->ItemAt(index);
}
