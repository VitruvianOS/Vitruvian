/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "BluetoothWindow.h"

#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>

#include <cstring>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <OutlineListView.h>
#include <Messenger.h>
#include <Roster.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TabView.h>

#include "BluetoothSettingsView.h"
#include "DeviceListItem.h"
#include "InquiryPanel.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Bluetooth"


static const uint32 kMsgToggleReplicant = 'trep';
static const uint32 kMsgReplicantToggled = 'trpd';
static const uint32 kMsgInitialScan = 'inis';
static const uint32 kMsgStatusReady = 'btst';
static const uint32 kMsgAdapterOpDone = 'btao';
static const uint32 kMsgOperationDone = 'btod';
static const uint32 kMsgPairDone = 'btpd';
static const uint32 kMsgSelectionChanged = 'btsl';

static const uint32 kMsgTogglePowered = 'btpw';
static const uint32 kMsgToggleDiscoverable = 'btdv';
static const uint32 kMsgAdd = 'btAd';
static const uint32 kMsgRemove = 'btRm';
static const uint32 kMsgPair = 'btPr';
static const uint32 kMsgDisconnect = 'btDc';
static const uint32 kMsgTrust = 'btTr';
static const uint32 kMsgBlock = 'btBl';
static const uint32 kMsgRefresh = 'btRf';

// Must match BluetoothStatus.rdef's app_signature exactly (also mirrored as
// kSignature in BluetoothStatus.cpp, which lives in a different binary and
// isn't link-visible from here).
static const char* kBluetoothStatusSignature
	= "application/x-vnd.Haiku-BluetoothStatus";

// The BView name BluetoothStatusView archives itself under (see its
// constructor) -- BDeskbar's item-lookup API works by that name.
static const char* kBluetoothStatusDeskbarItemName = "BluetoothStatus";


using namespace Bluetooth;


BluetoothWindow::BluetoothWindow()
	:
	BWindow(BRect(100, 100, 520, 480), B_TRANSLATE("Bluetooth"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fReplicantOpPending(false),
	fHasAdapter(false),
	fAdapterPowered(false)
{
	fPoweredCheckBox = new BCheckBox("powered", B_TRANSLATE("Powered"),
		new BMessage(kMsgTogglePowered));
	fDiscoverableCheckBox = new BCheckBox("discoverable",
		B_TRANSLATE("Discoverable"), new BMessage(kMsgToggleDiscoverable));

	fAdapterNameView = new BStringView("adapter_name", "");
	fAdapterAddressView = new BStringView("adapter_address", "");
	fAdapterAddressView->SetFont(be_fixed_font);

	fDeviceList = new BOutlineListView("device_list", B_SINGLE_SELECTION_LIST);
	fDeviceList->SetSelectionMessage(new BMessage(kMsgSelectionChanged));

	fPairedHeader = new BStringItem(B_TRANSLATE("Paired devices"));
	fAvailableHeader = new BStringItem(B_TRANSLATE("Available devices"));
	fDeviceList->AddItem(fPairedHeader);
	fDeviceList->AddItem(fAvailableHeader);

	fScrollView = new BScrollView("device_scroll", fDeviceList, 0, false,
		true);

	fEmptyStateView = new BStringView("empty_state", "");
	fEmptyStateView->SetAlignment(B_ALIGN_CENTER);
	fEmptyStateView->SetHighUIColor(B_LIST_ITEM_TEXT_COLOR, B_DARKEN_2_TINT);

	fAddButton = new BButton("add", B_TRANSLATE("Add" B_UTF8_ELLIPSIS),
		new BMessage(kMsgAdd));
	fRemoveButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemove));
	fPairButton = new BButton("pair", B_TRANSLATE("Pair" B_UTF8_ELLIPSIS),
		new BMessage(kMsgPair));
	fDisconnectButton = new BButton("disconnect", B_TRANSLATE("Disconnect"),
		new BMessage(kMsgDisconnect));
	fTrustButton = new BButton("trust", B_TRANSLATE("Trust"),
		new BMessage(kMsgTrust));
	fBlockButton = new BButton("block", B_TRANSLATE("As blocked"),
		new BMessage(kMsgBlock));
	fRefreshButton = new BButton("refresh",
		B_TRANSLATE("Refresh" B_UTF8_ELLIPSIS), new BMessage(kMsgRefresh));

	fShowReplicantCheckBox = new BCheckBox("showReplicantCheckBox",
		B_TRANSLATE("Show Bluetooth status in Deskbar"),
		new BMessage(kMsgToggleReplicant));
	fShowReplicantCheckBox->SetValue(_IsReplicantInstalled());

	BView* contentView = BLayoutBuilder::Group<>(B_VERTICAL,
			B_USE_DEFAULT_SPACING)
		.Add(fScrollView)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fAddButton)
			.Add(fRemoveButton)
			.AddGlue()
			.Add(fPairButton)
			.Add(fDisconnectButton)
			.Add(fTrustButton)
			.Add(fBlockButton)
			.Add(fRefreshButton)
		.End()
		.View();

	BView* emptyView = BLayoutBuilder::Group<>(B_VERTICAL)
		.AddGlue()
		.Add(fEmptyStateView)
		.AddGlue()
		.View();

	BView* devicesPage = BLayoutBuilder::Group<>(B_VERTICAL,
			B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.Add(fAdapterNameView)
				.Add(fAdapterAddressView)
			.End()
			.AddGlue()
			.Add(fDiscoverableCheckBox)
			.Add(fPoweredCheckBox)
		.End()
		.AddCards()
			.Add(contentView)
			.Add(emptyView)
			.GetLayout(&fContentCards)
		.End()
		.Add(fShowReplicantCheckBox)
	.View();
	devicesPage->SetName(B_TRANSLATE("Devices"));

	fSettings.LoadSettings();
	fSettingsView = new BluetoothSettingsView("settingsPage", fSettings);
	fSettingsView->SetName(B_TRANSLATE("Settings"));

	fTabView = new BTabView("tabs", B_WIDTH_FROM_LABEL);
	fTabView->AddTab(devicesPage);
	fTabView->AddTab(fSettingsView);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(fTabView)
	.End();

	_ShowLoadingState();
	_UpdateButtons();

	// Watch adapter/device changes for as long as this window lives -- the
	// preflet reacts to notifications instead of polling. BlueZBackend's
	// multi-watcher support means this coexists fine with an InquiryPanel's
	// own DiscoveryAgent scan.
	LocalDevice::StartWatching(BMessenger(this),
		LocalDevice::NOTIFICATION_ADAPTER_ADDED
		| LocalDevice::NOTIFICATION_ADAPTER_REMOVED
		| LocalDevice::NOTIFICATION_ADAPTER_PROPERTY_CHANGED
		| LocalDevice::NOTIFICATION_DEVICE_CONNECTED
		| LocalDevice::NOTIFICATION_DEVICE_DISCONNECTED
		| LocalDevice::NOTIFICATION_DEVICE_PROPERTY_CHANGED);

	CenterOnScreen();

	// Deferred, not called here: FetchStatusAsync does a blocking-shaped
	// round trip through the dispatch thread; queuing it before Show() would
	// still be fine since it's async, but keep the same "nothing before
	// Show()" discipline as the rest of this project so a slow/absent
	// bluetoothd can never delay the window appearing.
	PostMessage(kMsgInitialScan);
}


BluetoothWindow::~BluetoothWindow()
{
	LocalDevice::StopWatching(BMessenger(this));

	// fSettingsView mutates fSettings live but never persists it itself
	// (see BluetoothSettingsView.h) -- this is the one save point.
	fSettings.SaveSettings();
}


void
BluetoothWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgInitialScan:
		case kMsgRefresh:
			_RequestStatusUpdate();
			break;

		case kMsgStatusReady:
			_ApplyStatusUpdate(message);
			break;

		case LocalDevice::NOTIFICATION_ADAPTER_ADDED:
		case LocalDevice::NOTIFICATION_ADAPTER_REMOVED:
		case LocalDevice::NOTIFICATION_DEVICE_CONNECTED:
		case LocalDevice::NOTIFICATION_DEVICE_DISCONNECTED:
			// Coarse but correct: re-fetch the whole snapshot rather than
			// hand-patch state from a partial notification.
			_RequestStatusUpdate();
			break;

		case LocalDevice::NOTIFICATION_ADAPTER_PROPERTY_CHANGED:
		case LocalDevice::NOTIFICATION_DEVICE_PROPERTY_CHANGED:
			_ApplyPropertyChanged(message);
			break;

		case kMsgAdapterOpDone:
		case kMsgOperationDone:
			_RequestStatusUpdate();
			break;

		case kMsgPairDone:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status != B_OK) {
				// See InquiryPanel's identical handling: the pairing agent
				// lives in the BluetoothStatus replicant , not here.
				BString text = B_TRANSLATE(
					"Pairing failed. Make sure the Bluetooth status item is "
					"running (it registers the pairing agent), or the "
					"remote device may have declined.");
				BAlert* alert = new BAlert(B_TRANSLATE("Pairing failed"),
					text, B_TRANSLATE("OK"));
				alert->Go(NULL);
			}
			_RequestStatusUpdate();
			break;
		}

		case kMsgToggleReplicant:
			_ToggleReplicant();
			break;

		case kMsgReplicantToggled:
			_ApplyReplicantToggled(message);
			break;

		case kMsgTogglePowered:
			_SetPowered(fPoweredCheckBox->Value() == B_CONTROL_ON);
			break;

		case kMsgToggleDiscoverable:
			_SetDiscoverable(fDiscoverableCheckBox->Value() == B_CONTROL_ON);
			break;

		case kMsgSelectionChanged:
			_UpdateButtons();
			break;

		case kMsgAdd:
			_DoAdd();
			break;

		case kMsgRemove:
			_DoRemove();
			break;

		case kMsgPair:
			_DoPair();
			break;

		case kMsgDisconnect:
			_DoDisconnect();
			break;

		case kMsgTrust:
			_DoToggleTrust();
			break;

		case kMsgBlock:
			_DoToggleBlock();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
BluetoothWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
BluetoothWindow::_ShowLoadingState()
{
	fAdapterNameView->SetText(B_TRANSLATE("Loading" B_UTF8_ELLIPSIS));
	fAdapterAddressView->SetText("");
	fEmptyStateView->SetText(B_TRANSLATE("Loading" B_UTF8_ELLIPSIS));
	fPoweredCheckBox->SetEnabled(false);
	fDiscoverableCheckBox->SetEnabled(false);
}


void
BluetoothWindow::_ShowNoAdapterState()
{
	fHasAdapter = false;
	fAdapterPath = "";

	fAdapterNameView->SetText(B_TRANSLATE("No Bluetooth adapter"));
	fAdapterAddressView->SetText("");
	fPoweredCheckBox->SetEnabled(false);
	fPoweredCheckBox->SetValue(B_CONTROL_OFF);
	fDiscoverableCheckBox->SetEnabled(false);
	fDiscoverableCheckBox->SetValue(B_CONTROL_OFF);

	fEmptyStateView->SetText(B_TRANSLATE(
		"No Bluetooth adapter was found. Connect a Bluetooth controller, "
		"or check that bluetoothd is running."));

	if (fContentCards != NULL)
		fContentCards->SetVisibleItem((int32)1);

	_UpdateButtons();
}


void
BluetoothWindow::_RequestStatusUpdate()
{
	status_t status = LocalDevice::FetchStatusAsync(BMessenger(this),
		kMsgStatusReady);
	if (status != B_OK)
		_ShowNoAdapterState();
}


void
BluetoothWindow::_ApplyStatusUpdate(BMessage* message)
{
	BMessage adaptersReply;
	message->FindMessage("adapters", &adaptersReply);

	LocalDevicesList adapters(2);
	LocalDevice::DevicesFromMessage(adaptersReply, adapters);

	if (adapters.CountItems() == 0) {
		_ShowNoAdapterState();
		return;
	}

	// One adapter is the common case and the only one QEMU/most desktops
	// ever expose; picking the first keeps the UI from needing an adapter
	// picker nobody would use.
	LocalDevice* adapter = adapters.ItemAt(0);
	fHasAdapter = true;
	fAdapterPath = adapter->Path();
	fAdapterPowered = adapter->IsPowered();

	fAdapterNameView->SetText(adapter->GetFriendlyName().String());
	fAdapterAddressView->SetText(adapter->GetBluetoothAddress().String());

	fPoweredCheckBox->SetEnabled(true);
	fPoweredCheckBox->SetValue(fAdapterPowered ? B_CONTROL_ON : B_CONTROL_OFF);
	fDiscoverableCheckBox->SetEnabled(fAdapterPowered);
	fDiscoverableCheckBox->SetValue(
		adapter->IsDiscoverable() ? B_CONTROL_ON : B_CONTROL_OFF);

	if (fContentCards != NULL)
		fContentCards->SetVisibleItem((int32)0);

	BMessage devicesReply;
	message->FindMessage("devices", &devicesReply);
	_RebuildDeviceList(&devicesReply);

	_UpdateButtons();
}


void
BluetoothWindow::_ApplyPropertyChanged(BMessage* message)
{
	const char* path;
	if (message->FindString("path", &path) != B_OK)
		return;

	if (path == fAdapterPath) {
		bool value;
		if (message->FindBool("powered", &value) == B_OK) {
			fAdapterPowered = value;
			fPoweredCheckBox->SetValue(value ? B_CONTROL_ON : B_CONTROL_OFF);
			fDiscoverableCheckBox->SetEnabled(value);
		}
		if (message->FindBool("discoverable", &value) == B_OK)
			fDiscoverableCheckBox->SetValue(value ? B_CONTROL_ON
				: B_CONTROL_OFF);
		return;
	}

	// A device property changed (e.g. Connected toggled by an external
	// pairing tool) -- the connected/paired badge and section it belongs to
	// can both change, so re-fetch rather than patch a single row.
	_RequestStatusUpdate();
}


void
BluetoothWindow::_RebuildDeviceList(BMessage* devicesReply)
{
	RemoteDevicesList devices(10);
	RemoteDevice::DevicesFromMessage(*devicesReply, devices);

	// BOutlineListView has no bulk "remove children of this header" call --
	// walk it backwards, in a range bounded by the header positions.
	for (int32 i = fDeviceList->CountItems() - 1; i >= 0; i--) {
		BListItem* item = fDeviceList->ItemAt(i);
		if (item != fPairedHeader && item != fAvailableHeader)
			delete fDeviceList->RemoveItem(i);
	}

	int32 pairedInsertAt = fDeviceList->IndexOf(fAvailableHeader);
	int32 availableInsertAt = fDeviceList->CountItems();

	for (int32 i = 0; i < devices.CountItems(); i++) {
		RemoteDevice* device = devices.ItemAt(i);
		DeviceListItem* item = new DeviceListItem(*device);
		item->SetOutlineLevel(1);

		if (device->IsPaired()) {
			fDeviceList->AddItem(item, pairedInsertAt);
			pairedInsertAt++;
			availableInsertAt++;
		} else
			fDeviceList->AddItem(item, availableInsertAt++);
	}

	fDeviceList->Collapse(fPairedHeader);
	fDeviceList->Collapse(fAvailableHeader);
	fDeviceList->Expand(fPairedHeader);
	fDeviceList->Expand(fAvailableHeader);
}


DeviceListItem*
BluetoothWindow::_SelectedDevice()
{
	int32 selection = fDeviceList->CurrentSelection();
	if (selection < 0)
		return NULL;

	BListItem* item = fDeviceList->ItemAt(selection);
	if (item == fPairedHeader || item == fAvailableHeader)
		return NULL;

	return (DeviceListItem*)item;
}


void
BluetoothWindow::_UpdateButtons()
{
	fAddButton->SetEnabled(fHasAdapter && fAdapterPowered);

	int32 selection = fDeviceList->CurrentSelection();
	BListItem* rawItem = selection >= 0 ? fDeviceList->ItemAt(selection)
		: NULL;
	bool isDevice = rawItem != NULL && rawItem != fPairedHeader
		&& rawItem != fAvailableHeader;
	DeviceListItem* item = isDevice ? (DeviceListItem*)rawItem : NULL;

	fRemoveButton->SetEnabled(item != NULL && item->IsPaired());
	fPairButton->SetEnabled(item != NULL && !item->IsPaired());
	fDisconnectButton->SetEnabled(item != NULL && item->IsConnected());

	// Trusting an unpaired stranger has no security meaning in BlueZ (trust
	// only affects reconnection/authorization of an already-bonded device),
	// so keep it paired-only to match the mental model "trust a device I've
	// paired with". Blocking has no such prerequisite -- it is meant to also
	// cover devices the user never wants to pair with in the first place.
	fTrustButton->SetEnabled(item != NULL && item->IsPaired());
	fTrustButton->SetLabel(item != NULL && item->IsTrusted()
		? B_TRANSLATE("Untrust") : B_TRANSLATE("Trust"));

	fBlockButton->SetEnabled(item != NULL);
	fBlockButton->SetLabel(item != NULL && item->IsBlocked()
		? B_TRANSLATE("Unblock") : B_TRANSLATE("As blocked"));

	fRefreshButton->SetEnabled(fHasAdapter);
}


void
BluetoothWindow::_SetPowered(bool powered)
{
	if (!fHasAdapter)
		return;

	BMessage info;
	info.AddString("path", fAdapterPath);
	LocalDevice adapter(info);
	adapter.SetPowered(powered, BMessenger(this), kMsgAdapterOpDone);
}


void
BluetoothWindow::_SetDiscoverable(bool discoverable)
{
	if (!fHasAdapter)
		return;

	BMessage info;
	info.AddString("path", fAdapterPath);
	LocalDevice adapter(info);
	adapter.SetDiscoverable(discoverable, BMessenger(this),
		kMsgAdapterOpDone);
}


void
BluetoothWindow::_DoAdd()
{
	if (!fHasAdapter)
		return;

	InquiryPanel* panel = new InquiryPanel(fAdapterPath,
		fAdapterNameView->Text(),
		fSettings.InquiryTime() * 1000000LL);
	panel->Show();
}


void
BluetoothWindow::_DoRemove()
{
	DeviceListItem* item = _SelectedDevice();
	if (item == NULL || !item->IsPaired())
		return;

	BMessage info;
	info.AddString("path", item->Path());
	RemoteDevice device(info);
	device.Unpair(BMessenger(this), kMsgOperationDone);
}


void
BluetoothWindow::_DoPair()
{
	DeviceListItem* item = _SelectedDevice();
	if (item == NULL || item->IsPaired())
		return;

	BMessage info;
	info.AddString("path", item->Path());
	RemoteDevice device(info);
	device.Pair(BMessenger(this), kMsgPairDone);
}


void
BluetoothWindow::_DoDisconnect()
{
	DeviceListItem* item = _SelectedDevice();
	if (item == NULL || !item->IsConnected())
		return;

	BMessage info;
	info.AddString("path", item->Path());
	RemoteDevice device(info);
	device.Disconnect(BMessenger(this), kMsgOperationDone);
}


void
BluetoothWindow::_DoToggleTrust()
{
	DeviceListItem* item = _SelectedDevice();
	if (item == NULL || !item->IsPaired())
		return;

	BMessage info;
	info.AddString("path", item->Path());
	RemoteDevice device(info);
	device.SetTrusted(!item->IsTrusted(), BMessenger(this), kMsgOperationDone);
}


void
BluetoothWindow::_DoToggleBlock()
{
	DeviceListItem* item = _SelectedDevice();
	if (item == NULL)
		return;

	BMessage info;
	info.AddString("path", item->Path());
	RemoteDevice device(info);
	device.SetBlocked(!item->IsBlocked(), BMessenger(this), kMsgOperationDone);
}


struct _ReplicantToggleJob {
	BMessenger replyTo;
};


void
BluetoothWindow::_ToggleReplicant()
{
	// A mash of the checkbox must not stack up N concurrent Deskbar
	// round trips -- ignore further clicks until the one in flight replies.
	if (fReplicantOpPending)
		return;

	fReplicantOpPending = true;
	fShowReplicantCheckBox->SetEnabled(false);

	_ReplicantToggleJob* job = new _ReplicantToggleJob;
	job->replyTo = BMessenger(this);

	// BDeskbar's calls target Deskbar's own team; on add, Deskbar
	// instantiates the replicant (which registers the BlueZ agent) while
	// blocked -- doing any of this inline on the window thread froze the
	// preflet on rapid clicks. _ApplyReplicantToggled() resyncs the
	// checkbox from the reply.
	thread_id thread = spawn_thread(_ToggleReplicantThread,
		"toggle_bluetooth_replicant", B_NORMAL_PRIORITY, job);
	if (thread >= B_OK) {
		resume_thread(thread);
	} else {
		delete job;
		fReplicantOpPending = false;
		fShowReplicantCheckBox->SetEnabled(true);
	}
}


int32
BluetoothWindow::_ToggleReplicantThread(void* data)
{
	_ReplicantToggleJob* job = (_ReplicantToggleJob*)data;

	BDeskbar deskbar;
	bool wasInstalled = deskbar.HasItem(kBluetoothStatusDeskbarItemName);

	status_t status = B_OK;
	if (wasInstalled) {
		status = deskbar.RemoveItem(kBluetoothStatusDeskbarItemName);
	} else {
		entry_ref ref;
		status = be_roster->FindApp(kBluetoothStatusSignature, &ref);
		if (status == B_OK)
			status = deskbar.AddItem(&ref);
	}

	BMessage reply(kMsgReplicantToggled);
	reply.AddBool("was_installed", wasInstalled);
	reply.AddInt32("status", status);
	reply.AddBool("installed", deskbar.HasItem(kBluetoothStatusDeskbarItemName));
	job->replyTo.SendMessage(&reply);

	delete job;
	return 0;
}


void
BluetoothWindow::_ApplyReplicantToggled(BMessage* message)
{
	fReplicantOpPending = false;
	fShowReplicantCheckBox->SetEnabled(true);

	bool installed = false;
	message->FindBool("installed", &installed);
	fShowReplicantCheckBox->SetValue(installed ? B_CONTROL_ON : B_CONTROL_OFF);

	int32 status = B_OK;
	message->FindInt32("status", &status);
	if (status == B_OK)
		return;

	bool wasInstalled = false;
	message->FindBool("was_installed", &wasInstalled);

	BString text(wasInstalled
		? B_TRANSLATE("Couldn't remove the Bluetooth status item from the "
			"Deskbar: %error%")
		: B_TRANSLATE("Couldn't add the Bluetooth status item to the "
			"Deskbar: %error%"));
	text.ReplaceFirst("%error%", status == B_TIMED_OUT
		? B_TRANSLATE("Deskbar did not reply in time.") : strerror(status));
	BAlert* alert = new BAlert(B_TRANSLATE("Error"), text,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
}


bool
BluetoothWindow::_IsReplicantInstalled()
{
	BDeskbar deskbar;
	return deskbar.HasItem(kBluetoothStatusDeskbarItemName);
}
