/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "InquiryPanel.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <StatusBar.h>
#include <StringView.h>

#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>

#include "DeviceListItem.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Inquiry panel"


using namespace Bluetooth;


static const uint32 kMsgRescan = 'resc';
static const uint32 kMsgPair = 'pair';
static const uint32 kMsgPairDone = 'pdon';
static const uint32 kMsgDeviceFound = 'dvfn';
static const uint32 kMsgInquiryStarted = 'inst';
static const uint32 kMsgInquiryFinished = 'infn';
static const uint32 kMsgSelected = 'seld';


namespace {

// Bridges DiscoveryListener's callbacks (which run on the listener's own
// BLooper thread, see DiscoveryListener.cpp) back to InquiryPanel's window
// thread via BMessenger -- never touches fPanel's views directly.
class PanelDiscoveryListener : public DiscoveryListener {
public:
	explicit PanelDiscoveryListener(InquiryPanel* panel)
		:
		fPanel(panel)
	{
	}

	virtual void DeviceDiscovered(RemoteDevice* device, DeviceClass cod)
	{
		BMessage message(kMsgDeviceFound);
		message.AddString("path", device->Path());
		message.AddString("address", device->GetBluetoothAddress());
		message.AddString("name", device->GetFriendlyName());
		message.AddUInt32("class", device->GetDeviceClass().Record());
		message.AddBool("paired", device->IsPaired());
		message.AddBool("connected", device->IsConnected());
		message.AddBool("trusted", device->IsTrustedDevice());
		BMessenger(fPanel).SendMessage(&message);
	}

	virtual void InquiryStarted(status_t status)
	{
		BMessenger(fPanel).SendMessage(kMsgInquiryStarted);
	}

	virtual void InquiryCompleted(int discType)
	{
		BMessenger(fPanel).SendMessage(kMsgInquiryFinished);
	}

private:
	InquiryPanel* fPanel;
};

} // namespace


InquiryPanel::InquiryPanel(const BString& adapterPath,
	const BString& adapterName, bigtime_t inquirySeconds)
	:
	BWindow(BRect(120, 120, 480, 440), B_TRANSLATE("Add Bluetooth device"),
		B_FLOATING_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS),
	fAdapterPath(adapterPath),
	fDiscoveryAgent(NULL),
	fDiscoveryListener(NULL),
	fInquirySeconds(inquirySeconds),
	fScanning(false)
{
	fMessageView = new BStringView("message",
		B_TRANSLATE("Scanning for nearby devices..."));

	fProgressBar = new BStatusBar("progress");

	fDeviceList = new BListView("found_devices", B_SINGLE_SELECTION_LIST);
	fDeviceList->SetSelectionMessage(new BMessage(kMsgSelected));
	fScrollView = new BScrollView("scroll", fDeviceList, 0, false, true);

	fPairButton = new BButton("pair", B_TRANSLATE("Pair" B_UTF8_ELLIPSIS),
		new BMessage(kMsgPair));
	fPairButton->SetEnabled(false);

	fRescanButton = new BButton("rescan",
		B_TRANSLATE("Scan again"), new BMessage(kMsgRescan));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fMessageView)
		.Add(fProgressBar)
		.Add(fScrollView)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fRescanButton)
			.AddGlue()
			.Add(fPairButton)
		.End()
	.End();

	if (!adapterName.IsEmpty())
		SetTitle(adapterName.String());

	fDiscoveryAgent = new DiscoveryAgent(fAdapterPath);
	fDiscoveryListener = new PanelDiscoveryListener(this);

	CenterOnScreen();

	_StartInquiry();
}


InquiryPanel::~InquiryPanel()
{
	_StopInquiry();
	delete fDiscoveryAgent;
	// fDiscoveryListener is a BLooper; it quits itself via CancelInquiry's
	// StopWatching, but we still own the initial allocation until Run().
}


void
InquiryPanel::_StartInquiry()
{
	fScanning = true;
	fMessageView->SetText(B_TRANSLATE("Scanning for nearby devices..."));
	fProgressBar->Reset();
	fDeviceList->MakeEmpty();
	fPairButton->SetEnabled(false);
	fRescanButton->SetEnabled(false);

	if (fInquirySeconds > 0)
		fDiscoveryAgent->StartInquiry(fDiscoveryListener, fInquirySeconds);
	else
		fDiscoveryAgent->StartInquiry(fDiscoveryListener);
}


void
InquiryPanel::_StopInquiry()
{
	if (fDiscoveryAgent != NULL && fDiscoveryListener != NULL)
		fDiscoveryAgent->CancelInquiry(fDiscoveryListener);
	fScanning = false;
}


void
InquiryPanel::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgInquiryStarted:
			InquiryStarted();
			break;

		case kMsgDeviceFound:
			DeviceFound(message);
			break;

		case kMsgInquiryFinished:
			InquiryFinished();
			break;

		case kMsgRescan:
			_StartInquiry();
			break;

		case kMsgSelected:
			_UpdateButtons();
			break;

		case kMsgPair:
		{
			int32 selection = fDeviceList->CurrentSelection();
			DeviceListItem* item = selection >= 0
				? (DeviceListItem*)fDeviceList->ItemAt(selection) : NULL;
			if (item != NULL) {
				BMessage info;
				info.AddString("path", item->Path());
				RemoteDevice device(info);
				device.Pair(BMessenger(this), kMsgPairDone);
				fMessageView->SetText(B_TRANSLATE("Pairing" B_UTF8_ELLIPSIS));
			}
			break;
		}

		case kMsgPairDone:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status == B_OK) {
				fMessageView->SetText(B_TRANSLATE("Paired."));
			} else {
				// The BluetoothStatus replicant, not this preflet, owns the
				// org.bluez.Agent1 pairing agent; if it isn't running there
				// is no one to answer the passkey exchange and BlueZ
				// rejects the request. Also covers a real user-declined
				// pairing.
				BString text = B_TRANSLATE(
					"Pairing failed. Make sure the Bluetooth status item is "
					"running (it registers the pairing agent), or the "
					"remote device may have declined.");
				BAlert* alert = new BAlert(B_TRANSLATE("Pairing failed"),
					text, B_TRANSLATE("OK"));
				alert->Go(NULL);
				fMessageView->SetText(B_TRANSLATE("Pairing failed."));
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
InquiryPanel::DeviceFound(BMessage* deviceInfo)
{
	BString path;
	deviceInfo->FindString("path", &path);

	// A rescan may re-report an address already in the list; replace rather
	// than duplicate.
	for (int32 i = 0; i < fDeviceList->CountItems(); i++) {
		DeviceListItem* existing = (DeviceListItem*)fDeviceList->ItemAt(i);
		if (existing->Path() == path) {
			RemoteDevice device(*deviceInfo);
			existing->UpdateFrom(device);
			fDeviceList->InvalidateItem(i);
			return;
		}
	}

	RemoteDevice device(*deviceInfo);
	fDeviceList->AddItem(new DeviceListItem(device));
	_UpdateButtons();
}


void
InquiryPanel::InquiryStarted()
{
	fMessageView->SetText(B_TRANSLATE("Scanning for nearby devices..."));
}


void
InquiryPanel::InquiryFinished()
{
	fScanning = false;
	fMessageView->SetText(fDeviceList->CountItems() > 0
		? B_TRANSLATE("Scan complete. Select a device to pair.")
		: B_TRANSLATE("Scan complete. No devices found."));
	fRescanButton->SetEnabled(true);
	_UpdateButtons();
}


void
InquiryPanel::_UpdateButtons()
{
	fPairButton->SetEnabled(fDeviceList->CurrentSelection() >= 0);
}


bool
InquiryPanel::QuitRequested()
{
	_StopInquiry();
	return true;
}
