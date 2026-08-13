/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ConnectionInfoWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <StringView.h>

#include "NMBackend.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ConnectionInfoWindow"


static const uint32 kMsgClose = 'clse';


static const char* kNotYetAvailable = "Not yet available";


static BString
_StateString(uint32 state)
{
	switch (state) {
		case 30: return B_TRANSLATE("Disconnected");
		case 40: case 50: case 60: case 70: case 80: case 90:
			return B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS);
		case 100: return B_TRANSLATE("Activated");
		case 110: return B_TRANSLATE("Deactivating" B_UTF8_ELLIPSIS);
		case 120: return B_TRANSLATE("Failed");
		case 20: return B_TRANSLATE("Unavailable");
		case 10: return B_TRANSLATE("Unmanaged");
		default: return B_TRANSLATE("Unknown");
	}
}


ConnectionInfoWindow::ConnectionInfoWindow(const char* devicePath)
	:
	BWindow(BRect(0, 0, 360, 10), B_TRANSLATE("Connection Information"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS),
	fRootLayout(NULL),
	fInfoView(NULL),
	fDevicePath(devicePath)
{
	BButton* closeButton = new BButton(B_TRANSLATE("Close"),
		new BMessage(kMsgClose));
	closeButton->MakeDefault(true);

	fInfoView = new BStringView(NULL, B_TRANSLATE("Loading" B_UTF8_ELLIPSIS));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fInfoView)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(closeButton)
		.End();

	fRootLayout = dynamic_cast<BGroupLayout*>(GetLayout());

	CenterOnScreen();
}


ConnectionInfoWindow::~ConnectionInfoWindow()
{
}


void
ConnectionInfoWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgClose:
			PostMessage(B_QUIT_REQUESTED);
			break;

		case kMsgDeviceInfoReply:
			_PopulateFrom(*message);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
ConnectionInfoWindow::_PopulateFrom(const BMessage& deviceInfo)
{
	BString interfaceName, hwAddress, type, driver;
	uint32 state = 0;
	uint32 mtu = 0;

	deviceInfo.FindString(kNMFieldInterface, &interfaceName);
	deviceInfo.FindString(kNMFieldHWAddress, &hwAddress);
	deviceInfo.FindString(kNMFieldType, &type);
	deviceInfo.FindString(kNMFieldDriver, &driver);
	deviceInfo.FindUInt32(kNMFieldState, &state);
	deviceInfo.FindUInt32(kNMFieldMTU, &mtu);

	BGridView* grid = new BGridView(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING);
	BGridLayout* gridLayout = grid->GridLayout();

	int32 row = 0;
	auto addRow = [&](const char* label, const BString& value) {
		gridLayout->AddView(new BStringView(NULL, label), 0, row);
		BStringView* valueView = new BStringView(NULL, value.String());
		valueView->SetFont(be_bold_font);
		gridLayout->AddView(valueView, 1, row);
		row++;
	};

	if (interfaceName.Length() == 0 && hwAddress.Length() == 0
		&& type.Length() == 0) {
		// GetDeviceInfoAsync() found no device at this path anymore --
		// distinguish from a populated-but-sparse reply: unavailable,
		// not a blank/empty pane with no explanation.
		addRow(B_TRANSLATE("Status:"),
			B_TRANSLATE("Device is no longer present"));
	} else {
		addRow(B_TRANSLATE("Interface:"), interfaceName);
		addRow(B_TRANSLATE("Type:"), type);
		addRow(B_TRANSLATE("Hardware:"),
			hwAddress.Length() > 0 ? hwAddress : BString(kNotYetAvailable));
		addRow(B_TRANSLATE("State:"), _StateString(state));

		if (type == "wifi") {
			addRow(B_TRANSLATE("SSID:"), kNotYetAvailable);
			addRow(B_TRANSLATE("Signal:"), kNotYetAvailable);
			addRow(B_TRANSLATE("Security:"), kNotYetAvailable);
			addRow(B_TRANSLATE("Frequency:"), kNotYetAvailable);
		}

		addRow(B_TRANSLATE("IPv4 Address:"), kNotYetAvailable);
		addRow(B_TRANSLATE("IPv4 Gateway:"), kNotYetAvailable);
		addRow(B_TRANSLATE("IPv4 DNS:"), kNotYetAvailable);

		addRow(B_TRANSLATE("Driver:"),
			driver.Length() > 0 ? driver : BString(kNotYetAvailable));
		if (mtu > 0) {
			BString mtuStr;
			mtuStr << mtu;
			addRow(B_TRANSLATE("MTU:"), mtuStr);
		}
	}

	// Swap the Loading placeholder for the populated grid in place.
	if (fRootLayout == NULL) {
		delete grid;
		return;
	}
	// BLayout::RemoveView() only detaches the layout item; the BView stays
	// a child of the window. Deleting it while still attached hits
	// BView's "Call RemoveSelf first" debugger() -- RemoveSelf() first.
	fRootLayout->RemoveView(fInfoView);
	fInfoView->RemoveSelf();
	delete fInfoView;
	fInfoView = grid;
	fRootLayout->AddView(0, fInfoView);
}
