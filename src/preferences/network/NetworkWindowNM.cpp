/*
 * Copyright 2004-2019 Haiku Inc., All rights reserved.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NetworkWindowNM.h"

#include "InterfaceDetailView.h"
#include "NMBackend.h"
#include "StaticIPView.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Locale.h>
#include <Directory.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <OutlineListView.h>
#include <Path.h>
#include <PathFinder.h>
#include <PathMonitor.h>
#include <Roster.h>
#include <ScrollView.h>
#include <String.h>
#include <StringItem.h>
#include <SymLink.h>


const char* kNetworkStatusSignature = "application/x-vnd.Haiku-NetworkStatus";

static const uint32 kMsgRevert = 'rvrt';
static const uint32 kMsgToggleReplicant = 'trep';
static const uint32 kMsgItemSelected = 'ItSl';
static const uint32 kMsgConnectDevice = 'cndv';
static const uint32 kMsgDisconnectDevice = 'dscd';
static const uint32 kMsgRefreshDevices = 'rfrd';
static const uint32 kMsgInitialDeviceScan = 'inds';
static const uint32 kMsgDevicesReady = 'dvrd';
static const uint32 kMsgDeviceInfoReady = 'dird';

BMessenger gNetworkWindow;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NetworkWindow"


class TitleItem : public BStringItem {
public:
	TitleItem(const char* title)
		:
		BStringItem(title)
	{
	}

	void DrawItem(BView* owner, BRect bounds, bool complete)
	{
		owner->SetFont(be_bold_font);
		BStringItem::DrawItem(owner, bounds, complete);
		owner->SetFont(be_plain_font);
	}

	void Update(BView* owner, const BFont* font)
	{
		BStringItem::Update(owner, be_bold_font);
	}
};


// Row for a single NetworkManager device: status dot + name + status text,
// modeled on upstream InterfaceListItem's two-zone layout
// (haiku-latest/src/preferences/network/InterfaceListItem.cpp) but using a
// programmatic ui_color() dot instead of per-type HVIF art -- no wifi/
// ether/vpn icon resources exist in this tree yet (the Bluetooth tray glyph
// takes the same "programmatic over authored art, for now" approach).
// Replaces the legacy InterfaceListItem framework retired along
// with the old BNetworkSettings UI.
class DeviceListItem : public BStringItem {
public:
	DeviceListItem(const char* name, const char* devicePath,
		const char* statusText, bool connected)
		:
		BStringItem(name),
		fDevicePath(devicePath),
		fDeviceName(name),
		fStatusText(statusText),
		fConnected(connected),
		fFirstLineOffset(0),
		fLineOffset(0)
	{
	}

	const BString& DevicePath() const { return fDevicePath; }

	virtual void DrawItem(BView* owner, BRect bounds, bool complete)
	{
		owner->PushState();

		if (IsSelected() || complete) {
			owner->SetHighColor(IsSelected()
				? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
				: owner->LowColor());
			owner->FillRect(bounds);
		}

		const float dotSize = 8.0f;
		BPoint dotOrigin = bounds.LeftTop()
			+ BPoint(be_control_look->DefaultLabelSpacing(),
				(bounds.Height() - dotSize) / 2.0f);
		rgb_color dotColor = fConnected
			? ui_color(B_SUCCESS_COLOR) : tint_color(owner->LowColor(),
				B_DARKEN_2_TINT);
		owner->SetHighColor(dotColor);
		owner->FillEllipse(BRect(dotOrigin,
			dotOrigin + BPoint(dotSize, dotSize)));

		BPoint namePoint = bounds.LeftTop() + BPoint(dotSize
			+ 2 * be_control_look->DefaultLabelSpacing(), fFirstLineOffset);
		BPoint statusPoint = bounds.LeftTop() + BPoint(dotSize
			+ 2 * be_control_look->DefaultLabelSpacing(),
			fFirstLineOffset + fLineOffset);

		owner->SetHighColor(IsSelected()
			? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR));
		owner->SetFont(be_bold_font);
		owner->DrawString(fDeviceName, namePoint);
		owner->SetFont(be_plain_font);
		owner->DrawString(fStatusText, statusPoint);

		owner->PopState();
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		BListItem::Update(owner, font);

		font_height height;
		font->GetHeight(&height);
		float lineHeight = ceilf(height.ascent) + ceilf(height.descent)
			+ ceilf(height.leading);
		fFirstLineOffset = 2 + ceilf(height.ascent + height.leading / 2);
		fLineOffset = lineHeight;

		SetHeight(std::max(2 * lineHeight + 4, 8.0f + 4));
	}

private:
	BString fDevicePath;
	BString fDeviceName;
	BString fStatusText;
	bool fConnected;
	float fFirstLineOffset;
	float fLineOffset;
};


// Row for a single NM VPN connection profile. Single-line, unlike
// DeviceListItem, since a VPN entry has no interface/driver line to show.
class VPNListItem : public BStringItem {
public:
	VPNListItem(const char* name, const char* connectionPath, bool connected)
		:
		BStringItem(name),
		fConnectionPath(connectionPath),
		fConnected(connected)
	{
	}

	const BString&	ConnectionPath() const { return fConnectionPath; }
	bool			Connected() const { return fConnected; }

	virtual void DrawItem(BView* owner, BRect bounds, bool complete)
	{
		owner->PushState();

		if (IsSelected() || complete) {
			owner->SetHighColor(IsSelected()
				? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
				: owner->LowColor());
			owner->FillRect(bounds);
		}

		const float dotSize = 8.0f;
		BPoint dotOrigin = bounds.LeftTop()
			+ BPoint(be_control_look->DefaultLabelSpacing(),
				(bounds.Height() - dotSize) / 2.0f);
		rgb_color dotColor = fConnected
			? ui_color(B_SUCCESS_COLOR) : tint_color(owner->LowColor(),
				B_DARKEN_2_TINT);
		owner->SetHighColor(dotColor);
		owner->FillEllipse(BRect(dotOrigin,
			dotOrigin + BPoint(dotSize, dotSize)));

		BPoint textPoint = bounds.LeftTop() + BPoint(dotSize
			+ 2 * be_control_look->DefaultLabelSpacing(),
			bounds.Height() / 2.0f + 4.0f);
		owner->SetHighColor(IsSelected()
			? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR));
		owner->DrawString(Text(), textPoint);

		owner->PopState();
	}

private:
	BString	fConnectionPath;
	bool	fConnected;
};


// #pragma mark -


NetworkWindowNM::NetworkWindowNM()
	:
	BWindow(BRect(100, 100, 750, 400), B_TRANSLATE_SYSTEM_NAME("Network"),
		B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fListView(NULL),
	fDetailView(NULL),
	fRevertButton(NULL),
	fServicesItem(NULL),
	fDialUpItem(NULL),
	fVPNItem(NULL),
	fOtherItem(NULL),
	fWiredItem(NULL),
	fWirelessItem(NULL)
{
	// Settings section
	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));

	BMessage* message = new BMessage(kMsgToggleReplicant);
	BCheckBox* showReplicantCheckBox = new BCheckBox("showReplicantCheckBox",
		B_TRANSLATE("Show network status in Deskbar"), message);
	showReplicantCheckBox->SetExplicitMaxSize(
		BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	showReplicantCheckBox->SetValue(_IsReplicantInstalled());

	fListView = new BOutlineListView("list", B_SINGLE_SELECTION_LIST,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS | B_NAVIGABLE);
	fListView->SetSelectionMessage(new BMessage(kMsgItemSelected));

	BScrollView* scrollView = new BScrollView("ScrollView", fListView,
		0, false, true);
	scrollView->SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNLIMITED));

	fDetailView = new InterfaceDetailView();

	// Build the layout: list on the left, swappable detail pane on the
	// right, Deskbar checkbox along the bottom.
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(scrollView)
			.Add(fDetailView)
		.End()
		.Add(showReplicantCheckBox)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fRevertButton)
			.AddGlue()
		.End();

	gNetworkWindow = this;

	// Placeholder shown until the async scan's reply arrives (see
	// MessageReceived's kMsgInitialDeviceScan handler below). Populating the
	// real list happens on message receipt, on this window's own thread --
	// never from inside the constructor, which runs before Show() and before
	// this window's looper is even attached. Calling NMBackend::GetDevices()
	// (blocking) from here was the same bug that kept the Bluetooth preflet
	// from ever appearing: ReadyToRun()/Show() would never be reached while
	// stuck waiting on a wedged or slow NetworkManager.
	fListView->AddItem(new BStringItem(B_TRANSLATE("Loading" B_UTF8_ELLIPSIS)));

	_UpdateRevertButton();

	CenterOnScreen();

	// Start watching NetworkManager for device changes
	NMBackend* backend = NMBackend::Instance();
	if (backend != NULL) {
		backend->StartWatching(BMessenger(this),
			NMBackend::NOTIFICATION_DEVICE_ADDED |
			NMBackend::NOTIFICATION_DEVICE_REMOVED |
			NMBackend::NOTIFICATION_DEVICE_STATE_CHANGED |
			NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
	}

	// Deferred, not called here: GetDevicesAsync() posts its reply back
	// through this window's message queue, which does not exist until the
	// looper is running -- PostMessage() here queues it for right after
	// Show()/Run(), the same pattern BluetoothWindow uses for kMsgInitialScan.
	PostMessage(kMsgInitialDeviceScan);
}


NetworkWindowNM::~NetworkWindowNM()
{
	// Stop watching NetworkManager
	NMBackend* backend = NMBackend::Instance();
	if (backend != NULL) {
		backend->StopWatching(BMessenger(this));
	}
}


bool
NetworkWindowNM::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
NetworkWindowNM::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgInitialDeviceScan:
			_RequestDeviceScan();
			break;

		case kMsgDevicesReady:
			_PopulateDeviceList(message);
			break;

		case kMsgDeviceInfoReady:
			fDetailView->SetToDevice(*message);
			_UpdateRevertButton();
			break;

		case kMsgRevert:
			_RevertSettings();
			break;

		case StaticIPView::kMsgDirtyChanged:
			_UpdateRevertButton();
			break;

		case kMsgToggleReplicant:
			_ToggleReplicant();
			break;

		case kMsgItemSelected:
		{
			int32 index = fListView->CurrentSelection();
			BListItem* item = fListView->ItemAt(index);
			_SelectItem(item);
			break;
		}

		case kMsgConnectDevice:
		{
			const char* devicePath;
			if (message->FindString("device_path", &devicePath) == B_OK) {
				NMBackend* backend = NMBackend::Instance();
				if (backend != NULL)
					backend->ConnectDevice(devicePath);
			}
			break;
		}

		case kMsgDisconnectDevice:
		{
			const char* devicePath;
			if (message->FindString("device_path", &devicePath) == B_OK) {
				NMBackend* backend = NMBackend::Instance();
				if (backend != NULL)
					backend->DisconnectDevice(devicePath);
			}
			break;
		}

		case kMsgRefreshDevices:
			_RequestDeviceScan();
			break;

		case NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED:
		{
			BString reason;
			if (message->FindString("reason", &reason) == B_OK
					&& !reason.IsEmpty()) {
				BString text(B_TRANSLATE("Network operation failed."));
				text << "\n" << reason;
				BAlert* alert = new BAlert(B_TRANSLATE("Network"),
					text.String(), B_TRANSLATE("OK"));
				alert->Go(NULL);
			}
			_RequestDeviceScan();
			break;
		}

		case NMBackend::NOTIFICATION_DEVICE_ADDED:
		case NMBackend::NOTIFICATION_DEVICE_REMOVED:
		case NMBackend::NOTIFICATION_DEVICE_STATE_CHANGED:
			_RequestDeviceScan();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
NetworkWindowNM::_RequestDeviceScan()
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;

	// Fires the request and returns immediately -- this must never block:
	// it is called from MessageReceived() on this window's own thread, and a
	// preflet whose window thread is stuck waiting on NetworkManager cannot
	// repaint, move, or close. The reply arrives as kMsgDevicesReady, handled
	// above by _PopulateDeviceList().
	backend->GetDevicesAsync(BMessenger(this), kMsgDevicesReady);
}


void
NetworkWindowNM::_PopulateDeviceList(BMessage* devices)
{
	// Capture the current selection's stable identifier (D-Bus object path)
	// before tearing the list down -- NM notifications repopulate this list
	// often, and re-selecting index 0 every time would throw away whatever
	// the user had selected on every unrelated state change elsewhere.
	BString previousPath;
	bool hadSelection = false;
	{
		BListItem* selected = fListView->ItemAt(fListView->CurrentSelection());
		DeviceListItem* deviceItem = dynamic_cast<DeviceListItem*>(selected);
		VPNListItem* vpnItem = dynamic_cast<VPNListItem*>(selected);
		if (deviceItem != NULL) {
			previousPath = deviceItem->DevicePath();
			hadSelection = true;
		} else if (vpnItem != NULL) {
			previousPath = vpnItem->ConnectionPath();
			hadSelection = true;
		}
	}

	// BListView does not own its items, so draining is the only way to avoid
	// leaking one set per refresh -- and NM notifications refresh often.
	BListItem* stale;
	while ((stale = fListView->RemoveItem((int32)0)) != NULL)
		delete stale;

	bool nmAvailable = true;
	devices->FindBool(kNMFieldNMAvailable, &nmAvailable);
	if (!nmAvailable) {
		// Unavailable, not empty: NetworkManager itself is not reachable,
		// so an empty Wired/WiFi/VPN list here would be a lie about why.
		BStringItem* unavailable = new BStringItem(
			B_TRANSLATE("NetworkManager is not running"));
		unavailable->SetEnabled(false);
		fListView->AddItem(unavailable);
		fDetailView->ShowEmpty(
			B_TRANSLATE("NetworkManager is not running"));
		_UpdateRevertButton();
		return;
	}

	// Create section headers
	fWiredItem = new TitleItem(B_TRANSLATE("Wired"));
	fWirelessItem = new TitleItem(B_TRANSLATE("WiFi"));
	fVPNItem = new TitleItem(B_TRANSLATE("VPN"));

	fListView->AddItem(fWiredItem);
	fListView->AddItem(fWirelessItem);
	fListView->AddItem(fVPNItem);

	BListItem* restoredSelection = NULL;

	int32 deviceCount = 0;
	if (devices->FindInt32(kNMFieldDeviceCount, &deviceCount) != B_OK)
		deviceCount = 0;

	for (int32 i = 0; i < deviceCount; i++) {
		char deviceName[32];
		snprintf(deviceName, sizeof(deviceName), "device_%" B_PRId32, i);

		BMessage deviceInfo;
		if (devices->FindMessage(deviceName, &deviceInfo) != B_OK)
			continue;

		const char* devicePath;
		const char* interfaceName;
		const char* deviceType;
		uint32 deviceState;

		if (deviceInfo.FindString(kNMFieldPath, &devicePath) != B_OK ||
			deviceInfo.FindString(kNMFieldInterface, &interfaceName) != B_OK ||
			deviceInfo.FindString(kNMFieldType, &deviceType) != B_OK ||
			deviceInfo.FindUInt32(kNMFieldState, &deviceState) != B_OK)
			continue;

		// Determine which section to add to
		BListItem* parentItem = NULL;
		if (strcmp(deviceType, "ethernet") == 0) {
			parentItem = fWiredItem;
		} else if (strcmp(deviceType, "wifi") == 0) {
			parentItem = fWirelessItem;
		} else if (strcmp(deviceType, "vpn") == 0) {
			parentItem = fVPNItem;
		}

		if (parentItem == NULL)
			continue;

		bool connected = deviceState == kNMDeviceStateActivated;
		BString statusText = connected
			? B_TRANSLATE("Connected") : B_TRANSLATE("Disconnected");

		DeviceListItem* item = new DeviceListItem(interfaceName, devicePath,
			statusText.String(), connected);

		fListView->AddUnder(item, parentItem);

		if (hadSelection && previousPath == devicePath)
			restoredSelection = item;
	}

	if (restoredSelection == NULL && hadSelection)
		restoredSelection = _PopulateVPNList(previousPath);
	else
		_PopulateVPNList(BString());

	// Note: BListItem visibility toggling is private/friend-only in this
	// tree (BOutlineListView/BListView only); empty sections are simply
	// left as empty headers rather than hidden.

	if (restoredSelection != NULL) {
		fListView->Select(fListView->IndexOf(restoredSelection));
		_SelectItem(restoredSelection);
	} else {
		fListView->Select(0);
		_SelectItem(fListView->ItemAt(0));
	}

	// Set size of the list view from its contents
	float width;
	float height;
	fListView->GetPreferredSize(&width, &height);
	width += 2 * be_control_look->DefaultItemSpacing();
	fListView->SetExplicitSize(BSize(width, B_SIZE_UNSET));
	fListView->SetExplicitMinSize(BSize(width, std::min(height, 400.f)));
}


// Returns the newly-created VPNListItem whose connection path matches
// previousSelectionPath, or NULL if it's empty or nothing matched -- lets the
// caller restore the previously-selected VPN row across a repopulate the
// same way it does for devices.
BListItem*
NetworkWindowNM::_PopulateVPNList(const BString& previousSelectionPath)
{
	if (fVPNItem == NULL)
		return NULL;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return NULL;

	BMessage vpns;
	if (backend->GetVPNConnections(&vpns) != B_OK)
		return NULL;

	int32 count = 0;
	vpns.FindInt32(kNMFieldVPNCount, &count);

	BListItem* matched = NULL;

	for (int32 i = 0; i < count; i++) {
		char vpnName[32];
		snprintf(vpnName, sizeof(vpnName), "vpn_%" B_PRId32, i);

		BMessage vpnInfo;
		if (vpns.FindMessage(vpnName, &vpnInfo) != B_OK)
			continue;

		const char* name;
		const char* path;
		if (vpnInfo.FindString(kNMFieldVPNName, &name) != B_OK
			|| vpnInfo.FindString(kNMFieldVPNPath, &path) != B_OK)
			continue;
		bool connected = false;
		vpnInfo.FindBool(kNMFieldVPNConnected, &connected);

		VPNListItem* item = new VPNListItem(name, path, connected);
		fListView->AddUnder(item, fVPNItem);

		if (!previousSelectionPath.IsEmpty() && previousSelectionPath == path)
			matched = item;
	}

	return matched;
}


void
NetworkWindowNM::_SelectItem(BListItem* item)
{
	DeviceListItem* deviceItem = dynamic_cast<DeviceListItem*>(item);
	VPNListItem* vpnItem = dynamic_cast<VPNListItem*>(item);
	if (deviceItem != NULL) {
		fDetailView->ShowEmpty(B_TRANSLATE("Loading" B_UTF8_ELLIPSIS));

		NMBackend* backend = NMBackend::Instance();
		if (backend != NULL) {
			// Async: never block this window's thread on the backend.
			backend->GetDeviceInfoAsync(deviceItem->DevicePath().String(),
				BMessenger(this), kMsgDeviceInfoReady);
		}
	} else if (vpnItem != NULL) {
		BMessage vpnInfo;
		vpnInfo.AddString(kNMFieldVPNName, vpnItem->Text());
		vpnInfo.AddString(kNMFieldVPNPath, vpnItem->ConnectionPath());
		vpnInfo.AddBool(kNMFieldVPNConnected, vpnItem->Connected());
		fDetailView->SetToVPN(vpnInfo);
	} else {
		fDetailView->ShowEmpty(item != NULL
			? B_TRANSLATE("Not yet implemented")
			: B_TRANSLATE("Select a device"));
	}

	_UpdateRevertButton();
}


// Revert is scoped to the detail pane's editable fields (currently just
// StaticIPView) and enabled only when that pane is dirty against the
// snapshot taken when it was populated -- not the permanent SetEnabled(false)
// stub this used to be.
void
NetworkWindowNM::_UpdateRevertButton()
{
	bool dirty = fDetailView != NULL && fDetailView->IsRevertable();
	fRevertButton->SetEnabled(dirty);
	fRevertButton->SetToolTip(dirty
		? B_TRANSLATE("Discard unapplied IPv4 changes")
		: B_TRANSLATE("No unapplied changes"));
}


void
NetworkWindowNM::_RevertSettings()
{
	if (fDetailView != NULL)
		fDetailView->Revert();
	_UpdateRevertButton();
}


// Name the NetworkStatusView archives itself under (see
// NetworkStatusViewNM.cpp's BView constructor) -- BDeskbar's item-lookup
// API here works by that name rather than by entry_ref/signature.
static const char* kNetworkStatusDeskbarItemName = "NetworkStatus";


void
NetworkWindowNM::_ToggleReplicant()
{
	BDeskbar deskbar;
	status_t status = B_OK;

	if (_IsReplicantInstalled()) {
		status = deskbar.RemoveItem(kNetworkStatusDeskbarItemName);
		if (status != B_OK) {
			BAlert* alert = new BAlert(B_TRANSLATE("Error"),
				B_TRANSLATE("Couldn't remove the network status item from "
					"the Deskbar."),
				B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->Go();
		}
	} else {
		entry_ref ref;
		status = be_roster->FindApp(kNetworkStatusSignature, &ref);
		if (status == B_OK)
			status = deskbar.AddItem(&ref);

		if (status != B_OK) {
			BString text(B_TRANSLATE("Couldn't add the network status item "
				"to the Deskbar: %error%"));
			text.ReplaceFirst("%error%", strerror(status));
			BAlert* alert = new BAlert(B_TRANSLATE("Error"), text,
				B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
				B_WARNING_ALERT);
			alert->Go();
		}
	}

	// Re-sync regardless of outcome -- don't let the checkbox show
	// "checked" when the add/remove actually failed. The checkbox isn't a
	// stored member, so look it up by the name it was constructed with.
	BCheckBox* checkBox
		= dynamic_cast<BCheckBox*>(FindView("showReplicantCheckBox"));
	if (checkBox != NULL)
		checkBox->SetValue(_IsReplicantInstalled());
}


bool
NetworkWindowNM::_IsReplicantInstalled()
{
	BDeskbar deskbar;
	return deskbar.HasItem(kNetworkStatusDeskbarItemName);
}
