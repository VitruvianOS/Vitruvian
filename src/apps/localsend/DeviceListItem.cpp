/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "DeviceListItem.h"

#include <View.h>

#include <stdio.h>


DeviceListItem::DeviceListItem(const BString& alias, const BString& typeName,
	const BString& model, const BString& fingerprint,
	const BString& address, uint16 port, const BString& protocol,
	bool download, const BString& version)
	:
	fAlias(alias),
	fTypeName(typeName),
	fModel(model),
	fFingerprint(fingerprint),
	fAddress(address),
	fPort(port),
	fProtocol(protocol),
	fDownload(download),
	fVersion(version),
	fMaxTextWidth(0)
{
	fDevice.alias = alias.String();
	fDevice.version = version.String();
	fDevice.deviceModel = model.String();
	fDevice.deviceType = LocalSend::LsDevice::DeviceTypeFromName(
		typeName.String());
	fDevice.fingerprint = fingerprint.String();
	fDevice.port = port;
	fDevice.protocol = LocalSend::LsDevice::ProtocolTypeFromName(
		protocol.String());
	fDevice.download = download;
}


void
DeviceListItem::DrawItem(BView* owner, BRect frame, bool complete)
{
	if (complete || IsSelected()) {
		owner->SetHighColor(IsSelected()
			? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: ui_color(B_LIST_BACKGROUND_COLOR));
		owner->FillRect(frame);
	}

	// Small type badge on the left: a letter standing in for the
	// per-type icon until dedicated artwork lands.
	owner->SetHighColor(IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
		: ui_color(B_LIST_ITEM_TEXT_COLOR));
	owner->SetLowColor(owner->HighColor());

	BRect badge(frame.left + 4, frame.top + 3, frame.left + 19,
		frame.bottom - 3);
	owner->SetPenSize(1);
	owner->StrokeRect(badge);

	char typeLetter = 'D';
	if (fTypeName == "mobile")
		typeLetter = 'M';
	else if (fTypeName == "web")
		typeLetter = 'W';
	else if (fTypeName == "headless")
		typeLetter = 'H';
	else if (fTypeName == "server")
		typeLetter = 'S';

	BFont font;
	owner->GetFont(&font);
	font.SetSize(11);
	owner->SetFont(&font);
	char label[2] = { typeLetter, '\0' };
	owner->DrawString(label, BPoint(badge.left + (badge.Width()
		- font.StringWidth(label)) / 2, badge.bottom - 5));

	// Alias plus address underneath.
	font.SetSize(12);
	owner->SetFont(&font);
	BString line = fAlias;
	line << "  (" << fAddress << ":" << fPort << ")";
	owner->DrawString(line.String(),
		BPoint(badge.right + 8, frame.top + 14));

	// Model / type line.
	font.SetSize(10);
	owner->SetFont(&font);
	BString detail(fTypeName);
	if (!fModel.IsEmpty()) {
		detail = fModel;
		detail << " - " << fTypeName;
	}
	detail << " - v" << fVersion;
	if (fProtocol == "https")
		detail << " - TLS";
	owner->DrawString(detail.String(),
		BPoint(badge.right + 8, frame.top + 27));
}


void
DeviceListItem::Update(BView* owner, const BFont* font)
{
	BListItem::Update(owner, font);

	// No SetFont() on the owner: BListView::SetFont re-runs Update().
	SetHeight(32);

	BString widest(fAlias);
	widest << "  (" << fAddress << ":" << fPort << ")";
	fMaxTextWidth = font->StringWidth(widest.String()) + 32;
	SetWidth(owner->Bounds().Width());
}
