/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "DeviceListItem.h"

#include <algorithm>

#include <Catalog.h>
#include <ControlLook.h>
#include <View.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Remote devices"


using namespace Bluetooth;


static const float kInsets = 5.0f;
static const int kTextRows = 2;


DeviceListItem::DeviceListItem(const RemoteDevice& device)
{
	UpdateFrom(device);
}


DeviceListItem::~DeviceListItem()
{
}


void
DeviceListItem::UpdateFrom(const RemoteDevice& device)
{
	fPath = device.Path();
	fAddress = device.GetBluetoothAddress();
	fName = device.GetFriendlyName();
	fClass = device.GetDeviceClass();
	fPaired = device.IsPaired();
	fConnected = device.IsConnected();
	fTrusted = device.IsTrustedDevice();
	fBlocked = device.IsBlockedDevice();
}


void
DeviceListItem::DrawItem(BView* owner, BRect itemRect, bool complete)
{
	rgb_color highColor = owner->HighColor();
	rgb_color lowColor = owner->LowColor();

	if (IsSelected() || complete) {
		rgb_color color = IsSelected()
			? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: owner->ViewColor();
		owner->SetHighColor(color);
		owner->SetLowColor(color);
		owner->FillRect(itemRect);
	} else
		owner->SetLowColor(owner->ViewColor());

	rgb_color textColor = IsSelected()
		? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
		: ui_color(B_LIST_ITEM_TEXT_COLOR);

	font_height finfo;
	be_plain_font->GetHeight(&finfo);

	BPoint iconPoint(itemRect.left, itemRect.top);
	fClass.Draw(owner, iconPoint);

	BPoint textPoint(itemRect.left + DeviceClass::PixelsForIcon + 2 * kInsets,
		itemRect.top + kInsets + finfo.ascent);

	owner->SetHighColor(textColor);
	owner->SetFont(be_plain_font);
	owner->MovePenTo(textPoint);

	BString firstLine(fName);
	BString badges;
	if (fConnected)
		badges << B_TRANSLATE("connected");
	else if (fPaired)
		badges << B_TRANSLATE("paired");
	if (fTrusted) {
		if (!badges.IsEmpty())
			badges << ", ";
		badges << B_TRANSLATE("trusted");
	}
	if (fBlocked) {
		if (!badges.IsEmpty())
			badges << ", ";
		badges << B_TRANSLATE("blocked");
	}
	if (!badges.IsEmpty())
		firstLine << "  (" << badges << ")";
	owner->DrawString(firstLine.String());

	textPoint.y += finfo.ascent + finfo.descent + finfo.leading + kInsets;
	owner->SetFont(be_fixed_font);
	owner->MovePenTo(textPoint);
	owner->DrawString(fAddress.String());

	owner->SetHighColor(highColor);
	owner->SetLowColor(lowColor);
}


void
DeviceListItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);

	font_height height;
	font->GetHeight(&height);
	float textHeight = (height.ascent + height.descent + height.leading)
		* kTextRows + (kTextRows + 1) * kInsets;
	float iconHeight = DeviceClass::PixelsForIcon + 2 * kInsets;
	SetHeight(std::max(textHeight, iconHeight));
}
