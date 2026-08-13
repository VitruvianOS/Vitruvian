/*
 * Copyright 2006-2013, Haiku, Inc.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NetworkStatusView.h"
#include "NMBackend.h"
#include "NetworkStatus.h"
#include "NetworkStatusIcons.h"
#include "RadioView.h"
#include "WirelessNetworkMenuItem.h"
#include "ConnectionInfoWindow.h"
#include "SecretDialogWindow.h"

#include <Menu.h>
#include <StringItem.h>

#include <new>
#include <string.h>

#include <AboutWindow.h>
#include <Application.h>
#include <Alert.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <IconUtils.h>
#include <Resources.h>
#include <Roster.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NetworkStatus"


static const float kDeskbarHeight = 20.0f;
static const float kIconWidth = 16.0f;
static const float kIconHeight = 16.0f;


extern "C" BView* instantiate_deskbar_item(float maxWidth, float maxHeight);


NetworkStatusView::NetworkStatusView(BRect frame, int32 resizingMode, bool inDeskbar)
	:
	BView(frame, kDeskbarItemName, resizingMode,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND | B_FRAME_EVENTS),
	fDeviceIndex(-1),
	fConnected(false),
	fHasDevice(false),
	fSignalStrength(-1),
	fInDeskbar(inDeskbar)
{
	memset(fIcons, 0, sizeof(fIcons));

	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(ViewColor());

	if (!inDeskbar) {
		frame.OffsetTo(B_ORIGIN);
		frame.top = frame.bottom - 7;
		frame.left = frame.right - 7;
		BDragger* dragger = new BDragger(frame, this,
			B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		AddChild(dragger);
	}
}


NetworkStatusView::NetworkStatusView(BMessage* archive)
	:
	BView(archive),
	fDeviceIndex(-1),
	fConnected(false),
	fHasDevice(false),
	fSignalStrength(-1),
	fInDeskbar(false)
{
	memset(fIcons, 0, sizeof(fIcons));

	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK
		&& !strcasecmp(info.signature, "application/x-vnd.Be-TSKB"))
		fInDeskbar = true;

	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(ViewColor());
}


NetworkStatusView::~NetworkStatusView()
{
	for (size_t i = 0; i < sizeof(fIcons) / sizeof(fIcons[0]); i++)
		delete fIcons[i];
}


NetworkStatusView*
NetworkStatusView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "NetworkStatusView"))
		return NULL;

	return new NetworkStatusView(archive);
}


status_t
NetworkStatusView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kSignature);
	if (status == B_OK)
		status = archive->AddString("class", "NetworkStatusView");

	return status;
}


void
NetworkStatusView::AttachedToWindow()
{
	BView::AttachedToWindow();

	NMBackend* backend = NMBackend::Instance();
	if (backend != NULL) {
		backend->StartWatching(BMessenger(this),
			NMBackend::NOTIFICATION_DEVICE_ADDED |
			NMBackend::NOTIFICATION_DEVICE_REMOVED |
			NMBackend::NOTIFICATION_DEVICE_STATE_CHANGED |
			NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED |
			NMBackend::NOTIFICATION_WIFI_NETWORK_FOUND |
			NMBackend::NOTIFICATION_SIGNAL_STRENGTH_CHANGED);

		backend->RegisterSecretAgentAsync(BMessenger(this), BMessenger(this),
			kMsgAgentRegistered);
	}

	_RequestStatusUpdate();
}


void
NetworkStatusView::DetachedFromWindow()
{
	NMBackend* backend = NMBackend::Instance();
	if (backend != NULL) {
		backend->StopWatching(BMessenger(this));
		backend->UnregisterSecretAgentAsync(BMessenger(), 0);
	}

	BView::DetachedFromWindow();
}


void
NetworkStatusView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	Invalidate();
}


void
NetworkStatusView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgUpdateStatus:
			_RequestStatusUpdate();
			break;

		case kMsgStatusReady:
			_ApplyStatusUpdate(message);
			break;

		case kMsgConnectDevice:
			{
				const char* devicePath;
				if (message->FindString("device_path", &devicePath) == B_OK) {
					NMBackend* backend = NMBackend::Instance();
					if (backend != NULL && backend->ConnectDevice(devicePath) != B_OK) {
						_ShowOperationFailedAlert(B_TRANSLATE(
							"Could not start connecting: no such device."));
					}
				}
			}
			break;

		case kMsgDisconnectDevice:
			{
				const char* devicePath;
				if (message->FindString("device_path", &devicePath) == B_OK) {
					NMBackend* backend = NMBackend::Instance();
					if (backend != NULL && backend->DisconnectDevice(devicePath) != B_OK) {
						_ShowOperationFailedAlert(B_TRANSLATE(
							"Could not start disconnecting: no such device."));
					}
				}
			}
			break;
			
		case kMsgScanWiFi:
			{
				const char* devicePath;
				if (message->FindString("device_path", &devicePath) == B_OK) {
					_ScanWiFiNetworks(devicePath);
				}
			}
			break;
			
		case kMsgConnectWiFi:
			{
				const char* devicePath;
				const char* ssid;
				if (message->FindString("device_path", &devicePath) == B_OK &&
					message->FindString("ssid", &ssid) == B_OK) {
					const char* password = NULL;
					message->FindString("password", &password);

					NMBackend* backend = NMBackend::Instance();
					if (backend != NULL) {
						backend->ConnectToWiFiAsync(devicePath, ssid, password,
							NULL, true, BMessenger(this),
							kMsgConnectWiFiResult);
					}
				}
			}
			break;

		case kMsgConnectVPN:
			{
				const char* connectionPath;
				if (message->FindString("connection_path", &connectionPath)
						== B_OK) {
					NMBackend* backend = NMBackend::Instance();
					if (backend != NULL)
						backend->ConnectVPN(connectionPath);
				}
			}
			break;

		case kMsgDisconnectVPN:
			{
				const char* connectionPath;
				if (message->FindString("connection_path", &connectionPath)
						== B_OK) {
					NMBackend* backend = NMBackend::Instance();
					if (backend != NULL)
						backend->DisconnectVPN(connectionPath);
				}
			}
			break;

		case kMsgConnectWiFiResult:
			{
				int32 status = B_ERROR;
				message->FindInt32("status", &status);
				if (status != B_OK) {
					BString reason;
					message->FindString("reason", &reason);
					BString text(B_TRANSLATE("Could not join the Wi-Fi "
						"network."));
					if (!reason.IsEmpty()) {
						text << "\n" << reason;
					}
					_ShowOperationFailedAlert(text.String());
				}
			}
			break;

		case NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED:
			{
				BString reason;
				if (message->FindString("reason", &reason) == B_OK
						&& !reason.IsEmpty()) {
					BString text(B_TRANSLATE("Network operation failed."));
					text << "\n" << reason;
					_ShowOperationFailedAlert(text.String());
				}
			}
			_RequestStatusUpdate();
			break;

		case NMBackend::NOTIFICATION_DEVICE_ADDED:
		case NMBackend::NOTIFICATION_DEVICE_REMOVED:
		case NMBackend::NOTIFICATION_DEVICE_STATE_CHANGED:
		case NMBackend::NOTIFICATION_WIFI_NETWORK_FOUND:
		case NMBackend::NOTIFICATION_SIGNAL_STRENGTH_CHANGED:
			_RequestStatusUpdate();
			break;

		case kMsgOpenNetworkPreferences:
			be_roster->Launch("application/x-vnd.Haiku-Network");
			break;

		case kMsgToggleNetworking:
			{
				NMBackend* backend = NMBackend::Instance();
				if (backend != NULL)
					backend->SetNetworkingEnabled(!backend->IsNetworkingEnabled());
			}
			break;

		case kMsgToggleWireless:
			{
				NMBackend* backend = NMBackend::Instance();
				if (backend != NULL)
					backend->SetWirelessEnabled(!backend->IsWirelessEnabled());
			}
			break;

		case kMsgConnectionInfo:
			_ConnectionInfoRequested();
			break;

		case kMsgSecretResult:
			_HandleSecretResult(message);
			break;

		case kMsgAgentRequest:
		{
			int32 kind;
			if (message->FindInt32("kind", &kind) == B_OK)
				_OpenSecretDialog((secret_dialog_kind)kind, *message);
			break;
		}

		case kMsgAgentCancel:
			if (fSecretDialogTarget.IsValid())
				fSecretDialogTarget.SendMessage(B_QUIT_REQUESTED);
			break;

		case kMsgAgentRegistered:
			break;

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
		case kMsgDebugOpenSecretDialog:
		{
			int32 kind;
			if (message->FindInt32("kind", &kind) == B_OK) {
				bool requestNew = false;
				message->FindBool("request_new", &requestNew);

				BMessage request;
				request.AddString("ssid", "Sample Network");
				request.AddString("method", "PEAP");
				request.AddString("missing_file", "/boot/home/config/settings/"
					"ssl/client.pem");
				request.AddBool("request_new", requestNew);
				_OpenSecretDialog((secret_dialog_kind)kind, request);
			}
			break;
		}
#endif

		case B_ABOUT_REQUESTED:
			_AboutRequested();
			break;

		case B_QUIT_REQUESTED:
			_Quit();
			break;


		default:
			BView::MessageReceived(message);
			break;
	}
}


void
NetworkStatusView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();

	drawing_mode oldMode = DrawingMode();
	SetDrawingMode(B_OP_ALPHA);

	_DrawNetworkIcon(bounds);

	SetDrawingMode(oldMode);
}


BBitmap*
NetworkStatusView::_GetIcon(int32 iconID)
{
	int32 index = iconID - kNetworkStatusNoDevice;
	if (index < 0 || (size_t)index >= sizeof(fIcons) / sizeof(fIcons[0]))
		return NULL;

	if (fIcons[index] != NULL)
		return fIcons[index];

	BResources resources;
	if (resources.SetToImage((void*)&instantiate_deskbar_item) != B_OK)
		return NULL;

	size_t size = 0;
	const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE, iconID,
		&size);
	if (data == NULL)
		return NULL;

	BBitmap* icon = new(std::nothrow) BBitmap(BRect(0, 0, kIconWidth - 1,
		kIconHeight - 1), B_RGBA32);
	if (icon == NULL || icon->InitCheck() != B_OK
		|| BIconUtils::GetVectorIcon((const uint8*)data, size, icon) != B_OK) {
		delete icon;
		return NULL;
	}

	fIcons[index] = icon;
	return icon;
}


void
NetworkStatusView::_DrawNetworkIcon(BRect bounds)
{
	if (fConnected && fDeviceType == "wifi" && fSignalStrength >= 0) {
		RadioView::Draw(this, bounds, fSignalStrength,
			RadioView::DefaultMax());
		return;
	}

	int32 iconID;
	if (!fHasDevice)
		iconID = kNetworkStatusNoDevice;
	else if (fConnected)
		iconID = kNetworkStatusReady;
	else
		iconID = kNetworkStatusNoConnection;

	BBitmap* icon = _GetIcon(iconID);
	if (icon != NULL)
		DrawBitmap(icon, bounds);
}


void
NetworkStatusView::_RequestStatusUpdate()
{
	NMBackend* backend = NMBackend::Instance();
	if (backend != NULL)
		backend->GetDevicesAsync(BMessenger(this), kMsgStatusReady);
}


void
NetworkStatusView::_ApplyStatusUpdate(BMessage* devices)
{
	fDevices = *devices;

	int32 deviceCount = 0;
	if (devices->FindInt32(kNMFieldDeviceCount, &deviceCount) != B_OK)
		deviceCount = 0;

	fHasDevice = deviceCount > 0;

	for (int32 i = 0; i < deviceCount; i++) {
		char deviceName[32];
		snprintf(deviceName, sizeof(deviceName), "device_%" B_PRId32, i);

		BMessage deviceInfo;
		if (devices->FindMessage(deviceName, &deviceInfo) != B_OK)
			continue;

		const char* devicePath;
		if (deviceInfo.FindString(kNMFieldPath, &devicePath) != B_OK)
			continue;

		uint32 deviceState;
		if (deviceInfo.FindUInt32(kNMFieldState, &deviceState) != B_OK)
			continue;

		if (deviceState == kNMDeviceStateActivated) {
			fConnected = true;
			fDevicePath = devicePath;

			const char* deviceType;
			if (deviceInfo.FindString(kNMFieldType, &deviceType) == B_OK)
				fDeviceType = deviceType;
			else
				fDeviceType = "";

			if (fDeviceType == "wifi") {
				uint32 signalStrength = 0;
				deviceInfo.FindUInt32("signal_strength", &signalStrength);
				fSignalStrength = (int)signalStrength;
			} else {
				fSignalStrength = -1;
			}

			Invalidate();
			return;
		}
	}

	fConnected = false;
	fDevicePath = "";
	fDeviceType = "";
	fSignalStrength = -1;
	Invalidate();
}


void
NetworkStatusView::_ScanWiFiNetworks(const char* devicePath)
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;
	
	BMessage networks;
	if (backend->ScanWiFiNetworks(devicePath, &networks) != B_OK)
		return;
	
	// TODO: Show WiFi networks in popup menu
}


void
NetworkStatusView::MouseDown(BPoint where)
{
	// Upstream NetworkStatusView (haiku-latest) builds exactly one menu per
	// click regardless of button, appending Quit only when hosted in the
	// tray -- it does not branch into two independently-constructed popups
	// the way this file previously did. That branch is not just a style
	// difference: with two different BPopUpMenu instances possible from the
	// same MouseDown, Deskbar's own TExpandoMenuBar was observed tracking
	// concurrently with ours (two menu-tracking threads snoozing at once,
	// fighting for the mouse grab -- the desktop looked frozen with nothing
	// actually deadlocked). Matching upstream's single-menu shape removes
	// the divergence.
	_ShowMenu(where);
}


void
NetworkStatusView::_ShowOperationFailedAlert(const char* message)
{
	// Heap-allocated, async Go(): a synchronous BAlert::Go() blocks the
	// calling thread until dismissed, which in the tray is Deskbar's window
	// thread. An operation failing must never blank the device list or the
	// menu that triggered it -- this is a transient notice only.
	BAlert* alert = new BAlert(B_TRANSLATE("Not supported"), message,
		B_TRANSLATE("OK"));
	alert->Go(NULL);
}


void
NetworkStatusView::_ShowMenu(BPoint where)
{
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
	menu->SetAsyncAutoDestruct(true);
	menu->SetFont(be_plain_font);

	// Built from the snapshot last delivered by GetDevicesAsync(), not a
	// fresh synchronous query -- this runs on the host window thread, which
	// in the tray is Deskbar's, and must never block on the backend.
	bool nmAvailable = true;
	fDevices.FindBool(kNMFieldNMAvailable, &nmAvailable);
	if (!nmAvailable) {
		BMenuItem* unavailable = new BMenuItem(
			B_TRANSLATE("NetworkManager is not running"), NULL);
		unavailable->SetEnabled(false);
		menu->AddItem(unavailable);
		menu->AddSeparatorItem();
	}

	int32 deviceCount = 0;
	if (nmAvailable && fDevices.FindInt32(kNMFieldDeviceCount, &deviceCount) == B_OK) {
		for (int32 i = 0; i < deviceCount; i++) {
			char deviceName[32];
			snprintf(deviceName, sizeof(deviceName), "device_%" B_PRId32, i);

			BMessage deviceInfo;
			if (fDevices.FindMessage(deviceName, &deviceInfo) != B_OK)
				continue;

			const char* devicePath;
			const char* interfaceName;
			uint32 deviceState;

			if (deviceInfo.FindString(kNMFieldPath, &devicePath) != B_OK ||
				deviceInfo.FindString(kNMFieldInterface, &interfaceName) != B_OK ||
				deviceInfo.FindUInt32(kNMFieldState, &deviceState) != B_OK)
				continue;

			// A status line per device -- plain text, not a target.
			BString statusLine = interfaceName;
			statusLine << ": ";
			statusLine << (deviceState == kNMDeviceStateActivated
				? "Connected" : "Not connected");
			BMenuItem* statusItem = new BMenuItem(statusLine.String(), NULL);
			statusItem->SetEnabled(false);
			menu->AddItem(statusItem);

			const char* deviceType;
			if (deviceInfo.FindString(kNMFieldType, &deviceType) != B_OK)
				deviceType = "unknown";

			if (strcmp(deviceType, "wifi") == 0) {
				_BuildWiFiSection(menu, devicePath);
			} else if (deviceState != kNMDeviceStateActivated) {
				BMessage* msg = new BMessage(kMsgConnectDevice);
				msg->AddString("device_path", devicePath);
				menu->AddItem(new BMenuItem("Connect", msg));
			} else {
				BMessage* msg = new BMessage(kMsgDisconnectDevice);
				msg->AddString("device_path", devicePath);
				menu->AddItem(new BMenuItem("Disconnect", msg));
			}

			menu->AddSeparatorItem();
		}
	}

	_BuildVPNSection(menu);

	NMBackend* backend = NMBackend::Instance();

	BMenuItem* networkingItem = new BMenuItem("Enable Networking",
		new BMessage(kMsgToggleNetworking));
	networkingItem->SetMarked(backend != NULL
		&& backend->IsNetworkingEnabled());
	menu->AddItem(networkingItem);

	BMenuItem* wirelessItem = new BMenuItem("Enable Wi-Fi",
		new BMessage(kMsgToggleWireless));
	wirelessItem->SetMarked(backend != NULL && backend->IsWirelessEnabled());
	menu->AddItem(wirelessItem);

	menu->AddSeparatorItem();
	BMenuItem* infoItem = new BMenuItem(
		"Connection Information" B_UTF8_ELLIPSIS,
		new BMessage(kMsgConnectionInfo));
	infoItem->SetEnabled(fHasDevice && fConnected);
	menu->AddItem(infoItem);

	menu->AddItem(new BMenuItem("Open Network Preferences" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOpenNetworkPreferences)));

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
	menu->AddSeparatorItem();
	_BuildDebugDialogMenu(menu);
#endif

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("About NetworkStatus" B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED)));

	if (fInDeskbar) {
		menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED)));
	}

	// Without this the items invoke against the window -- which in the tray is
	// Deskbar, so the messages would never reach MessageReceived() below.
	menu->SetTargetForItems(this);

	ConvertToScreen(&where);
	// Async + heap + auto-destruct, as upstream does. A synchronous Go()
	// blocks the calling thread until the menu closes -- in the tray that is
	// Deskbar's window thread, so the whole desktop freezes while it is up.
	menu->Go(where, true, true, true);
}


void
NetworkStatusView::_BuildWiFiSection(BMenu* menu, const char* devicePath)
{
	NMBackend* backend = NMBackend::Instance();
	BMessage networks;
	int32 count = 0;
	if (backend != NULL)
		backend->ScanWiFiNetworks(devicePath, &networks);
	networks.FindInt32(kNMFieldAPCount, &count);

	if (count == 0) {
		BMenuItem* empty = new BMenuItem(
			B_TRANSLATE("No networks in range"), NULL);
		empty->SetEnabled(false);
		menu->AddItem(empty);
		return;
	}

	// A dedicated submenu, not rows spliced into the parent: SortItems()
	// below reorders whatever menu it is called on, and the parent already
	// has the device status line and Connect/Disconnect items ahead of this
	// section that must not move.
	BMenu* wifiMenu = new BMenu(B_TRANSLATE("Wi-Fi networks"));
	wifiMenu->SetFont(be_plain_font);

	for (int32 i = 0; i < count; i++) {
		char apName[32];
		snprintf(apName, sizeof(apName), "ap_%" B_PRId32, i);
		BMessage apInfo;
		if (networks.FindMessage(apName, &apInfo) != B_OK)
			continue;

		const char* ssid;
		int32 strength = 0;
		bool secured = false;
		bool connected = false;
		if (apInfo.FindString(kNMFieldAPSSID, &ssid) != B_OK)
			continue;
		apInfo.FindInt32(kNMFieldAPStrength, &strength);
		apInfo.FindBool(kNMFieldAPSecured, &secured);
		apInfo.FindBool(kNMFieldAPConnected, &connected);

		BMessage* msg = new BMessage(kMsgConnectWiFi);
		msg->AddString("device_path", devicePath);
		msg->AddString("ssid", ssid);

		WirelessNetworkMenuItem* item = new WirelessNetworkMenuItem(ssid,
			strength, secured ? B_TRANSLATE("Secured") : "", connected, msg);
		wifiMenu->AddItem(item);
	}

	wifiMenu->SortItems(WirelessNetworkMenuItem::CompareSignalStrength);
	// SetTargetForItems() on the parent below does not recurse into
	// submenus -- without this, the WirelessNetworkMenuItems invoke against
	// their window (Deskbar in the tray), the exact "unrooted items" bug
	// already hit once with About/Quit.
	wifiMenu->SetTargetForItems(this);
	menu->AddItem(wifiMenu);
}


void
NetworkStatusView::_BuildVPNSection(BMenu* menu)
{
	NMBackend* backend = NMBackend::Instance();
	BMessage vpns;
	int32 count = 0;
	if (backend != NULL)
		backend->GetVPNConnections(&vpns);
	vpns.FindInt32(kNMFieldVPNCount, &count);

	if (count == 0) {
		BMenuItem* empty = new BMenuItem(
			B_TRANSLATE("No VPN connections configured"), NULL);
		empty->SetEnabled(false);
		menu->AddItem(empty);
		menu->AddSeparatorItem();
		return;
	}

	for (int32 i = 0; i < count; i++) {
		char vpnName[32];
		snprintf(vpnName, sizeof(vpnName), "vpn_%" B_PRId32, i);
		BMessage vpnInfo;
		if (vpns.FindMessage(vpnName, &vpnInfo) != B_OK)
			continue;

		const char* name;
		const char* path;
		bool connected = false;
		if (vpnInfo.FindString(kNMFieldVPNName, &name) != B_OK
				|| vpnInfo.FindString(kNMFieldVPNPath, &path) != B_OK)
			continue;
		vpnInfo.FindBool(kNMFieldVPNConnected, &connected);

		BMessage* msg = new BMessage(connected
			? kMsgDisconnectVPN : kMsgConnectVPN);
		msg->AddString("connection_path", path);

		BString label = name;
		label << (connected ? B_TRANSLATE(" (connected)") : "");
		BMenuItem* item = new BMenuItem(label.String(), msg);
		item->SetMarked(connected);
		menu->AddItem(item);
	}

	menu->AddSeparatorItem();
}


void
NetworkStatusView::_ConnectionInfoRequested()
{
	if (!fHasDevice || fDevicePath.Length() == 0)
		return;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;

	// Heap-allocated: BWindow runs on its own thread and deletes itself.
	// Opens immediately with a Loading placeholder; GetDeviceInfoAsync()'s
	// reply populates it later. Never call the synchronous GetDeviceInfo()
	// from here -- this runs on Deskbar's window thread.
	ConnectionInfoWindow* window
		= new ConnectionInfoWindow(fDevicePath.String());
	window->Show();

	backend->GetDeviceInfoAsync(fDevicePath.String(), BMessenger(window),
		ConnectionInfoWindow::kMsgDeviceInfoReply);
}


void
NetworkStatusView::_OpenSecretDialog(secret_dialog_kind kind,
	const BMessage& request)
{
	// One live secret dialog at a time: cancel whatever is open first.
	// The old window's own QuitRequested() sends its cancel result before it
	// closes -- a normal cancel from the entry point's perspective.
	if (fSecretDialogTarget.IsValid())
		fSecretDialogTarget.SendMessage(B_QUIT_REQUESTED);

	SecretDialogWindow* window = new SecretDialogWindow(kind, request,
		BMessenger(this), kMsgSecretResult);
	fSecretDialogTarget = BMessenger(window);
	window->Show();
}


void
NetworkStatusView::_HandleSecretResult(BMessage* message)
{
	// The single entry point the real SecretAgent.GetSecrets reply wires
	// into.
	fSecretDialogTarget = BMessenger();

	uint32 requestId = 0;
	if (message->FindUInt32("request_id", &requestId) == B_OK
			&& requestId != 0) {
		bool connect = false;
		bool remember = false;
		BString password, identity;
		message->FindBool("connect", &connect);
		message->FindBool("remember", &remember);
		message->FindString("password", &password);
		message->FindString("identity", &identity);

		NMBackend* backend = NMBackend::Instance();
		if (backend != NULL) {
			backend->CompleteSecretRequest(requestId, connect, password,
				identity, remember);
		}
	}

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
	int32 kind = -1;
	bool connect = false;
	bool remember = false;
	BString password, identity;
	message->FindInt32("kind", &kind);
	message->FindBool("connect", &connect);
	message->FindBool("remember", &remember);
	message->FindString("password", &password);
	message->FindString("identity", &identity);
	fprintf(stderr, "NetworkStatus: secret dialog kind=%d connect=%d "
		"remember=%d identity=\"%s\"\n", (int)kind, connect, remember,
		identity.String());
#endif
}


#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
void
NetworkStatusView::_BuildDebugDialogMenu(BMenu* parent)
{
	// Test trigger only: opens each secret dialog with sample content so all
	// five can be reviewed side-by-side against GNOME/XFCE/Haiku. Guarded by
	// a CMake option that defaults off, not a runtime switch, so it cannot
	// ship enabled by accident.
	static const struct {
		const char* label;
		secret_dialog_kind kind;
		bool requestNew;
	} kItems[] = {
		{ "WPA/WPA2/WPA3 PSK" B_UTF8_ELLIPSIS, kSecretWPAPSK, false },
		{ "WPA/WPA2/WPA3 PSK (rejected key)" B_UTF8_ELLIPSIS, kSecretWPAPSK, true },
		{ "WEP" B_UTF8_ELLIPSIS, kSecretWEP, false },
		{ "WPA-Enterprise" B_UTF8_ELLIPSIS, kSecretEnterprise, false },
		{ "Wired 802.1x" B_UTF8_ELLIPSIS, kSecretWired8021x, false },
		{ "Missing certificate" B_UTF8_ELLIPSIS, kSecretMissingCertificate, false },
	};

	BMenu* menu = new BMenu("Test secret dialogs (debug)");
	for (size_t i = 0; i < sizeof(kItems) / sizeof(kItems[0]); i++) {
		BMessage* msg = new BMessage(kMsgDebugOpenSecretDialog);
		msg->AddInt32("kind", (int32)kItems[i].kind);
		msg->AddBool("request_new", kItems[i].requestNew);
		menu->AddItem(new BMenuItem(kItems[i].label, msg));
	}
	menu->SetTargetForItems(this);
	parent->AddItem(menu);
}
#endif


void
NetworkStatusView::_AboutRequested()
{
	// Heap-allocated on purpose: a BWindow runs on its own thread and deletes
	// itself when it quits. A stack instance would be destroyed here, on the
	// host looper's thread, without holding its lock.
	BAboutWindow* window = new BAboutWindow(
		B_TRANSLATE_SYSTEM_NAME("NetworkStatus"), kSignature);

	window->AddCopyright(2026, "Dario Casalinuovo");
	window->AddDescription(B_TRANSLATE("Network status indicator"));

	window->Show();
}


int32
NetworkStatusView::_RemoveFromDeskbarThread(void*)
{
	BDeskbar deskbar;
	deskbar.RemoveItem(kDeskbarItemName);
	return 0;
}


void
NetworkStatusView::_Quit()
{
	// In the tray "Quit" means remove the replicant -- posting
	// B_QUIT_REQUESTED on to our looper would ask Deskbar itself to quit.
	if (fInDeskbar) {
		// BDeskbar::RemoveItem's untimed SendMessage targets Deskbar's own
		// team, which this replicant runs inside. Calling it inline here
		// would block this window thread, mid-dispatch, on a port nobody is
		// left to drain -- an actual deadlock reproduced on a real VM, not a
		// theoretical one. Hand it to a detached thread instead so this
		// handler returns immediately.
		thread_id thread = spawn_thread(_RemoveFromDeskbarThread,
			"remove_networkstatus_item", B_NORMAL_PRIORITY, NULL);
		if (thread >= B_OK)
			resume_thread(thread);
	} else
		be_app->PostMessage(B_QUIT_REQUESTED);
}


// NetworkStatus replicant instantiation


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	// Square, driven by height: maxWidth is the whole tray width, and a
	// replicant wider than one row makes TReplicantTray::LocationForReplicant()
	// loop forever looking for a row it fits in.
	return new NetworkStatusView(BRect(0, 0, maxHeight - 1, maxHeight - 1),
		B_FOLLOW_LEFT | B_FOLLOW_TOP, true);
}


extern "C" _EXPORT status_t
instantiate_deskbar_item_for_width(float* width, float* height)
{
	*width = kIconWidth;
	*height = kIconHeight;
	return B_OK;
}