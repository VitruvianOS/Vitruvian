/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <ObexSendChooser.h>
#include <ObexTransferWindow.h>

#include <bluetooth/RemoteDevice.h>

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Point.h>
#include <PopUpMenu.h>
#include <String.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Bluetooth send"


static const uint32 kMsgDevicesReady = 'osdr';
static const uint32 kMsgStart = 'osgo';
static const uint32 kMsgPickDevice = 'ospd';


void
ObexSendChooser::Send(const BMessage& files)
{
	ObexSendChooser* chooser = new ObexSendChooser(files);
	be_app->AddHandler(chooser);
	BMessenger self(chooser);
	BMessage start(kMsgStart);
	self.SendMessage(&start);
}


ObexSendChooser::ObexSendChooser(const BMessage& files)
	:
	BHandler("obex_send_chooser"),
	fFiles(files)
{
}


ObexSendChooser::~ObexSendChooser()
{
}


void
ObexSendChooser::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStart:
		{
			status_t status = Bluetooth::RemoteDevice::FetchAllAsync(
				BMessenger(this), kMsgDevicesReady);
			if (status != B_OK) {
				BAlert* alert = new BAlert(B_TRANSLATE("Bluetooth"),
					B_TRANSLATE("Couldn't query Bluetooth devices."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_WARNING_ALERT);
				alert->Go();
				be_app->RemoveHandler(this);
				delete this;
			}
			break;
		}

		case kMsgDevicesReady:
			_ShowPicker(message);
			break;

		default:
			BHandler::MessageReceived(message);
			break;
	}
}


void
ObexSendChooser::_ShowPicker(BMessage* devicesReply)
{
	Bluetooth::RemoteDevicesList devices(8);
	Bluetooth::RemoteDevice::DevicesFromMessage(*devicesReply, devices);

	BPopUpMenu* menu = new BPopUpMenu("bt_devices");
	BString address;
	BString name;
	bool havePaired = false;

	for (int32 i = 0; i < devices.CountItems(); i++) {
		Bluetooth::RemoteDevice* device = devices.ItemAt(i);
		if (device == NULL || !device->IsPaired())
			continue;

		havePaired = true;
		BString label(device->GetFriendlyName());
		if (label.IsEmpty())
			label = device->GetBluetoothAddress();
		BMessage* pick = new BMessage(kMsgPickDevice);
		pick->AddString("address", device->GetBluetoothAddress());
		pick->AddString("name", device->GetFriendlyName());
		menu->AddItem(new BMenuItem(label.String(), pick));
	}

	if (!havePaired) {
		BAlert* alert = new BAlert(B_TRANSLATE("Bluetooth"),
			B_TRANSLATE("No paired Bluetooth devices. Pair a device in "
				"the Bluetooth preflet first."),
			B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_WARNING_ALERT);
		alert->Go();
		be_app->RemoveHandler(this);
		delete this;
		return;
	}

	// No item target: Go() returns the chosen item; invoking would post
	// to a freed handler.
	BMenuItem* chosen = menu->Go(BPoint(120, 120), true);
	if (chosen != NULL && chosen->Message() != NULL) {
		chosen->Message()->FindString("address", &address);
		chosen->Message()->FindString("name", &name);
		if (name.IsEmpty())
			name = address;
	}

	delete menu;

	if (!address.IsEmpty()) {
		ObexTransferWindow* window = new ObexTransferWindow(
			name.String(), address.String(), fFiles);
		window->Show();
	}

	be_app->RemoveHandler(this);
	delete this;
}
