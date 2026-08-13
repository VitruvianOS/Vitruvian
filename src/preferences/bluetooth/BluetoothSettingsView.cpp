/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Four controls, matched against what BlueZ can honestly back:
 *
 *  - Local devices found on system: straightforward, same as upstream.
 *  - Default inquiry time: BlueZ's StartDiscovery is open-ended (no
 *    discovery-timeout property on org.bluez.Adapter1), so this is purely
 *    a client-side value we hold in BluetoothSettings and hand to
 *    DiscoveryAgent::StartInquiry()'s `secs` parameter when a scan starts
 *    (see InquiryPanel, which now takes it as a constructor argument).
 *  - Incoming connections policy: BlueZ has no policy tiers. The only real
 *    knob is org.bluez.Adapter1.Pairable (accept/reject incoming pairing
 *    requests outright), so that -- not upstream's three-way menu -- is
 *    what this control is, labeled accordingly. It is live-written the
 *    moment it's toggled, same as Powered/Discoverable in BluetoothWindow,
 *    not something staged behind Apply/Revert.
 *  - Identify host as: org.bluez.Adapter1.Class is read-only over D-Bus
 *    (set via Class= in /etc/bluetooth/main.conf, applied by bluetoothd on
 *    reload) -- there is no live setter to wire up. Shown as read-only text
 *    instead of a control that would silently do nothing.
 */

#include "BluetoothSettingsView.h"

#include <bluetooth/DeviceClass.h>
#include <bluetooth/LocalDevice.h>

#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings view"


static const uint32 kMsgLocalSwitched = 'bslc';
static const uint32 kMsgSetInquiryTime = 'bsit';
static const uint32 kMsgSetPairable = 'bspr';
static const uint32 kMsgStatusReady = 'bsst';
static const uint32 kMsgAdapterOpDone = 'bsad';


using namespace Bluetooth;


BluetoothSettingsView::BluetoothSettingsView(const char* name,
	BluetoothSettings& settings)
	:
	BView(name, 0),
	fSettings(settings),
	fLocalDevicesMenu(NULL),
	fAdapters(NULL)
{
	fLocalDevicesMenu = new BPopUpMenu(B_TRANSLATE("Searching" B_UTF8_ELLIPSIS));
	fLocalDevicesMenuField = new BMenuField("devices",
		B_TRANSLATE("Local devices found on system:"), fLocalDevicesMenu);

	BString label(B_TRANSLATE("Default inquiry time:"));
	label << " " << fSettings.InquiryTime();
	fInquiryTimeControl = new BSlider("time", label.String(),
		new BMessage(kMsgSetInquiryTime), 15, 61, B_HORIZONTAL);
	fInquiryTimeControl->SetLimitLabels(B_TRANSLATE("15 secs"),
		B_TRANSLATE("61 secs"));
	fInquiryTimeControl->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fInquiryTimeControl->SetHashMarkCount(20);
	fInquiryTimeControl->SetValue(fSettings.InquiryTime());

	fPairableCheckBox = new BCheckBox("pairable",
		B_TRANSLATE("Accept incoming pairing requests"),
		new BMessage(kMsgSetPairable));
	fPairableCheckBox->SetEnabled(false);

	fDeviceClassView = new BStringView("deviceClass", "");

	BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fLocalDevicesMenuField->CreateLabelLayoutItem(), 0, 0)
		.Add(fLocalDevicesMenuField->CreateMenuBarLayoutItem(), 1, 0)

		.Add(fInquiryTimeControl, 0, 1, 2)

		.Add(fPairableCheckBox, 0, 2, 2)

		.Add(new BStringView("classLabel", B_TRANSLATE("Identify host as:")),
			0, 3)
		.Add(fDeviceClassView, 1, 3)
	.End();
}


BluetoothSettingsView::~BluetoothSettingsView()
{
	// fSettings is owned by BluetoothWindow, which saves it on its own
	// destruction -- this view only mutates the shared instance.
	LocalDevice::StopWatching(BMessenger(this));

	delete fAdapters;
}


void
BluetoothSettingsView::AttachedToWindow()
{
	if (Parent() != NULL)
		SetViewColor(Parent()->ViewColor());
	else
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fLocalDevicesMenu->SetTargetForItems(this);
	fInquiryTimeControl->SetTarget(this);
	fPairableCheckBox->SetTarget(this);

	LocalDevice::StartWatching(BMessenger(this),
		LocalDevice::NOTIFICATION_ADAPTER_ADDED
		| LocalDevice::NOTIFICATION_ADAPTER_REMOVED
		| LocalDevice::NOTIFICATION_ADAPTER_PROPERTY_CHANGED);

	_RequestStatus();
}


void
BluetoothSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgStatusReady:
			_ApplyStatus(message);
			break;

		case LocalDevice::NOTIFICATION_ADAPTER_ADDED:
		case LocalDevice::NOTIFICATION_ADAPTER_REMOVED:
			_RequestStatus();
			break;

		case LocalDevice::NOTIFICATION_ADAPTER_PROPERTY_CHANGED:
		{
			const char* path;
			if (message->FindString("path", &path) == B_OK
					&& fSelectedAdapterPath == path) {
				// A partial delta could be missing "pairable"/"class" if
				// something else changed -- re-fetch rather than risk
				// showing a stale value next to a fresh one.
				_RequestStatus();
			}
			break;
		}

		case kMsgLocalSwitched:
		{
			BString path;
			if (message->FindString("path", &path) == B_OK)
				_SelectAdapter(path);
			break;
		}

		case kMsgSetInquiryTime:
		{
			fSettings.SetInquiryTime(fInquiryTimeControl->Value());
			BString label(B_TRANSLATE("Default inquiry time:"));
			label << " " << fInquiryTimeControl->Value();
			fInquiryTimeControl->SetLabel(label.String());
			break;
		}

		case kMsgSetPairable:
		{
			LocalDevice* adapter = _SelectedAdapter();
			if (adapter == NULL)
				break;
			bool pairable = fPairableCheckBox->Value() == B_CONTROL_ON;
			adapter->SetPairable(pairable, BMessenger(this),
				kMsgAdapterOpDone);
			break;
		}

		case kMsgAdapterOpDone:
			_RequestStatus();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
BluetoothSettingsView::_RequestStatus()
{
	LocalDevice::FetchAllAsync(BMessenger(this), kMsgStatusReady);
}


void
BluetoothSettingsView::_ApplyStatus(BMessage* message)
{
	delete fAdapters;
	fAdapters = new LocalDevicesList(2);
	LocalDevice::DevicesFromMessage(*message, *fAdapters);

	while (fLocalDevicesMenu->CountItems() > 0) {
		BMenuItem* item = fLocalDevicesMenu->RemoveItem((int32)0);
		delete item;
	}

	if (fAdapters->CountItems() == 0) {
		BMenuItem* none = new BMenuItem(B_TRANSLATE("No adapter found"),
			NULL);
		none->SetEnabled(false);
		fLocalDevicesMenu->AddItem(none);
		none->SetMarked(true);
		fSelectedAdapterPath = "";
		_UpdateAdapterDependentControls();
		return;
	}

	// Prefer the remembered adapter if it's still present; otherwise fall
	// back to the first one, same rule BluetoothWindow uses.
	bool foundRemembered = false;
	for (int32 i = 0; i < fAdapters->CountItems(); i++) {
		LocalDevice* adapter = fAdapters->ItemAt(i);

		BMessage* itemMessage = new BMessage(kMsgLocalSwitched);
		itemMessage->AddString("path", adapter->Path());
		BMenuItem* item = new BMenuItem(adapter->GetFriendlyName().String(),
			itemMessage);
		fLocalDevicesMenu->AddItem(item);

		if (adapter->Path() == fSettings.PickedAdapterPath()) {
			item->SetMarked(true);
			fSelectedAdapterPath = adapter->Path();
			foundRemembered = true;
		}
	}

	if (!foundRemembered) {
		LocalDevice* first = fAdapters->ItemAt(0);
		fSelectedAdapterPath = first->Path();
		fLocalDevicesMenu->ItemAt(0)->SetMarked(true);
	}

	fLocalDevicesMenu->SetTargetForItems(this);
	_UpdateAdapterDependentControls();
}


void
BluetoothSettingsView::_SelectAdapter(const BString& path)
{
	fSelectedAdapterPath = path;
	fSettings.SetPickedAdapterPath(path);
	_UpdateAdapterDependentControls();
}


LocalDevice*
BluetoothSettingsView::_SelectedAdapter()
{
	if (fAdapters == NULL || fSelectedAdapterPath.IsEmpty())
		return NULL;

	for (int32 i = 0; i < fAdapters->CountItems(); i++) {
		LocalDevice* adapter = fAdapters->ItemAt(i);
		if (adapter->Path() == fSelectedAdapterPath)
			return adapter;
	}
	return NULL;
}


void
BluetoothSettingsView::_UpdateAdapterDependentControls()
{
	LocalDevice* adapter = _SelectedAdapter();
	if (adapter == NULL) {
		fPairableCheckBox->SetEnabled(false);
		fPairableCheckBox->SetValue(B_CONTROL_OFF);
		fDeviceClassView->SetText(B_TRANSLATE("Unknown"));
		return;
	}

	fPairableCheckBox->SetEnabled(true);
	fPairableCheckBox->SetValue(
		adapter->IsPairable() ? B_CONTROL_ON : B_CONTROL_OFF);

	DeviceClass deviceClass = adapter->GetDeviceClass();
	if (deviceClass.IsUnknownDeviceClass()) {
		fDeviceClassView->SetText(B_TRANSLATE("Unknown"));
	} else {
		BString major;
		deviceClass.GetMajorDeviceClass(major);
		BString minor;
		deviceClass.GetMinorDeviceClass(minor);

		BString text(major);
		if (!minor.IsEmpty() && minor != " -")
			text << " (" << minor << ")";
		fDeviceClassView->SetText(text.String());
	}
}
