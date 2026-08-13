/*
 * Copyright 2010, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "WirelessNetworkMenuItem.h"

#include <string.h>

#include <Catalog.h>

#include "RadioView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WirelessNetworkMenuItem"


WirelessNetworkMenuItem::WirelessNetworkMenuItem(const char* ssid,
	int32 signalStrength, const char* security, bool isConnected,
	BMessage* message)
	:
	BMenuItem(ssid, message),
	fSSID(ssid),
	fSignalStrength(signalStrength),
	fSecurity(security),
	fIsConnected(isConnected)
{
	BString label = ssid;
	if (fSecurity.Length() > 0) {
		label << " (" << fSecurity << ")";
		SetLabel(label.String());
	}

	if (isConnected)
		SetMarked(true);
}


WirelessNetworkMenuItem::~WirelessNetworkMenuItem()
{
}


void
WirelessNetworkMenuItem::DrawContent()
{
	DrawRadioIcon();
	BMenuItem::DrawContent();
}


void
WirelessNetworkMenuItem::GetContentSize(float* width, float* height)
{
	BMenuItem::GetContentSize(width, height);
	*width += *height + 4;
}


void
WirelessNetworkMenuItem::DrawRadioIcon()
{
	BRect bounds = Frame();
	bounds.left = bounds.right - 4 - bounds.Height();
	bounds.right -= 4;
	bounds.bottom -= 2;

	RadioView::Draw(Menu(), bounds, fSignalStrength, RadioView::DefaultMax());
}


/*static*/ int
WirelessNetworkMenuItem::CompareSignalStrength(const BMenuItem* a,
	const BMenuItem* b)
{
	WirelessNetworkMenuItem* aItem = (WirelessNetworkMenuItem*)a;
	WirelessNetworkMenuItem* bItem = (WirelessNetworkMenuItem*)b;

	if (aItem->SignalStrength() == bItem->SignalStrength())
		return strcasecmp(aItem->SSID(), bItem->SSID());

	return bItem->SignalStrength() - aItem->SignalStrength();
}
