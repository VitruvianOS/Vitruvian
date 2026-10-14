/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "DeviceListItem.h"

#include <Font.h>
#include <View.h>


DeviceCategoryItem::DeviceCategoryItem(Hardware::DeviceCategory category)
	:
	BStringItem(Hardware::CategoryLabel(category)),
	fCategory(category)
{
}


void
DeviceCategoryItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	rgb_color high = ui_color(IsSelected()
		? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
	rgb_color fill = ui_color(IsSelected()
		? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);

	if (complete) {
		owner->SetHighColor(fill);
		owner->FillRect(frame);
	}

	owner->SetHighColor(high);
	owner->SetLowColor(fill);

	BFont font;
	owner->GetFont(&font);
	font.SetFlags(font.Flags() | B_BOLD_FACE);
	owner->SetFont(&font);

	owner->DrawString(Text(), BPoint(frame.left + 3, frame.bottom - 5));
}


DeviceItem::DeviceItem(const Hardware::DeviceRecord& record,
	const char* name)
	:
	BStringItem(name, 1),
	fRecord(record)
{
}
