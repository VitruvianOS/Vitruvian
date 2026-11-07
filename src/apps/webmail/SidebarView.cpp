/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SidebarView.h"

#include <utility>

#include <LayoutBuilder.h>
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
	// The scroll view needs a layout to manage it, or it stays 1x1.
	fScroll = new BScrollView("accounts-scroll", fList, 0, false, true,
		B_NO_BORDER);
	SetExplicitMinSize(BSize(150, B_SIZE_UNSET));
	SetExplicitMaxSize(BSize(220, B_SIZE_UNSET));
	SetExplicitPreferredSize(BSize(190, B_SIZE_UNSET));
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fScroll);
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fList->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
SidebarView::SetAccounts(const std::vector<Account>& accounts)
{
	// Keep the selection and the unread counts across a rebuild.
	BString selected;
	std::vector<std::pair<BString, int32> > unread;
	for (int32 i = 0; i < fList->CountItems(); i++) {
		AccountItem* item = (AccountItem*)fList->ItemAt(i);
		unread.push_back(std::make_pair(item->GetAccount().id,
			item->Unread()));
	}
	if (SelectedItem() != NULL)
		selected = SelectedItem()->GetAccount().id;

	fList->MakeEmpty();
	for (int32 i = 0; i < (int32)accounts.size(); i++) {
		AccountItem* item = new AccountItem(accounts[i]);
		for (size_t j = 0; j < unread.size(); j++) {
			if (unread[j].first == accounts[i].id)
				item->SetUnread(unread[j].second);
		}
		fList->AddItem(item);
		if (accounts[i].id == selected)
			fList->Select(i);
	}
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


void
SidebarView::SelectFirst()
{
	if (fList->CountItems() > 0)
		fList->Select(0);
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
