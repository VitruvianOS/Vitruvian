/*
 * Copyright 2006-2013, Haiku, Inc.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "BluetoothStatusView.h"

#include <algorithm>
#include <new>
#include <set>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <AboutWindow.h>
#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Bitmap.h>
#include <Deskbar.h>
#include <Dragger.h>
#include <IconUtils.h>
#include <Locale.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>
#include <TextView.h>

#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>

#include "BluetoothStatus.h"
#include "PairingDialogWindow.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothStatus"


static const float kDeskbarHeight = 20.0f;
static const float kIconWidth = 16.0f;
static const float kIconHeight = 16.0f;


// Defined at file scope below; forward-declared so _GetTrayIcon() can use
// its address to resolve this add-on's own image for BResources::SetToImage().
extern "C" BView* instantiate_deskbar_item(float maxWidth, float maxHeight);


BluetoothStatusView::BluetoothStatusView(BRect frame, int32 resizingMode, bool inDeskbar)
	:
	BView(frame, kDeskbarItemName, resizingMode,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND | B_FRAME_EVENTS),
	fAdapterCount(0),
	fPowered(false),
	fConnected(false),
	fDiscovering(false),
	fPulsePhase(false),
	fInDeskbar(inDeskbar),
	fTrayIcon(NULL)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(ViewColor());

	if (!inDeskbar) {
		// not in the tray: add the dragger that lets us be dragged into a shelf
		frame.OffsetTo(B_ORIGIN);
		frame.top = frame.bottom - 7;
		frame.left = frame.right - 7;
		BDragger* dragger = new BDragger(frame, this,
			B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM);
		AddChild(dragger);
	}
}


BluetoothStatusView::BluetoothStatusView(BMessage* archive)
	:
	BView(archive),
	fAdapterCount(0),
	fPowered(false),
	fConnected(false),
	fDiscovering(false),
	fPulsePhase(false),
	fInDeskbar(false),
	fTrayIcon(NULL)
{
	// Deskbar restores saved replicants through this archive constructor, not
	// through instantiate_deskbar_item(), so the tray mode has to be recovered
	// from who is hosting us.
	app_info info;
	if (be_app->GetAppInfo(&info) == B_OK
		&& !strcasecmp(info.signature, "application/x-vnd.Be-TSKB"))
		fInDeskbar = true;

	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(ViewColor());
}


BluetoothStatusView::~BluetoothStatusView()
{
	delete fTrayIcon;
}


BluetoothStatusView*
BluetoothStatusView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "BluetoothStatusView"))
		return NULL;

	return new BluetoothStatusView(archive);
}


status_t
BluetoothStatusView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kSignature);
	if (status == B_OK)
		status = archive->AddString("class", "BluetoothStatusView");

	return status;
}


void
BluetoothStatusView::AttachedToWindow()
{
	BView::AttachedToWindow();

	// Event-driven: LocalDevice::StartWatching() now delivers real BlueZ
	// signals, so there is no periodic poll to fall back to.
	LocalDevice::StartWatching(BMessenger(this),
		LocalDevice::NOTIFICATION_ADAPTER_ADDED |
		LocalDevice::NOTIFICATION_ADAPTER_REMOVED |
		LocalDevice::NOTIFICATION_DEVICE_CONNECTED |
		LocalDevice::NOTIFICATION_DEVICE_DISCONNECTED);

	// Register as the org.bluez.Agent1 pairing UI owner (the replicant
	// owns the agent, not the preflet, which usually isn't running). Safe
	// and idempotent to call again on a later re-attach.
	LocalDevice::RegisterAgent(BMessenger(this), BMessenger(this),
		kMsgOperationDone);

	// Initial update
	_RequestStatusUpdate();
}


void
BluetoothStatusView::DetachedFromWindow()
{
	// Stop watching Bluetooth changes
	LocalDevice::StopWatching(BMessenger(this));

	// Unregister the pairing agent so no stale registration is left on the
	// bus once this replicant is removed from Deskbar (or the standalone
	// window closes). Fire-and-forget: the view may already be on its way
	// out, so there is nothing useful to reply to.
	LocalDevice::UnregisterAgent(BMessenger(), 0);

	BView::DetachedFromWindow();
}


void
BluetoothStatusView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	Invalidate();
}


void
BluetoothStatusView::MessageReceived(BMessage* message)
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
					BMessage info;
					info.AddString("path", devicePath);
					RemoteDevice(info).Connect(BMessenger(this),
						kMsgOperationDone);
				}
			}
			break;

		case kMsgDisconnectDevice:
			{
				const char* devicePath;
				if (message->FindString("device_path", &devicePath) == B_OK) {
					BMessage info;
					info.AddString("path", devicePath);
					RemoteDevice(info).Disconnect(BMessenger(this),
						kMsgOperationDone);
				}
			}
			break;

		case kMsgScanDevices:
			{
				_ScanDevices();
			}
			break;

		case kMsgScanReady:
			// TODO: Show discovered devices in popup menu (see _ScanDevices).
			break;

		case kMsgOperationDone:
			// Connect/disconnect completed -- refresh status so the tray
			// glyph and cached menu snapshot reflect the new state.
			_RequestStatusUpdate();
			break;

		case kMsgEnableAdapter:
			_SetAdapterPowered(true);
			break;

		case kMsgDisableAdapter:
			_SetAdapterPowered(false);
			break;

		case kMsgOpenBluetoothPreferences:
			_OpenBluetoothPreferences();
			break;

		case kMsgPairingResult:
			_HandlePairingResult(message);
			break;

		case kMsgAgentRequest:
		{
			// The Agent1 method handler shapes the request like
			// PairingDialogWindow expects (kind/passkey/pin_code/
			// service_name/request_id) but deliberately carries only
			// "device_path" -- resolving a friendly name is this view's
			// job (from its own status-poll cache), not something the
			// dispatch thread can do synchronously. See BlueZBackend.cpp's
			// _HandleAgentMethodCall for why.
			int32 kind;
			if (message->FindInt32("kind", &kind) == B_OK) {
				BMessage request(*message);
				const char* devicePath;
				if (request.FindString("device_path", &devicePath) == B_OK)
					request.AddString("device_name",
						_CachedDeviceName(devicePath));
				_OpenPairingDialog((pairing_dialog_kind)kind, request);
			}
			break;
		}

		case kMsgAgentCancel:
			// Agent1.Cancel: dismiss whichever dialog is open. Idempotent --
			// SendMessage on an invalid/dead messenger is a silent no-op.
			if (fPairingDialogTarget.IsValid())
				fPairingDialogTarget.SendMessage(B_QUIT_REQUESTED);
			break;

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
		case kMsgDebugOpenPairingDialog:
		{
			int32 kind;
			if (message->FindInt32("kind", &kind) == B_OK) {
				BMessage request;
				request.AddString("device_name", "Sample Headphones");
				request.AddUInt32("passkey", 123456);
				request.AddString("pin_code", "1234");
				request.AddString("service_name", "OBEX File Transfer");
				_OpenPairingDialog((pairing_dialog_kind)kind, request);
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
BluetoothStatusView::_AboutRequested()
{
	// Heap-allocated on purpose: a BWindow runs on its own thread and deletes
	// itself when it quits. A stack instance would be destroyed here, on the
	// host looper's thread, without holding its lock.
	BAboutWindow* window = new BAboutWindow(
		B_TRANSLATE_SYSTEM_NAME("BluetoothStatus"), kSignature);

	window->AddCopyright(2026, "Dario Casalinuovo");
	window->AddDescription(B_TRANSLATE("Bluetooth status indicator"));

	window->Show();
}


int32
BluetoothStatusView::_RemoveFromDeskbarThread(void*)
{
	BDeskbar deskbar;
	deskbar.RemoveItem(kDeskbarItemName);
	return 0;
}


void
BluetoothStatusView::_Quit()
{
	// In the tray "Quit" means remove the replicant -- posting
	// B_QUIT_REQUESTED on to our looper would ask Deskbar itself to quit.
	if (fInDeskbar) {
		// BDeskbar::RemoveItem's untimed SendMessage targets Deskbar's own
		// team, which this replicant runs inside; calling it inline would
		// block this window thread mid-dispatch with nothing left to drain
		// the port -- reproduced as a real deadlock. Hand it to a detached
		// thread so this handler returns immediately.
		thread_id thread = spawn_thread(_RemoveFromDeskbarThread,
			"remove_bluetoothstatus_item", B_NORMAL_PRIORITY, NULL);
		if (thread >= B_OK)
			resume_thread(thread);
	} else
		be_app->PostMessage(B_QUIT_REQUESTED);
}


void
BluetoothStatusView::MouseDown(BPoint where)
{
	_ShowMenu(where);
}


void
BluetoothStatusView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();

	// B_TRANSPARENT_BACKGROUND: the tray paints behind us, so draw the glyph
	// only. Touching the view color here would re-trigger a redraw.
	drawing_mode oldMode = DrawingMode();
	SetDrawingMode(B_OP_ALPHA);

	_DrawBluetoothIcon(bounds.InsetByCopy(2, 2));

	SetDrawingMode(oldMode);
}


// One HVIF glyph (resource(1, "tray_icon") in BluetoothStatusIcons.rdef)
// plus programmatic alpha/slash/dot -- no authored per-state art, matching
// the network glyph's state-icon-vs-overlay split for consistency.
BBitmap*
BluetoothStatusView::_GetTrayIcon()
{
	if (fTrayIcon != NULL)
		return fTrayIcon;

	// Resolves against this add-on's own resources, not the host
	// application's -- as a Deskbar replicant, be_app is Deskbar, which has
	// no "tray_icon" resource of its own.
	BResources resources;
	if (resources.SetToImage((void*)&instantiate_deskbar_item) != B_OK)
		return NULL;

	size_t size = 0;
	const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE, 1, &size);
	if (data == NULL)
		return NULL;

	BBitmap* icon = new(std::nothrow) BBitmap(BRect(0, 0, kIconWidth - 1,
		kIconHeight - 1), B_RGBA32);
	if (icon == NULL || icon->InitCheck() != B_OK
		|| BIconUtils::GetVectorIcon((const uint8*)data, size, icon) != B_OK) {
		delete icon;
		return NULL;
	}

	fTrayIcon = icon;
	return fTrayIcon;
}


void
BluetoothStatusView::_DrawIconWithAlpha(BBitmap* icon, BRect bounds,
	uint8 alpha)
{
	// B_OP_ALPHA + B_PIXEL_ALPHA honors an icon's own per-pixel alpha but
	// not a caller-supplied constant one, so a uniform fade (the "off"
	// state) needs the bitmap's alpha channel scaled directly.
	if (alpha == 255) {
		DrawBitmap(icon, bounds);
		return;
	}

	BBitmap faded(icon->Bounds(), B_RGBA32);
	if (faded.InitCheck() != B_OK) {
		DrawBitmap(icon, bounds);
		return;
	}
	faded.ImportBits(icon);

	uint8* bits = (uint8*)faded.Bits();
	int32 pixels = faded.BitsLength() / 4;
	for (int32 i = 0; i < pixels; i++)
		bits[i * 4 + 3] = (uint8)(bits[i * 4 + 3] * alpha / 255);

	DrawBitmap(&faded, bounds);
}


void
BluetoothStatusView::_DrawBluetoothIcon(BRect bounds)
{
	BBitmap* icon = _GetTrayIcon();
	if (icon == NULL)
		return;

	uint8 alpha = 255;
	if (fDiscovering) {
		// Coarse pulse driven by the existing 3s status-poll runner rather
		// than a dedicated one -- adequate for a "discovering" hint and
		// avoids a second BMessageRunner per replicant instance.
		fPulsePhase = !fPulsePhase;
		alpha = fPulsePhase ? 255 : 140;
	} else if (fAdapterCount > 0 && !fPowered) {
		alpha = 102; // ~40%, "adapter present but off" state
	}

	_DrawIconWithAlpha(icon, bounds, alpha);

	if (fAdapterCount == 0) {
		// No adapter: diagonal slash across the glyph.
		SetHighColor(ui_color(B_FAILURE_COLOR));
		SetPenSize(1.5f);
		StrokeLine(bounds.LeftTop(), bounds.RightBottom());
		SetPenSize(1.0f);
	} else if (fConnected) {
		// Connected: small accent dot, bottom-right.
		BRect dot(bounds.right - 5, bounds.bottom - 5, bounds.right,
			bounds.bottom);
		SetHighColor(ui_color(B_SUCCESS_COLOR));
		FillEllipse(dot);
	}
}


void
BluetoothStatusView::_RequestStatusUpdate()
{
	// Fires the request and returns immediately -- the reply lands in
	// MessageReceived() as kMsgStatusReady. Never call the synchronous
	// GetAdapters()/GetDevices() from here: this runs on the replicant's
	// host window thread, which in the tray is Deskbar's -- confirmed by a
	// gdb backtrace of Deskbar's tray window thread parked in
	// BlueZBackend::_DoGetManagedObjects via this exact call site.
	LocalDevice::FetchStatusAsync(BMessenger(this), kMsgStatusReady);
}


void
BluetoothStatusView::_ApplyStatusUpdate(BMessage* status)
{
	BMessage adapters;
	LocalDevicesList adapterList(5);
	if (status->FindMessage("adapters", &adapters) == B_OK)
		LocalDevice::DevicesFromMessage(adapters, adapterList);

	fAdapterCount = adapterList.CountItems();

	// Check first adapter status (simplified)
	if (fAdapterCount > 0) {
		LocalDevice* adapter = adapterList.ItemAt(0);
		fAdapterPath = adapter->Path();
		fPowered = adapter->IsPowered();
		fDiscovering = adapter->IsDiscovering();

		BMessage devices;
		RemoteDevicesList deviceList(20);
		if (status->FindMessage("devices", &devices) == B_OK)
			RemoteDevice::DevicesFromMessage(devices, deviceList);

		// Check if any device is connected
		fConnected = (deviceList.CountItems() > 0);

		// Refresh the name cache an incoming Agent1 request resolves
		// against -- see _CachedDeviceName().
		fDeviceNames.clear();
		int32 deviceCount = 0;
		devices.FindInt32("device_count", &deviceCount);
		for (int32 i = 0; i < deviceCount; i++) {
			BString itemName;
			itemName << "device_" << i;
			BMessage info;
			if (devices.FindMessage(itemName.String(), &info) != B_OK)
				continue;
			BString path, name;
			if (info.FindString("path", &path) != B_OK)
				continue;
			if (info.FindString("alias", &name) != B_OK || name.IsEmpty())
				info.FindString("name", &name);
			if (!name.IsEmpty())
				fDeviceNames[path] = name;
		}
	} else {
		fAdapterPath = "";
		fPowered = false;
		fConnected = false;
		fDiscovering = false;
	}

	Invalidate();
}


void
BluetoothStatusView::_SetAdapterPowered(bool powered)
{
	if (fAdapterPath.IsEmpty())
		return;

	BMessage info;
	info.AddString("path", fAdapterPath);
	LocalDevice(info).SetPowered(powered, BMessenger(this), kMsgOperationDone);
}


void
BluetoothStatusView::_OpenBluetoothPreferences()
{
	be_roster->Launch("application/x-vnd.Haiku-Bluetooth");
}


void
BluetoothStatusView::_OpenPairingDialog(pairing_dialog_kind kind,
	const BMessage& request)
{
	// One live pairing dialog at a time: cancel whatever is open first.
	// The old window's own QuitRequested() sends its cancel result before it
	// closes, so this is a normal cancel from the entry point's perspective,
	// not a special case.
	if (fPairingDialogTarget.IsValid())
		fPairingDialogTarget.SendMessage(B_QUIT_REQUESTED);

	PairingDialogWindow* window = new PairingDialogWindow(kind, request,
		BMessenger(this), kMsgPairingResult);
	fPairingDialogTarget = BMessenger(window);
	window->Show();
}


BString
BluetoothStatusView::_CachedDeviceName(const char* devicePath)
{
	if (devicePath == NULL)
		return BString();

	std::map<BString, BString>::iterator it = fDeviceNames.find(devicePath);
	if (it != fDeviceNames.end())
		return it->second;

	// No cached alias/name (device not seen in a status poll yet, e.g. a
	// cold-boot autoconnect racing the first poll) -- fall back to the
	// D-Bus path rather than blocking to resolve one.
	return BString(devicePath);
}


void
BluetoothStatusView::_HandlePairingResult(BMessage* message)
{
	// The single entry point the real Agent1 callback replies wire into.
	fPairingDialogTarget = BMessenger();

	uint32 requestId = 0;
	if (message->FindUInt32("request_id", &requestId) == B_OK
			&& requestId != 0) {
		bool accepted = false;
		BString value;
		message->FindBool("accepted", &accepted);
		message->FindString("value", &value);
		LocalDevice::CompleteAgentRequest(requestId, accepted, value);
	}

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
	int32 kind = -1;
	bool accepted = false;
	BString value;
	message->FindInt32("kind", &kind);
	message->FindBool("accepted", &accepted);
	message->FindString("value", &value);
	fprintf(stderr, "BluetoothStatus: pairing dialog kind=%d accepted=%d "
		"value=\"%s\"\n", (int)kind, accepted, value.String());
#endif
}


#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
void
BluetoothStatusView::_BuildDebugDialogMenu(BMenu* parent)
{
	// Test trigger only: opens each pairing dialog with sample content so
	// all five can be reviewed side-by-side against GNOME/XFCE/Haiku. Guarded
	// by a CMake option that defaults off, not a runtime switch, so it cannot
	// ship enabled by accident.
	static const struct { const char* label; pairing_dialog_kind kind; } kItems[] = {
		{ "Request Confirmation" B_UTF8_ELLIPSIS, kPairingRequestConfirmation },
		{ "Request Passkey" B_UTF8_ELLIPSIS, kPairingRequestPasskey },
		{ "Display Passkey" B_UTF8_ELLIPSIS, kPairingDisplayPasskey },
		{ "Request PIN Code" B_UTF8_ELLIPSIS, kPairingRequestPinCode },
		{ "Display PIN Code" B_UTF8_ELLIPSIS, kPairingDisplayPinCode },
		{ "Request Authorization" B_UTF8_ELLIPSIS, kPairingRequestAuthorization },
		{ "Authorize Service" B_UTF8_ELLIPSIS, kPairingAuthorizeService },
	};

	BMenu* menu = new BMenu("Test pairing dialogs (debug)");
	for (size_t i = 0; i < sizeof(kItems) / sizeof(kItems[0]); i++) {
		BMessage* msg = new BMessage(kMsgDebugOpenPairingDialog);
		msg->AddInt32("kind", (int32)kItems[i].kind);
		menu->AddItem(new BMenuItem(kItems[i].label, msg));
	}
	menu->SetTargetForItems(this);
	parent->AddItem(menu);
}
#endif


void
BluetoothStatusView::_ScanDevices()
{
	RemoteDevice::FetchAllAsync(BMessenger(this), kMsgScanReady);
}


void
BluetoothStatusView::_ShowMenu(BPoint where)
{
	// One menu per click, matching the fix applied to NetworkStatusView (see
	// its MouseDown comment): two independently-constructed BPopUpMenus from
	// the same replicant risked Deskbar's own TExpandoMenuBar tracking
	// concurrently with ours, which looks like a desktop freeze even though
	// nothing is deadlocked. The previous code additionally had the branch
	// backwards -- it showed the functional Bluetooth menu only when NOT in
	// the tray, and the About/Quit-only menu when actually in the tray,
	// which is the one place a user would click it.
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
	menu->SetAsyncAutoDestruct(true);
	menu->SetFont(be_plain_font);
	BMenuItem* item;

	// Add adapter toggle
	item = new BMenuItem(fPowered ? "Disable Bluetooth" : "Enable Bluetooth",
		new BMessage(fPowered ? kMsgDisableAdapter : kMsgEnableAdapter));
	menu->AddItem(item);

	menu->AddSeparatorItem();

	// Add scan option
	item = new BMenuItem("Scan for devices", new BMessage(kMsgScanDevices));
	menu->AddItem(item);

	// Add connected device if any
	if (fConnected && !fConnectedDevice.IsEmpty()) {
		menu->AddSeparatorItem();
		BString label = "Connected to ";
		label += fConnectedDevice;
		item = new BMenuItem(label.String(), NULL);
		item->SetEnabled(false);
		menu->AddItem(item);
	}

	// Add preferences option
	menu->AddSeparatorItem();
	item = new BMenuItem("Bluetooth preferences" B_UTF8_ELLIPSIS,
		new BMessage(kMsgOpenBluetoothPreferences));
	menu->AddItem(item);

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
	menu->AddSeparatorItem();
	_BuildDebugDialogMenu(menu);
#endif

	menu->AddSeparatorItem();
	item = new BMenuItem("About" B_UTF8_ELLIPSIS,
		new BMessage(B_ABOUT_REQUESTED));
	menu->AddItem(item);

	if (fInDeskbar)
		menu->AddItem(new BMenuItem("Quit", new BMessage(B_QUIT_REQUESTED)));

	menu->SetTargetForItems(this);
	ConvertToScreen(&where);
	// Async + heap + auto-destruct, as upstream does. A synchronous Go()
	// blocks the calling thread until the menu closes -- in the tray that is
	// Deskbar's window thread, so the whole desktop freezes while it is up.
	menu->Go(where, true, true, true);
}


// BluetoothStatus replicant instantiation


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	// Square, driven by height: maxWidth is the whole tray width, and a
	// replicant wider than one row makes TReplicantTray::LocationForReplicant()
	// loop forever looking for a row it fits in.
	return new BluetoothStatusView(BRect(0, 0, maxHeight - 1, maxHeight - 1),
		B_FOLLOW_LEFT | B_FOLLOW_TOP, true);
}


extern "C" _EXPORT status_t
instantiate_deskbar_item_for_width(float* width, float* height)
{
	*width = kIconWidth;
	*height = kIconHeight;
	return B_OK;
}