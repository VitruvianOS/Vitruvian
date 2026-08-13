/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "InterfaceDetailView.h"

#include "NMBackend.h"
#include "StaticIPView.h"

#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <GridLayout.h>
#include <GridView.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <NetworkInterface.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>

#include <algorithm>
#include <stdio.h>
#include <vector>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InterfaceDetailView"


static const char* kNotYetAvailable = "Not yet available";

static const uint32 kMsgWiFiSelectionChanged = 'iWsc';
static const uint32 kMsgJoinWiFi = 'iJwf';
static const uint32 kMsgForgetWiFi = 'iFwf';
static const uint32 kMsgWiFiActionResult = 'iWar';
static const uint32 kMsgConnectVPN = 'iCvp';
static const uint32 kMsgDisconnectVPN = 'iDvp';

static const uint32 kMsgSavedSelectionChanged = 'iSsc';
static const uint32 kMsgSavedNetworksLoaded = 'iSnl';
static const uint32 kMsgForgetSaved = 'iFsv';
static const uint32 kMsgToggleSavedAutoconnect = 'iTsa';
static const uint32 kMsgMoveSavedUp = 'iSup';
static const uint32 kMsgMoveSavedDown = 'iSdn';
static const uint32 kMsgSavedActionResult = 'iSar';
static const uint32 kMsgProfilesLoaded = 'iPrl';
static const uint32 kMsgSavedReordered = 'iSro';
static const uint32 kMsgSavedItemDragged = 'iSid';


static BString
_FormatBytes(uint64 bytes)
{
	const char* units[] = {"B", "KB", "MB", "GB", "TB"};
	double value = (double)bytes;
	int unit = 0;
	while (value >= 1024.0 && unit < 4) {
		value /= 1024.0;
		unit++;
	}
	char buffer[64];
	snprintf(buffer, sizeof(buffer), "%.1f %s", value, units[unit]);
	return BString(buffer);
}


static BString
_StateString(uint32 state)
{
	switch (state) {
		case 30: return B_TRANSLATE("Disconnected");
		case 40: case 50: case 60: case 70: case 80: case 90:
			return B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS);
		case 100: return B_TRANSLATE("Connected");
		case 110: return B_TRANSLATE("Deactivating" B_UTF8_ELLIPSIS);
		case 120: return B_TRANSLATE("Failed");
		case 20: return B_TRANSLATE("Unavailable");
		case 10: return B_TRANSLATE("Unmanaged");
		default: return B_TRANSLATE("Unknown");
	}
}


// Single row in the "Available networks" list. Text-only status line rather
// than a signal-bar glyph -- no such art exists in this tree yet (see
// DeviceListItem in NetworkWindowNM.cpp for the same programmatic-over-
// authored-art approach).
class WiFiNetworkItem : public BStringItem {
public:
	WiFiNetworkItem(const char* ssid, int32 strength, bool secured,
		bool connected, bool saved)
		:
		BStringItem(ssid),
		fSSID(ssid),
		fStrength(strength),
		fSecured(secured),
		fConnected(connected),
		fSaved(saved),
		fFirstLineOffset(0),
		fLineOffset(0)
	{
	}

	const BString&	SSID() const { return fSSID; }
	bool			Secured() const { return fSecured; }
	bool			IsConnectedNetwork() const { return fConnected; }

	// Set once the async saved-networks reply lands, which is usually after
	// this list has already been built from the scan results.
	void SetSaved(bool saved) { fSaved = saved; }
	bool IsSaved() const { return fSaved; }

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
		owner->DrawString(fSSID, namePoint);
		owner->SetFont(be_plain_font);

		BString status;
		if (fConnected) {
			status = B_TRANSLATE("Connected");
		} else {
			status << fStrength << "%";
			status << (fSecured ? B_TRANSLATE(" - Secured")
				: B_TRANSLATE(" - Open"));
		}
		// Cross-reference into the "Saved networks" list below: tells the
		// user this in-range AP already has a stored profile, without
		// merging the two genuinely different lists (in-range vs stored).
		if (fSaved && !fConnected)
			status << "  \xE2\x80\x94  " << B_TRANSLATE("Saved");
		owner->DrawString(status.String(), statusPoint);

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
	BString	fSSID;
	int32	fStrength;
	bool	fSecured;
	bool	fConnected;
	bool	fSaved;
	float	fFirstLineOffset;
	float	fLineOffset;
};


// Single row in the "Saved networks" list -- NM connection profiles, shown
// regardless of whether their AP is currently in range.
class SavedNetworkItem : public BStringItem {
public:
	SavedNetworkItem(const char* ssid, const char* path, bool autoconnect,
		int32 priority)
		:
		BStringItem(""),
		fSSID(ssid),
		fPath(path),
		fAutoconnect(autoconnect),
		fPriority(priority)
	{
		_UpdateText();
	}

	const BString&	SSID() const { return fSSID; }
	const BString&	Path() const { return fPath; }
	bool			Autoconnect() const { return fAutoconnect; }
	int32			Priority() const { return fPriority; }

	void SetAutoconnect(bool autoconnect)
	{
		fAutoconnect = autoconnect;
		_UpdateText();
	}

	void SetPriority(int32 priority)
	{
		fPriority = priority;
		_UpdateText();
	}

private:
	void _UpdateText()
	{
		BString text(fSSID);
		text << "  \xE2\x80\x94  ";	// em dash
		text << (fAutoconnect ? B_TRANSLATE("Autoconnect: On")
			: B_TRANSLATE("Autoconnect: Off"));
		text << "  \xE2\x80\x94  " << B_TRANSLATE("Priority:") << " "
			<< fPriority;
		SetText(text.String());
	}

	BString	fSSID;
	BString	fPath;
	bool	fAutoconnect;
	int32	fPriority;
};


// Drag-and-drop reordering of the "Saved networks" list, following Haiku's
// own idiom for reorderable BListViews (see e.g. mail_daemon's
// FilterConfigView DragListView): InitiateDrag() snapshots the row into a
// drag bitmap and posts a B_SIMPLE_DATA-style self-message carrying the
// source index; MessageReceived() resolves the drop point back to a target
// index and does the actual BListView::MoveItem(). Move Up/Down buttons stay
// as the accessible path alongside this.
class SavedNetworkListView : public BListView {
public:
	SavedNetworkListView(const char* name, BMessage* itemMovedMessage)
		:
		BListView(name, B_SINGLE_SELECTION_LIST),
		fDragging(false),
		fDragIndex(-1),
		fLastDragTarget(-1),
		fItemMovedMessage(itemMovedMessage)
	{
	}

	virtual ~SavedNetworkListView()
	{
		delete fItemMovedMessage;
	}

	virtual bool InitiateDrag(BPoint point, int32 index, bool wasSelected)
	{
		if (index < 0)
			return false;

		BRect frame(ItemFrame(index));
		BBitmap* bitmap = new BBitmap(frame.OffsetToCopy(B_ORIGIN), B_RGBA32,
			true);
		BView* view = new BView(bitmap->Bounds(), NULL, 0, 0);
		bitmap->AddChild(view);

		if (view->LockLooper()) {
			BListItem* item = ItemAt(index);
			bool selected = item->IsSelected();

			view->SetLowColor(225, 225, 225, 128);
			view->FillRect(view->Bounds());

			if (selected)
				item->Deselect();
			item->DrawItem(view, view->Bounds(), true);
			if (selected)
				item->Select();

			view->UnlockLooper();
		}

		fLastDragTarget = -1;
		fDragIndex = index;
		fDragging = true;

		BMessage drag(kMsgSavedItemDragged);
		drag.AddInt32("index", index);
		DragMessage(&drag, bitmap, B_OP_ALPHA, point - frame.LeftTop(), this);

		return true;
	}

	void DrawDragTargetIndicator(int32 target)
	{
		PushState();
		SetDrawingMode(B_OP_INVERT);

		bool last = false;
		if (target >= CountItems()) {
			target = CountItems() - 1;
			last = true;
		}

		BRect frame = ItemFrame(target);
		if (last)
			frame.OffsetBy(0, frame.Height());
		frame.bottom = frame.top + 1;

		FillRect(frame);

		PopState();
	}

	virtual void MouseMoved(BPoint point, uint32 transit,
		const BMessage* dragMessage)
	{
		BListView::MouseMoved(point, transit, dragMessage);

		if ((transit != B_ENTERED_VIEW && transit != B_INSIDE_VIEW)
			|| !fDragging) {
			return;
		}

		int32 target = IndexOf(point);
		if (target == -1)
			target = CountItems();

		if (target == fDragIndex || target == fDragIndex + 1)
			target = -1;

		if (target == fLastDragTarget)
			return;

		if (fLastDragTarget != -1)
			DrawDragTargetIndicator(fLastDragTarget);

		fLastDragTarget = target;
		if (target != -1)
			DrawDragTargetIndicator(target);
	}

	virtual void MouseUp(BPoint point)
	{
		if (fDragging) {
			fDragging = false;
			if (fLastDragTarget != -1)
				DrawDragTargetIndicator(fLastDragTarget);
		}
		BListView::MouseUp(point);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgSavedItemDragged) {
			int32 source = message->FindInt32("index");
			BPoint point;
			if (message->FindPoint("_drop_point_", &point) == B_OK) {
				ConvertFromScreen(&point);
				int32 to = IndexOf(point);
				if (to > fDragIndex)
					to--;
				if (to == -1)
					to = CountItems() - 1;

				if (source != to && source >= 0 && to >= 0) {
					MoveItem(source, to);

					if (fItemMovedMessage != NULL) {
						BMessage moved(fItemMovedMessage->what);
						moved.AddInt32("from", source);
						moved.AddInt32("to", to);
						Messenger().SendMessage(&moved);
					}
				}
			}
			return;
		}
		BListView::MessageReceived(message);
	}

private:
	bool		fDragging;
	int32		fDragIndex;
	int32		fLastDragTarget;
	BMessage*	fItemMovedMessage;
};


InterfaceDetailView::InterfaceDetailView()
	:
	BView("interfaceDetail", B_WILL_DRAW),
	fGridLayout(NULL),
	fMode(MODE_EMPTY),
	fStaticIPView(NULL),
	fWiFiListView(NULL),
	fJoinButton(NULL),
	fForgetButton(NULL),
	fSavedListView(NULL),
	fSavedForgetButton(NULL),
	fSavedAutoconnectButton(NULL),
	fSavedMoveUpButton(NULL),
	fSavedMoveDownButton(NULL),
	fVPNConnectButton(NULL),
	fVPNDisconnectButton(NULL)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	fEmptyMessage = B_TRANSLATE("Select a device");
}


InterfaceDetailView::~InterfaceDetailView()
{
}


void
InterfaceDetailView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_Rebuild();
}


void
InterfaceDetailView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgWiFiSelectionChanged:
			_UpdateWiFiButtons();
			break;

		case kMsgJoinWiFi:
		{
			if (fWiFiListView == NULL)
				break;
			WiFiNetworkItem* item = dynamic_cast<WiFiNetworkItem*>(
				fWiFiListView->ItemAt(fWiFiListView->CurrentSelection()));
			if (item == NULL)
				break;

			BString devicePath;
			fDeviceInfo.FindString(kNMFieldPath, &devicePath);

			NMBackend* backend = NMBackend::Instance();
			if (backend != NULL) {
				// password is NULL: a secured network's credential prompt is
				// the NM SecretAgent's job, not this preflet's.
				backend->ConnectToWiFiAsync(devicePath.String(),
					item->SSID().String(), NULL, NULL, true,
					BMessenger(this), kMsgWiFiActionResult);
			}
			break;
		}

		case kMsgForgetWiFi:
		{
			if (fWiFiListView == NULL)
				break;
			WiFiNetworkItem* item = dynamic_cast<WiFiNetworkItem*>(
				fWiFiListView->ItemAt(fWiFiListView->CurrentSelection()));
			if (item == NULL)
				break;

			NMBackend* backend = NMBackend::Instance();
			if (backend != NULL) {
				// Prefer the precise by-path delete: SSID matching (the old
				// ForgetWiFiNetwork()) deletes every saved profile with that
				// SSID, which is the wrong thing when two profiles share one
				// -- fSavedNetworks (already loaded for the Saved networks
				// list below) is where the path lives. Only fall back to the
				// SSID-wide delete when there is no saved profile to match
				// (an in-range network with no saved profile at all has
				// nothing for ForgetSavedNetworkAsync() to target).
				BString path;
				int32 matches = 0;
				int32 count = 0;
				fSavedNetworks.FindInt32(kNMFieldSavedCount, &count);
				for (int32 i = 0; i < count; i++) {
					char name[32];
					snprintf(name, sizeof(name), "saved_%d", (int)i);
					BMessage entry;
					if (fSavedNetworks.FindMessage(name, &entry) != B_OK)
						continue;
					BString ssid, entryPath;
					entry.FindString(kNMFieldSavedSSID, &ssid);
					entry.FindString(kNMFieldSavedPath, &entryPath);
					if (ssid == item->SSID()) {
						matches++;
						path = entryPath;
					}
				}

				if (matches == 1 && !path.IsEmpty()) {
					backend->ForgetSavedNetworkAsync(path.String(),
						BMessenger(this), kMsgSavedActionResult);
				} else if (matches == 0) {
					// No saved profile at all -- nothing to forget.
				} else {
					// Ambiguous (several profiles share this SSID): the old
					// delete-every-match behavior is still correct here,
					// since a specific one can't be inferred from this list.
					backend->ForgetWiFiNetwork(item->SSID().String());
				}
			}
			_Rebuild();
			break;
		}

		case kMsgWiFiActionResult:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status != B_OK) {
				BString reason;
				message->FindString("reason", &reason);
				BString text(B_TRANSLATE("Could not join the Wi-Fi network."));
				if (!reason.IsEmpty())
					text << "\n" << reason;
				BAlert* alert = new BAlert(B_TRANSLATE("Not connected"),
					text.String(), B_TRANSLATE("OK"));
				alert->Go(NULL);
			}
			break;
		}

		case kMsgSavedNetworksLoaded:
		{
			fSavedNetworks = *message;
			_RebuildSavedList();
			_UpdateWiFiSavedMarkers();
			break;
		}

		case kMsgProfilesLoaded:
		{
			// A reply for a device that is no longer the one shown (the user
			// clicked to a different device before this arrived) would feed
			// StaticIPView the wrong device's profile chooser -- match the
			// device path before applying it.
			BString path;
			BString shownPath;
			fDeviceInfo.FindString(kNMFieldPath, &shownPath);
			if (fStaticIPView != NULL
				&& message->FindString(kNMFieldPath, &path) == B_OK
				&& path == shownPath) {
				fStaticIPView->SetProfiles(*message);
			}
			break;
		}

		case kMsgSavedSelectionChanged:
			_UpdateSavedButtons();
			break;

		case kMsgForgetSaved:
		{
			if (fSavedListView == NULL)
				break;
			SavedNetworkItem* item = dynamic_cast<SavedNetworkItem*>(
				fSavedListView->ItemAt(fSavedListView->CurrentSelection()));
			if (item == NULL)
				break;

			NMBackend* backend = NMBackend::Instance();
			if (backend != NULL) {
				backend->ForgetSavedNetworkAsync(item->Path().String(),
					BMessenger(this), kMsgSavedActionResult);
			}
			break;
		}

		case kMsgToggleSavedAutoconnect:
		{
			if (fSavedListView == NULL)
				break;
			SavedNetworkItem* item = dynamic_cast<SavedNetworkItem*>(
				fSavedListView->ItemAt(fSavedListView->CurrentSelection()));
			if (item == NULL)
				break;

			NMBackend* backend = NMBackend::Instance();
			if (backend != NULL) {
				backend->SetWiFiAutoconnectAsync(item->Path().String(),
					!item->Autoconnect(), BMessenger(this),
					kMsgSavedActionResult);
			}
			break;
		}

		case kMsgMoveSavedUp:
		case kMsgMoveSavedDown:
		{
			if (fSavedListView == NULL)
				break;
			int32 index = fSavedListView->CurrentSelection();
			int32 otherIndex = index
				+ (message->what == kMsgMoveSavedUp ? -1 : 1);
			if (index < 0 || otherIndex < 0
				|| otherIndex >= fSavedListView->CountItems()) {
				break;
			}

			fSavedListView->MoveItem(index, otherIndex);
			fSavedListView->Select(otherIndex);
			_RenumberSavedList();
			break;
		}

		case kMsgSavedReordered:
			// The list view already performed the visual MoveItem() before
			// sending this; just persist the resulting order.
			_RenumberSavedList();
			break;

		case kMsgSavedActionResult:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status != B_OK) {
				BString reason;
				message->FindString("reason", &reason);
				BString text(
					B_TRANSLATE("Could not update the saved network."));
				if (!reason.IsEmpty())
					text << "\n" << reason;
				BAlert* alert = new BAlert(B_TRANSLATE("Not updated"),
					text.String(), B_TRANSLATE("OK"));
				alert->Go(NULL);
			}
			_RequestSavedNetworks();
			break;
		}

		case kMsgConnectVPN:
		{
			BString path;
			if (fDeviceInfo.FindString(kNMFieldVPNPath, &path) == B_OK) {
				NMBackend* backend = NMBackend::Instance();
				if (backend != NULL)
					backend->ConnectVPN(path.String());
			}
			break;
		}

		case kMsgDisconnectVPN:
		{
			BString path;
			if (fDeviceInfo.FindString(kNMFieldVPNPath, &path) == B_OK) {
				NMBackend* backend = NMBackend::Instance();
				if (backend != NULL)
					backend->DisconnectVPN(path.String());
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
InterfaceDetailView::SetToDevice(const BMessage& deviceInfo)
{
	fDeviceInfo = deviceInfo;
	fMode = MODE_DEVICE;
	_Rebuild();
}


void
InterfaceDetailView::SetToVPN(const BMessage& vpnInfo)
{
	fDeviceInfo = vpnInfo;
	fMode = MODE_VPN;
	_Rebuild();
}


void
InterfaceDetailView::ShowEmpty(const char* message)
{
	fMode = MODE_EMPTY;
	fEmptyMessage = message;
	_Rebuild();
}


void
InterfaceDetailView::_Rebuild()
{
	// Tear down and rebuild rather than mutate in place: the field set
	// differs by device type (wired vs WiFi vs VPN vs empty), so a partial-
	// update path would need to track more state than a redraw costs here.
	// BListView does not own its items, so they have to be drained before the
	// list view itself goes away with the rest of the children.
	if (fWiFiListView != NULL) {
		BListItem* stale;
		while ((stale = fWiFiListView->RemoveItem((int32)0)) != NULL)
			delete stale;
	}
	if (fSavedListView != NULL) {
		BListItem* stale;
		while ((stale = fSavedListView->RemoveItem((int32)0)) != NULL)
			delete stale;
	}

	for (int32 i = ChildAt(0) ? CountChildren() : 0; i-- > 0;) {
		BView* child = ChildAt(i);
		RemoveChild(child);
		delete child;
	}
	fStaticIPView = NULL;
	fWiFiListView = NULL;
	fJoinButton = NULL;
	fForgetButton = NULL;
	fSavedListView = NULL;
	fSavedForgetButton = NULL;
	fSavedAutoconnectButton = NULL;
	fSavedMoveUpButton = NULL;
	fSavedMoveDownButton = NULL;
	fVPNConnectButton = NULL;
	fVPNDisconnectButton = NULL;

	// Replace the layout wholesale rather than reusing it: deleting the child
	// views leaves the old layout holding items that are not views (glue,
	// nested groups) plus items whose views are now gone, and the next layout
	// pass walks them. SetLayout() deletes the previous layout for us.
	SetLayout(new BGroupLayout(B_VERTICAL));

	if (fMode == MODE_EMPTY) {
		BStringView* empty = new BStringView(NULL, fEmptyMessage.String());
		empty->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
		empty->SetAlignment(B_ALIGN_CENTER);
		BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
			.AddGlue()
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(empty)
				.AddGlue()
			.End()
			.AddGlue();
		return;
	}

	if (fMode == MODE_VPN) {
		_RebuildVPNView();
		return;
	}

	_RebuildDeviceView();
}


void
InterfaceDetailView::_RebuildDeviceView()
{
	BString interfaceName, hwAddress, type, driver, devicePath;
	uint32 state = 0;
	uint32 mtu = 0;
	fDeviceInfo.FindString(kNMFieldInterface, &interfaceName);
	fDeviceInfo.FindString(kNMFieldHWAddress, &hwAddress);
	fDeviceInfo.FindString(kNMFieldType, &type);
	fDeviceInfo.FindString(kNMFieldDriver, &driver);
	fDeviceInfo.FindString(kNMFieldPath, &devicePath);
	fDeviceInfo.FindUInt32(kNMFieldState, &state);
	fDeviceInfo.FindUInt32(kNMFieldMTU, &mtu);

	bool isWiFi = type == "wifi";

	// Pull the AP snapshot up front so the SSID/Signal/Security rows below
	// can show the connected network's real data instead of a placeholder.
	BMessage networks;
	int32 apCount = 0;
	BString connectedSSID;
	int32 connectedStrength = 0;
	bool connectedSecured = false;
	bool haveConnected = false;
	if (isWiFi && devicePath.Length() > 0) {
		NMBackend* backend = NMBackend::Instance();
		if (backend != NULL)
			backend->ScanWiFiNetworks(devicePath.String(), &networks);
		networks.FindInt32(kNMFieldAPCount, &apCount);

		for (int32 i = 0; i < apCount; i++) {
			char apName[32];
			snprintf(apName, sizeof(apName), "ap_%d", (int)i);
			BMessage apInfo;
			if (networks.FindMessage(apName, &apInfo) != B_OK)
				continue;
			bool connected = false;
			apInfo.FindBool(kNMFieldAPConnected, &connected);
			if (!connected)
				continue;
			apInfo.FindString(kNMFieldAPSSID, &connectedSSID);
			apInfo.FindInt32(kNMFieldAPStrength, &connectedStrength);
			apInfo.FindBool(kNMFieldAPSecured, &connectedSecured);
			haveConnected = true;
			break;
		}
	}

	BGridView* grid = new BGridView(B_USE_HALF_ITEM_SPACING,
		B_USE_HALF_ITEM_SPACING);
	fGridLayout = grid->GridLayout();

	int32 row = 0;
	auto addRow = [&](const char* label, const BString& value) {
		fGridLayout->AddView(new BStringView(NULL, label), 0, row);
		BStringView* valueView = new BStringView(NULL, value.String());
		valueView->SetFont(be_bold_font);
		fGridLayout->AddView(valueView, 1, row);
		row++;
	};

	addRow(B_TRANSLATE("Status:"), _StateString(state));

	// Live: BNetworkInterface reads straight from the kernel's own
	// interface table (not NetworkManager), so this works even when NM's
	// GetDeviceInfo() is stale or the interface is unmanaged.
	BNetworkInterface iface(interfaceName.String());
	if (iface.Exists()) {
		BNetworkInterfaceAddress addr;
		bool haveIPv4 = false;
		for (int32 i = 0; i < iface.CountAddresses(); i++) {
			if (iface.GetAddressAt(i, addr) == B_OK
				&& addr.Address().Family() == AF_INET) {
				BString ip;
				ip << addr.Address().ToString();
				addRow(B_TRANSLATE("IP Address:"), ip);
				haveIPv4 = true;
				break;
			}
		}
		if (!haveIPv4)
			addRow(B_TRANSLATE("IP Address:"), B_TRANSLATE("None"));

		ifreq_stats stats;
		if (iface.GetStats(stats) == B_OK) {
			addRow(B_TRANSLATE("Sent:"), _FormatBytes(stats.send.bytes));
			addRow(B_TRANSLATE("Received:"), _FormatBytes(stats.receive.bytes));
		}
	} else {
		addRow(B_TRANSLATE("IP Address:"), kNotYetAvailable);
	}

	BString gateway, dns;
	fDeviceInfo.FindString(kNMFieldGateway, &gateway);
	fDeviceInfo.FindString(kNMFieldDNS, &dns);
	addRow(B_TRANSLATE("Gateway:"),
		gateway.Length() > 0 ? gateway : BString(kNotYetAvailable));
	addRow(B_TRANSLATE("DNS:"),
		dns.Length() > 0 ? dns : BString(kNotYetAvailable));

	addRow(B_TRANSLATE("MAC:"),
		hwAddress.Length() > 0 ? hwAddress : BString(kNotYetAvailable));

	if (mtu > 0) {
		BString mtuStr;
		mtuStr << mtu;
		addRow(B_TRANSLATE("MTU:"), mtuStr);
	}

	addRow(B_TRANSLATE("Driver:"),
		driver.Length() > 0 ? driver : BString(kNotYetAvailable));

	if (isWiFi) {
		if (haveConnected) {
			BString strengthStr;
			strengthStr << connectedStrength << "%";
			addRow(B_TRANSLATE("SSID:"), connectedSSID);
			addRow(B_TRANSLATE("Signal:"), strengthStr);
			addRow(B_TRANSLATE("Security:"), connectedSecured
				? B_TRANSLATE("Secured") : B_TRANSLATE("Open"));
		} else {
			addRow(B_TRANSLATE("SSID:"), B_TRANSLATE("Not connected"));
			addRow(B_TRANSLATE("Signal:"), kNotYetAvailable);
			addRow(B_TRANSLATE("Security:"), kNotYetAvailable);
		}
	}

	BString title(B_TRANSLATE("Interface: %name%"));
	title.ReplaceFirst("%name%", interfaceName);
	BStringView* titleView = new BStringView(NULL, title.String());
	titleView->SetFont(be_bold_font);

	// IPv4 configuration only makes sense for real network
	// interfaces, not the VPN section's connection entries.
	bool showStaticIP = type == "ethernet" || type == "wifi";
	if (showStaticIP) {
		fStaticIPView = new StaticIPView();
		fStaticIPView->SetToDevice(fDeviceInfo);

		NMBackend* backend = NMBackend::Instance();
		if (backend != NULL && devicePath.Length() > 0) {
			backend->GetDeviceConnectionProfilesAsync(devicePath.String(),
				BMessenger(this), kMsgProfilesLoaded);
		}
	}

	BLayoutBuilder::Group<> builder((BGroupLayout*)GetLayout());
	builder.SetInsets(B_USE_WINDOW_SPACING)
		.Add(titleView)
		.Add(grid);

	if (showStaticIP) {
		builder.Add(new BStringView(NULL,
			B_TRANSLATE("IPv4 configuration:")));
		builder.Add(fStaticIPView);
	}

	if (isWiFi) {
		builder.Add(new BStringView(NULL,
			B_TRANSLATE("Available networks:")));

		fWiFiListView = new BListView("wifiList", B_SINGLE_SELECTION_LIST);
		fWiFiListView->SetSelectionMessage(new BMessage(
			kMsgWiFiSelectionChanged));
		fWiFiListView->SetTarget(this);
		fWiFiListView->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120));

		for (int32 i = 0; i < apCount; i++) {
			char apName[32];
			snprintf(apName, sizeof(apName), "ap_%d", (int)i);
			BMessage apInfo;
			if (networks.FindMessage(apName, &apInfo) != B_OK)
				continue;

			BString ssid;
			if (apInfo.FindString(kNMFieldAPSSID, &ssid) != B_OK)
				continue;
			int32 strength = 0;
			bool secured = false;
			bool connected = false;
			apInfo.FindInt32(kNMFieldAPStrength, &strength);
			apInfo.FindBool(kNMFieldAPSecured, &secured);
			apInfo.FindBool(kNMFieldAPConnected, &connected);

			fWiFiListView->AddItem(new WiFiNetworkItem(ssid.String(),
				strength, secured, connected, _HasSavedProfile(ssid)));
		}

		BScrollView* scrollView = new BScrollView("wifiScroll",
			fWiFiListView, 0, false, true);

		fJoinButton = new BButton("join", B_TRANSLATE("Join"),
			new BMessage(kMsgJoinWiFi));
		fForgetButton = new BButton("forget", B_TRANSLATE("Forget"),
			new BMessage(kMsgForgetWiFi));
		fJoinButton->SetTarget(this);
		fForgetButton->SetTarget(this);

		builder.Add(scrollView)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(fJoinButton)
				.Add(fForgetButton)
				.AddGlue()
			.End();

		_UpdateWiFiButtons();

		builder.Add(new BStringView(NULL, B_TRANSLATE("Saved networks:")));

		fSavedListView = new SavedNetworkListView("savedList",
			new BMessage(kMsgSavedReordered));
		fSavedListView->SetSelectionMessage(
			new BMessage(kMsgSavedSelectionChanged));
		fSavedListView->SetTarget(this);
		fSavedListView->SetExplicitMinSize(BSize(B_SIZE_UNSET, 100));

		BScrollView* savedScroll = new BScrollView("savedScroll",
			fSavedListView, 0, false, true);

		fSavedForgetButton = new BButton("savedForget",
			B_TRANSLATE("Forget"), new BMessage(kMsgForgetSaved));
		fSavedAutoconnectButton = new BButton("savedAutoconnect",
			B_TRANSLATE("Toggle Autoconnect"),
			new BMessage(kMsgToggleSavedAutoconnect));
		fSavedMoveUpButton = new BButton("savedUp", B_TRANSLATE("Move Up"),
			new BMessage(kMsgMoveSavedUp));
		fSavedMoveDownButton = new BButton("savedDown",
			B_TRANSLATE("Move Down"), new BMessage(kMsgMoveSavedDown));
		fSavedForgetButton->SetTarget(this);
		fSavedAutoconnectButton->SetTarget(this);
		fSavedMoveUpButton->SetTarget(this);
		fSavedMoveDownButton->SetTarget(this);

		builder.Add(savedScroll)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(fSavedForgetButton)
				.Add(fSavedAutoconnectButton)
				.Add(fSavedMoveUpButton)
				.Add(fSavedMoveDownButton)
				.AddGlue()
			.End();

		_UpdateSavedButtons();
		_RequestSavedNetworks();
	}

	builder.AddGlue();
}


void
InterfaceDetailView::_RequestSavedNetworks()
{
	NMBackend* backend = NMBackend::Instance();
	if (backend != NULL) {
		backend->GetSavedWiFiNetworksAsync(BMessenger(this),
			kMsgSavedNetworksLoaded);
	}
}


bool
InterfaceDetailView::_HasSavedProfile(const BString& ssid) const
{
	int32 count = 0;
	fSavedNetworks.FindInt32(kNMFieldSavedCount, &count);
	for (int32 i = 0; i < count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "saved_%d", (int)i);
		BMessage entry;
		if (fSavedNetworks.FindMessage(name, &entry) != B_OK)
			continue;
		BString entrySSID;
		entry.FindString(kNMFieldSavedSSID, &entrySSID);
		if (entrySSID == ssid)
			return true;
	}
	return false;
}


void
InterfaceDetailView::_UpdateWiFiSavedMarkers()
{
	// Saved networks load asynchronously and usually land after the
	// available-networks list has already been built from the scan reply --
	// patch the markers in rather than waiting for the next full _Rebuild().
	if (fWiFiListView == NULL)
		return;

	for (int32 i = 0; i < fWiFiListView->CountItems(); i++) {
		WiFiNetworkItem* item = dynamic_cast<WiFiNetworkItem*>(
			fWiFiListView->ItemAt(i));
		if (item == NULL)
			continue;
		item->SetSaved(_HasSavedProfile(item->SSID()));
	}
	fWiFiListView->Invalidate();
}


void
InterfaceDetailView::_RenumberSavedList()
{
	// Renumber the whole list from its current visual order rather than
	// swapping two priorities: NM defaults every profile to 0, and swapping
	// two equal values is a silent no-op.
	if (fSavedListView == NULL)
		return;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;

	int32 count = fSavedListView->CountItems();
	for (int32 i = 0; i < count; i++) {
		SavedNetworkItem* entry = dynamic_cast<SavedNetworkItem*>(
			fSavedListView->ItemAt(i));
		if (entry == NULL)
			continue;
		backend->SetWiFiPriorityAsync(entry->Path().String(), count - 1 - i,
			BMessenger(this), kMsgSavedActionResult);
	}
}


void
InterfaceDetailView::_RebuildSavedList()
{
	if (fSavedListView == NULL)
		return;

	BListItem* stale;
	while ((stale = fSavedListView->RemoveItem((int32)0)) != NULL)
		delete stale;

	int32 count = 0;
	fSavedNetworks.FindInt32(kNMFieldSavedCount, &count);

	// Highest priority first -- matches NM's own connect-order semantics and
	// makes "Move Up" visually move a profile toward the front of the list.
	std::vector<BMessage> entries;
	for (int32 i = 0; i < count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "saved_%d", (int)i);
		BMessage entry;
		if (fSavedNetworks.FindMessage(name, &entry) == B_OK)
			entries.push_back(entry);
	}
	std::sort(entries.begin(), entries.end(),
		[](const BMessage& a, const BMessage& b) {
			int32 pa = 0, pb = 0;
			a.FindInt32(kNMFieldSavedPriority, &pa);
			b.FindInt32(kNMFieldSavedPriority, &pb);
			return pa > pb;
		});

	for (size_t i = 0; i < entries.size(); i++) {
		BString ssid, path;
		bool autoconnect = true;
		int32 priority = 0;
		entries[i].FindString(kNMFieldSavedSSID, &ssid);
		entries[i].FindString(kNMFieldSavedPath, &path);
		entries[i].FindBool(kNMFieldSavedAutoconnect, &autoconnect);
		entries[i].FindInt32(kNMFieldSavedPriority, &priority);
		if (ssid.IsEmpty() || path.IsEmpty())
			continue;

		fSavedListView->AddItem(new SavedNetworkItem(ssid.String(),
			path.String(), autoconnect, priority));
	}

	_UpdateSavedButtons();
}


void
InterfaceDetailView::_UpdateSavedButtons()
{
	if (fSavedListView == NULL)
		return;

	int32 index = fSavedListView->CurrentSelection();
	SavedNetworkItem* item = dynamic_cast<SavedNetworkItem*>(
		fSavedListView->ItemAt(index));

	if (fSavedForgetButton != NULL)
		fSavedForgetButton->SetEnabled(item != NULL);
	if (fSavedAutoconnectButton != NULL)
		fSavedAutoconnectButton->SetEnabled(item != NULL);
	if (fSavedMoveUpButton != NULL)
		fSavedMoveUpButton->SetEnabled(item != NULL && index > 0);
	if (fSavedMoveDownButton != NULL) {
		fSavedMoveDownButton->SetEnabled(item != NULL
			&& index < fSavedListView->CountItems() - 1);
	}
}


void
InterfaceDetailView::_RebuildVPNView()
{
	BString name, path;
	bool connected = false;
	fDeviceInfo.FindString(kNMFieldVPNName, &name);
	fDeviceInfo.FindString(kNMFieldVPNPath, &path);
	fDeviceInfo.FindBool(kNMFieldVPNConnected, &connected);

	BString title(B_TRANSLATE("VPN: %name%"));
	title.ReplaceFirst("%name%", name);
	BStringView* titleView = new BStringView(NULL, title.String());
	titleView->SetFont(be_bold_font);

	BGridView* grid = new BGridView(B_USE_HALF_ITEM_SPACING,
		B_USE_HALF_ITEM_SPACING);
	fGridLayout = grid->GridLayout();
	fGridLayout->AddView(new BStringView(NULL, B_TRANSLATE("Status:")), 0, 0);
	BStringView* statusValue = new BStringView(NULL, connected
		? B_TRANSLATE("Connected") : B_TRANSLATE("Disconnected"));
	statusValue->SetFont(be_bold_font);
	fGridLayout->AddView(statusValue, 1, 0);

	fVPNConnectButton = new BButton("vpnConnect", B_TRANSLATE("Connect"),
		new BMessage(kMsgConnectVPN));
	fVPNDisconnectButton = new BButton("vpnDisconnect",
		B_TRANSLATE("Disconnect"), new BMessage(kMsgDisconnectVPN));
	fVPNConnectButton->SetEnabled(!connected);
	fVPNDisconnectButton->SetEnabled(connected);
	fVPNConnectButton->SetTarget(this);
	fVPNDisconnectButton->SetTarget(this);

	BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(titleView)
		.Add(grid)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fVPNConnectButton)
			.Add(fVPNDisconnectButton)
			.AddGlue()
		.End()
		.AddGlue();
}


void
InterfaceDetailView::_UpdateWiFiButtons()
{
	if (fWiFiListView == NULL)
		return;

	WiFiNetworkItem* item = dynamic_cast<WiFiNetworkItem*>(
		fWiFiListView->ItemAt(fWiFiListView->CurrentSelection()));

	if (fJoinButton != NULL)
		fJoinButton->SetEnabled(item != NULL && !item->IsConnectedNetwork());
	if (fForgetButton != NULL)
		fForgetButton->SetEnabled(item != NULL);
}


bool
InterfaceDetailView::IsRevertable() const
{
	return fStaticIPView != NULL && fStaticIPView->IsDirty();
}


void
InterfaceDetailView::Revert()
{
	if (fStaticIPView != NULL)
		fStaticIPView->Revert();
}
