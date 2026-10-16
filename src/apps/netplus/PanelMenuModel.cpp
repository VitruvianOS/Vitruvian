/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "PanelMenuModel.h"


PanelMenuRow::PanelMenuRow()
	:
	type(kPanelMenuRowAction),
	messageWhat(0),
	subPage(-1),
	icon(-1),
	enabled(true)
{
}


PanelMenuModel::PanelMenuModel()
{
}


int32_t
PanelMenuModel::AddPage(const char* title)
{
	Page page;
	page.title = title != NULL ? title : "";
	fPages.push_back(page);

	if (fStack.empty())
		fStack.push_back((int32_t)fPages.size() - 1);

	return (int32_t)fPages.size() - 1;
}


void
PanelMenuModel::AddActionRow(int32_t page, const char* label,
	uint32_t messageWhat, const char* shortcut, const char* payload,
	int32_t icon)
{
	if (page < 0 || page >= (int32_t)fPages.size())
		return;

	PanelMenuRow row;
	row.type = kPanelMenuRowAction;
	row.label = label != NULL ? label : "";
	row.messageWhat = messageWhat;
	if (shortcut != NULL)
		row.shortcut = shortcut;
	if (payload != NULL)
		row.payload = payload;
	row.icon = icon;
	fPages[page].rows.push_back(row);
}


void
PanelMenuModel::AddSubPageRow(int32_t page, const char* label, int32_t subPage,
	int32_t icon)
{
	if (page < 0 || page >= (int32_t)fPages.size())
		return;

	PanelMenuRow row;
	row.type = kPanelMenuRowSubPage;
	row.label = label != NULL ? label : "";
	row.subPage = subPage;
	row.icon = icon;
	fPages[page].rows.push_back(row);
}


void
PanelMenuModel::AddSeparatorRow(int32_t page)
{
	if (page < 0 || page >= (int32_t)fPages.size())
		return;

	PanelMenuRow row;
	row.type = kPanelMenuRowSeparator;
	fPages[page].rows.push_back(row);
}


void
PanelMenuModel::AddSectionLabelRow(int32_t page, const char* label)
{
	if (page < 0 || page >= (int32_t)fPages.size())
		return;

	PanelMenuRow row;
	row.type = kPanelMenuRowSectionLabel;
	row.label = label != NULL ? label : "";
	row.enabled = false;
	fPages[page].rows.push_back(row);
}


int32_t
PanelMenuModel::CurrentPage() const
{
	if (fStack.empty())
		return -1;
	return fStack.back();
}


const char*
PanelMenuModel::CurrentTitle() const
{
	int32_t page = CurrentPage();
	if (page < 0)
		return "";
	return fPages[page].title.c_str();
}


bool
PanelMenuModel::CanGoBack() const
{
	return fStack.size() > 1;
}


void
PanelMenuModel::PushPage(int32_t page)
{
	if (page < 0 || page >= (int32_t)fPages.size())
		return;

	fStack.push_back(page);
}


void
PanelMenuModel::PopPage()
{
	if (fStack.size() > 1)
		fStack.pop_back();
}


int32_t
PanelMenuModel::RowCount(int32_t page) const
{
	if (page < 0 || page >= (int32_t)fPages.size())
		return 0;
	return (int32_t)fPages[page].rows.size();
}


const PanelMenuRow&
PanelMenuModel::RowAt(int32_t page, int32_t index) const
{
	return _RowAt(page, index);
}


PanelMenuModel::SelectResult
PanelMenuModel::SelectRow(int32_t index, uint32_t* what, std::string* payload,
	int32_t* page) const
{
	int32_t current = CurrentPage();
	if (current < 0 || index < 0
		|| index >= (int32_t)fPages[current].rows.size()) {
		return kSelectNone;
	}

	const PanelMenuRow& row = fPages[current].rows[index];
	switch (row.type) {
		case kPanelMenuRowAction:
			if (!row.enabled)
				return kSelectNone;
			if (what != NULL)
				*what = row.messageWhat;
			if (payload != NULL)
				*payload = row.payload;
			return kSelectInvoke;

		case kPanelMenuRowSubPage:
			if (page != NULL)
				*page = row.subPage;
			return kSelectPush;

		default:
			return kSelectNone;
	}
}


const PanelMenuRow&
PanelMenuModel::_RowAt(int32_t page, int32_t index) const
{
	static PanelMenuRow sEmpty;
	if (page < 0 || page >= (int32_t)fPages.size()
		|| index < 0 || index >= (int32_t)fPages[page].rows.size()) {
		return sEmpty;
	}
	return fPages[page].rows[index];
}
