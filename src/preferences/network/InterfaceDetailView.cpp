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
#include <GroupView.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <NetworkInterface.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <algorithm>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
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
static const uint32 kMsgRemoveVPN = 'iRvp';
static const uint32 kMsgRemoveVPNResult = 'iRvr';

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

static const uint32 kMsgStartHotspot = 'iHst';
static const uint32 kMsgStopHotspot = 'iHsp';
static const uint32 kMsgHotspotStateReply = 'iHsr';
static const uint32 kMsgHotspotActionResult = 'iHar';
static const uint32 kMsgHotspotSetupCommit = 'iHsc';
static const uint32 kMsgHotspotFieldModified = 'iHfm';


// Modal collector for hotspot name and password. Start stays disabled
// until the name is non-empty and the password reaches the WPA2 minimum.
class HotspotSetupWindow : public BWindow {
public:
	HotspotSetupWindow(const BMessenger& target, const char* defaultName,
		const char* defaultPassword, bool willDisconnect)
		:
		BWindow(BRect(0, 0, 360, willDisconnect ? 280 : 220),
			B_TRANSLATE("Turn On Wi-Fi Hotspot"), B_MODAL_WINDOW_LOOK,
			B_MODAL_APP_WINDOW_FEEL,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fTarget(target)
	{
		fNameControl = new BTextControl(B_TRANSLATE("Network name:"),
			defaultName, new BMessage(kMsgHotspotFieldModified));
		fNameControl->TextView()->SetExplicitMinSize(BSize(200,
			B_SIZE_UNSET));

		fPasswordControl = new BTextControl(B_TRANSLATE("Password:"),
			defaultPassword, new BMessage(kMsgHotspotFieldModified));
		fPasswordControl->TextView()->SetExplicitMinSize(BSize(200,
			B_SIZE_UNSET));

		BStringView* hint = new BStringView(NULL,
			B_TRANSLATE("At least 8 characters."));
		hint->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_3_TINT));

		BButton* cancelButton = new BButton(B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));
		fStartButton = new BButton(B_TRANSLATE("Start"),
			new BMessage(kMsgHotspotSetupCommit));
		fStartButton->MakeDefault(true);

		BLayoutBuilder::Group<> builder(this, B_VERTICAL,
			B_USE_DEFAULT_SPACING);
		builder.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fNameControl)
			.Add(fPasswordControl)
			.Add(hint);

		if (willDisconnect) {
			BString warn(B_TRANSLATE("Starting the hotspot disconnects "
				"this Wi-Fi adapter from its current network."));
			builder.Add(new BStringView(NULL, warn.String()));
		}

		builder
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancelButton)
				.Add(fStartButton)
			.End();

		_UpdateStartButton();
		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgHotspotFieldModified) {
			_UpdateStartButton();
			return;
		}
		if (message->what == kMsgHotspotSetupCommit) {
			BString name(fNameControl->Text());
			BString password(fPasswordControl->Text());
			name.Trim();
			password.Trim();
			if (name.IsEmpty() || password.Length() < 8)
				return;
			BMessage commit(kMsgHotspotSetupCommit);
			commit.AddString("ssid", name);
			commit.AddString("password", password);
			fTarget.SendMessage(&commit);
			Quit();
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	void _UpdateStartButton()
	{
		BString name(fNameControl->Text());
		BString password(fPasswordControl->Text());
		name.Trim();
		password.Trim();
		fStartButton->SetEnabled(!name.IsEmpty() && password.Length() >= 8);
	}

	BMessenger		fTarget;
	BTextControl*	fNameControl;
	BTextControl*	fPasswordControl;
	BButton*		fStartButton;
};


// WPA2-PSK keys must be 8..63 characters; this default is well above the
// minimum and readable enough to retype onto another device.
static BString
_GenerateHotspotPassword()
{
	static const char kAlphabet[]
		= "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	BString password;
	unsigned char bytes[12];
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd >= 0) {
		ssize_t n = read(fd, bytes, sizeof(bytes));
		close(fd);
		if (n != (ssize_t)sizeof(bytes)) {
			srand((unsigned)time(NULL) ^ (unsigned)getpid());
			for (size_t i = 0; i < sizeof(bytes); i++)
				bytes[i] = (unsigned char)(rand() & 0xff);
		}
	} else {
		srand((unsigned)time(NULL) ^ (unsigned)getpid());
		for (size_t i = 0; i < sizeof(bytes); i++)
			bytes[i] = (unsigned char)(rand() & 0xff);
	}
	for (size_t i = 0; i < sizeof(bytes); i++)
		password << kAlphabet[bytes[i] % (sizeof(kAlphabet) - 1)];
	return password;
}


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
// DeviceListItem in NetworkWindow.cpp for the same programmatic-over-
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
	fTabView(NULL),
	fSelectedTab(0),
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
	fVPNDisconnectButton(NULL),
	fVPNRemoveButton(NULL),
	fHotspotStartButton(NULL),
	fHotspotStopButton(NULL),
	fHotspotPage(NULL)
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

		case kMsgRemoveVPN:
		{
			BString path;
			BString name;
			fDeviceInfo.FindString(kNMFieldVPNPath, &path);
			fDeviceInfo.FindString(kNMFieldVPNName, &name);
			if (path.IsEmpty())
				break;

			BString text;
			if (name.IsEmpty())
				text = B_TRANSLATE("Delete this VPN connection?");
			else
				text.SetToFormat(
					B_TRANSLATE("Delete the VPN connection \"%s\"?"), name.String());
			text << "\n" << B_TRANSLATE("This cannot be undone.");

			BAlert* alert = new BAlert(B_TRANSLATE("Remove VPN"),
				text.String(), B_TRANSLATE("Remove"),
				B_TRANSLATE("Cancel"), NULL, B_WIDTH_AS_USUAL,
				B_STOP_ALERT);
			alert->SetShortcut(B_ESCAPE, 0);
			if (alert->Go() == 0) {
				NMBackend* backend = NMBackend::Instance();
				if (backend != NULL) {
					backend->RemoveVPNAsync(path.String(),
						BMessenger(this), kMsgRemoveVPNResult);
				}
			}
			break;
		}

		case kMsgRemoveVPNResult:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status != B_OK) {
				BString reason;
				message->FindString("reason", &reason);
				BString text(B_TRANSLATE("Could not remove the VPN "
					"connection."));
				if (!reason.IsEmpty())
					text << "\n" << reason;
				BAlert* alert = new BAlert(B_TRANSLATE("Not removed"),
					text.String(), B_TRANSLATE("OK"));
				alert->Go(NULL);
			}
			break;
		}

		case kMsgStartHotspot:
		{
			bool willDisconnect = false;
			fHotspotState.FindBool(kNMFieldHotspotWillDisconnect,
				&willDisconnect);
			BString defaultName("V Hotspot");
			// Reuse the saved profile password so devices that already
			// joined keep working; mint one only when no profile exists.
			BString defaultPassword;
			if (fHotspotSettings.ProfileUUID().IsEmpty())
				defaultPassword = _GenerateHotspotPassword();
			else
				fHotspotState.FindString(kNMFieldHotspotPassword,
					&defaultPassword);
			HotspotSetupWindow* window = new HotspotSetupWindow(
				BMessenger(this), defaultName.String(),
				defaultPassword.String(), willDisconnect);
			window->Show();
			break;
		}

		case kMsgHotspotSetupCommit:
		{
			BString ssid, password;
			message->FindString("ssid", &ssid);
			message->FindString("password", &password);
			_StartHotspot(ssid, password);
			break;
		}

		case kMsgStopHotspot:
			_StopHotspot();
			break;

		case kMsgHotspotStateReply:
		{
			// Rebuilding the whole pane here re-requested this reply, so the
			// pane rebuilt forever; only the Hotspot tab depends on it.
			bool changed = false;
			const char* boolFields[] = { kNMFieldHotspotActive,
				kNMFieldHotspotCanStart };
			for (const char* field : boolFields) {
				bool oldValue = false;
				bool newValue = false;
				fHotspotState.FindBool(field, &oldValue);
				message->FindBool(field, &newValue);
				changed |= oldValue != newValue;
			}
			const char* stringFields[] = { kNMFieldHotspotSSID,
				kNMFieldHotspotPassword };
			for (const char* field : stringFields) {
				BString oldValue;
				BString newValue;
				fHotspotState.FindString(field, &oldValue);
				message->FindString(field, &newValue);
				changed |= oldValue != newValue;
			}

			fHotspotState = *message;
			if (changed)
				_RebuildHotspotTab();
			break;
		}

		case kMsgHotspotActionResult:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status != B_OK)
				_ShowHotspotError(message);
			// Save a newly minted profile UUID so the next start reuses it.
			BString uuid;
			if (message->FindString(kNMFieldHotspotUUID, &uuid) == B_OK
				&& !uuid.IsEmpty()) {
				fHotspotSettings.SetProfileUUID(uuid);
			}
			_RequestHotspotState();
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
	// Same device again (refresh): keep the tab; another one starts over.
	BString oldPath, newPath;
	fDeviceInfo.FindString(kNMFieldPath, &oldPath);
	deviceInfo.FindString(kNMFieldPath, &newPath);
	if (fMode != MODE_DEVICE || oldPath != newPath)
		fSelectedTab = 0;

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
InterfaceDetailView::ShowVPNSection(const BMessage& vpns)
{
	fMode = MODE_VPN_SECTION;
	fDeviceInfo = vpns;
	_Rebuild();
}


void
InterfaceDetailView::ShowWiFiSection(const BMessage& adapters)
{
	fMode = MODE_WIFI_SECTION;
	fDeviceInfo = adapters;
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

	// Hotspot and saved-network replies rebuild the pane; keep the tab.
	if (fTabView != NULL)
		fSelectedTab = fTabView->Selection();
	fTabView = NULL;

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
	fVPNRemoveButton = NULL;
	fHotspotStartButton = NULL;
	fHotspotStopButton = NULL;
	fHotspotPage = NULL;

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

	if (fMode == MODE_VPN_SECTION) {
		_RebuildVPNSectionView();
		return;
	}

	if (fMode == MODE_WIFI_SECTION) {
		_RebuildWiFiSectionView();
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

	// Live addresses via BNetworkInterface, which reads NMBackend's device snapshot; the stubs it
	// replaced always returned empty, leaving this row at "None" (#236).
	BNetworkInterface iface(interfaceName.String());
	if (iface.Exists()) {
		BNetworkInterfaceAddress addr;
		bool haveIPv4 = false;
		bool haveIPv6 = false;
		BString ipv4Line;
		BString ipv6Line;
		for (int32 i = 0; i < iface.CountAddresses(); i++) {
			if (iface.GetAddressAt(i, addr) != B_OK)
				continue;

			int family = addr.Address().Family();
			if (family != AF_INET && family != AF_INET6)
				continue;

			BString ip;
			ip << addr.Address().ToString();
			ssize_t prefix = addr.Mask().PrefixLength();
			if (prefix > 0)
				ip << "/" << prefix;

			if (family == AF_INET) {
				if (haveIPv4)
					ipv4Line << ", ";
				ipv4Line << ip;
				haveIPv4 = true;
			} else {
				if (haveIPv6)
					ipv6Line << ", ";
				ipv6Line << ip;
				haveIPv6 = true;
			}
		}
		addRow(B_TRANSLATE("IP Address:"),
			haveIPv4 ? ipv4Line : BString(B_TRANSLATE("None")));
		addRow(B_TRANSLATE("IPv6 Address:"),
			haveIPv6 ? ipv6Line : BString(B_TRANSLATE("None")));

		ifreq_stats stats;
		if (iface.GetStats(stats) == B_OK) {
			addRow(B_TRANSLATE("Sent:"), _FormatBytes(stats.send.bytes));
			addRow(B_TRANSLATE("Received:"), _FormatBytes(stats.receive.bytes));
		}
	} else {
		addRow(B_TRANSLATE("IP Address:"), kNotYetAvailable);
		addRow(B_TRANSLATE("IPv6 Address:"), kNotYetAvailable);
	}

	// Gateway/DNS come from the same live snapshot (IPv4 lease preferred, IPv6 fallback), not the profile.
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

	// One section per tab: stacked, a Wi-Fi device's sections are taller
	// than the screen and the window grows past its bottom edge.
	fTabView = new BTabView("tabs", B_WIDTH_FROM_LABEL);

	BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(titleView)
		.Add(fTabView);

	if (isWiFi) {
		BLayoutBuilder::Group<> builder(_AddTab(B_TRANSLATE("Networks")));
		builder.Add(new BStringView(NULL,
			B_TRANSLATE("Available networks:")));

		fWiFiListView = new BListView("wifiList", B_SINGLE_SELECTION_LIST);
		fWiFiListView->SetSelectionMessage(new BMessage(
			kMsgWiFiSelectionChanged));
		fWiFiListView->SetTarget(this);
		// Min keeps a usable strip when the scan is empty; max stops the
		// layout from giving this list every AP row in range.
		fWiFiListView->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120));
		fWiFiListView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 200));
		_FillWiFiList(networks);

		BScrollView* scrollView = new BScrollView("wifiScroll",
			fWiFiListView, 0, false, true);

		fJoinButton = new BButton("join", B_TRANSLATE("Join"),
			new BMessage(kMsgJoinWiFi));
		fForgetButton = new BButton("forget", B_TRANSLATE("Forget"),
			new BMessage(kMsgForgetWiFi));
		fJoinButton->SetTarget(this);
		fForgetButton->SetTarget(this);
		BMessage* joinOther = new BMessage(kMsgJoinOtherWiFi);
		joinOther->AddString("device", devicePath);

		builder.Add(scrollView)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(fJoinButton)
				.Add(fForgetButton)
				.AddGlue()
				.Add(new BButton("joinOther",
					B_TRANSLATE("Join other network" B_UTF8_ELLIPSIS),
					joinOther))
			.End();

		_UpdateWiFiButtons();

		builder.Add(new BStringView(NULL, B_TRANSLATE("Saved networks:")));

		fSavedListView = new SavedNetworkListView("savedList",
			new BMessage(kMsgSavedReordered));
		fSavedListView->SetSelectionMessage(
			new BMessage(kMsgSavedSelectionChanged));
		fSavedListView->SetTarget(this);
		fSavedListView->SetExplicitMinSize(BSize(B_SIZE_UNSET, 100));
		fSavedListView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 160));

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
	}

	BLayoutBuilder::Group<>(_AddTab(B_TRANSLATE("Status")))
		.Add(grid)
		.AddGlue();

	if (showStaticIP) {
		BLayoutBuilder::Group<>(_AddTab(B_TRANSLATE("IPv4")))
			.Add(fStaticIPView)
			.AddGlue();
	}

	if (isWiFi) {
		fHotspotPage = new BGroupView(B_TRANSLATE("Hotspot"), B_VERTICAL);
		fTabView->AddTab(fHotspotPage);
		_RebuildHotspotTab();

		_RequestSavedNetworks();
		_RequestHotspotState();
	}

	if (fSelectedTab > 0 && fSelectedTab < fTabView->CountTabs())
		fTabView->Select(fSelectedTab);
}


void
InterfaceDetailView::_RebuildHotspotTab()
{
	if (fHotspotPage == NULL)
		return;

	for (int32 i = fHotspotPage->CountChildren(); i-- > 0;) {
		BView* child = fHotspotPage->ChildAt(i);
		fHotspotPage->RemoveChild(child);
		delete child;
	}
	fHotspotStartButton = NULL;
	fHotspotStopButton = NULL;

	// Fresh layout, as in _Rebuild(): the old one still holds the glue.
	BGroupLayout* layout = new BGroupLayout(B_VERTICAL);
	fHotspotPage->SetLayout(layout);
	layout->SetInsets(B_USE_DEFAULT_SPACING);
	_AddHotspotSection(layout);
	BLayoutBuilder::Group<>(layout).AddGlue();
}


BGroupLayout*
InterfaceDetailView::_AddTab(const char* label)
{
	BGroupView* page = new BGroupView(label, B_VERTICAL);
	page->GroupLayout()->SetInsets(B_USE_DEFAULT_SPACING);
	fTabView->AddTab(page);
	return page->GroupLayout();
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
InterfaceDetailView::RefreshWiFiNetworks(const char* devicePath)
{
	BString shownPath;
	if (fMode != MODE_DEVICE || fWiFiListView == NULL
		|| fDeviceInfo.FindString(kNMFieldPath, &shownPath) != B_OK
		|| shownPath != devicePath) {
		return;
	}

	NMBackend* backend = NMBackend::Instance();
	BMessage networks;
	if (backend == NULL
		|| backend->GetWiFiNetworks(devicePath, &networks) != B_OK) {
		return;
	}

	BString selectedSSID;
	WiFiNetworkItem* selected = dynamic_cast<WiFiNetworkItem*>(
		fWiFiListView->ItemAt(fWiFiListView->CurrentSelection()));
	if (selected != NULL)
		selectedSSID = selected->SSID();

	BListItem* stale;
	while ((stale = fWiFiListView->RemoveItem((int32)0)) != NULL)
		delete stale;
	_FillWiFiList(networks);

	for (int32 i = 0; i < fWiFiListView->CountItems(); i++) {
		WiFiNetworkItem* item = dynamic_cast<WiFiNetworkItem*>(
			fWiFiListView->ItemAt(i));
		if (item != NULL && selectedSSID.Length() > 0
			&& item->SSID() == selectedSSID) {
			fWiFiListView->Select(i);
			break;
		}
	}
	_UpdateWiFiButtons();
}


void
InterfaceDetailView::_FillWiFiList(const BMessage& networks)
{
	int32 apCount = 0;
	networks.FindInt32(kNMFieldAPCount, &apCount);
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


static const char*
_VPNStateLabel(const BMessage& info)
{
	bool connected = false;
	bool activating = false;
	info.FindBool(kNMFieldVPNConnected, &connected);
	info.FindBool(kNMFieldVPNActivating, &activating);
	if (connected)
		return B_TRANSLATE("Connected");
	if (activating)
		return B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS);
	return B_TRANSLATE("Disconnected");
}


void
InterfaceDetailView::_RebuildVPNView()
{
	BString name, path;
	bool connected = false;
	bool activating = false;
	fDeviceInfo.FindString(kNMFieldVPNName, &name);
	fDeviceInfo.FindString(kNMFieldVPNPath, &path);
	fDeviceInfo.FindBool(kNMFieldVPNConnected, &connected);
	fDeviceInfo.FindBool(kNMFieldVPNActivating, &activating);

	BStringView* titleView = new BStringView(NULL, name.String());
	titleView->SetFont(be_bold_font);

	BGridView* grid = new BGridView(B_USE_HALF_ITEM_SPACING,
		B_USE_HALF_ITEM_SPACING);
	fGridLayout = grid->GridLayout();
	int32 row = 0;
	auto addRow = [&](const char* label, const char* value, bool bold) {
		BStringView* valueView = new BStringView(NULL, value);
		if (bold)
			valueView->SetFont(be_bold_font);
		fGridLayout->AddView(new BStringView(NULL, label), 0, row);
		fGridLayout->AddView(valueView, 1, row);
		row++;
	};

	addRow(B_TRANSLATE("Status:"), _VPNStateLabel(fDeviceInfo), true);
	BString value;
	if (fDeviceInfo.FindString(kNMFieldVPNType, &value) == B_OK)
		addRow(B_TRANSLATE("Type:"), value, false);
	if (fDeviceInfo.FindString(kNMFieldVPNServer, &value) == B_OK)
		addRow(B_TRANSLATE("Server:"), value, false);
	if (fDeviceInfo.FindString(kNMFieldVPNUser, &value) == B_OK)
		addRow(B_TRANSLATE("User:"), value, false);
	for (int32 i = 0;
			fDeviceInfo.FindString(kNMFieldVPNAddress, i, &value) == B_OK; i++)
		addRow(i == 0 ? B_TRANSLATE("Address:") : "", value, false);
	for (int32 i = 0;
			fDeviceInfo.FindString(kNMFieldVPNDNS, i, &value) == B_OK; i++)
		addRow(i == 0 ? B_TRANSLATE("DNS:") : "", value, false);
	bool autoconnect = false;
	fDeviceInfo.FindBool(kNMFieldVPNAutoconnect, &autoconnect);
	addRow(B_TRANSLATE("Connect automatically:"),
		autoconnect ? B_TRANSLATE("Yes") : B_TRANSLATE("No"), false);

	fVPNConnectButton = new BButton("vpnConnect", B_TRANSLATE("Connect"),
		new BMessage(kMsgConnectVPN));
	fVPNDisconnectButton = new BButton("vpnDisconnect",
		B_TRANSLATE("Disconnect"), new BMessage(kMsgDisconnectVPN));
	fVPNRemoveButton = new BButton("vpnRemove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemoveVPN));
	fVPNConnectButton->SetEnabled(!connected && !activating);
	fVPNDisconnectButton->SetEnabled(connected || activating);
	fVPNConnectButton->SetTarget(this);
	fVPNDisconnectButton->SetTarget(this);
	fVPNRemoveButton->SetTarget(this);

	BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(titleView)
		.Add(grid)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fVPNConnectButton)
			.Add(fVPNDisconnectButton)
			.Add(fVPNRemoveButton)
			.AddGlue()
		.End()
		.AddGlue();
}


void
InterfaceDetailView::_RebuildWiFiSectionView()
{
	BStringView* titleView = new BStringView(NULL, B_TRANSLATE("Wi-Fi"));
	titleView->SetFont(be_bold_font);

	BGroupLayout* layout = (BGroupLayout*)GetLayout();
	BLayoutBuilder::Group<>(layout)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(titleView);

	BString path;
	if (fDeviceInfo.FindString("path", 0, &path) != B_OK) {
		layout->AddView(new BStringView(NULL,
			B_TRANSLATE("No Wi-Fi adapter found")));
		layout->AddItem(BSpaceLayoutItem::CreateGlue());
		return;
	}

	BGridView* grid = new BGridView(B_USE_DEFAULT_SPACING,
		B_USE_HALF_ITEM_SPACING);
	BGridLayout* gridLayout = grid->GridLayout();
	BString name, status;
	for (int32 i = 0; fDeviceInfo.FindString("name", i, &name) == B_OK; i++) {
		fDeviceInfo.FindString("status", i, &status);
		BStringView* nameView = new BStringView(NULL, name);
		nameView->SetFont(be_bold_font);
		gridLayout->AddView(nameView, 0, i);
		gridLayout->AddView(new BStringView(NULL, status), 1, i);
	}
	layout->AddView(grid);

	BStringView* hint = new BStringView(NULL, B_TRANSLATE("Select an adapter "
		"to see the networks in range."));
	hint->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT));
	layout->AddView(hint);

	BLayoutBuilder::Group<>(layout)
		.AddGroup(B_HORIZONTAL)
			.Add(new BButton("joinOther",
				B_TRANSLATE("Join other network" B_UTF8_ELLIPSIS),
				new BMessage(kMsgJoinOtherWiFi)))
			.AddGlue()
		.End()
		.AddGlue();
}


void
InterfaceDetailView::_RebuildVPNSectionView()
{
	BStringView* titleView = new BStringView(NULL, B_TRANSLATE("VPN"));
	titleView->SetFont(be_bold_font);

	BGroupLayout* layout = (BGroupLayout*)GetLayout();
	BLayoutBuilder::Group<>(layout)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(titleView);

	int32 count = 0;
	fDeviceInfo.FindInt32(kNMFieldVPNCount, &count);
	if (count == 0) {
		BStringView* empty = new BStringView(NULL,
			B_TRANSLATE("No VPN connections. Import the configuration file "
				"your VPN provider gave you."));
		layout->AddView(empty);
	} else {
		BGridView* grid = new BGridView(B_USE_DEFAULT_SPACING,
			B_USE_HALF_ITEM_SPACING);
		BGridLayout* gridLayout = grid->GridLayout();
		const char* headings[] = { B_TRANSLATE("Name"), B_TRANSLATE("Type"),
			B_TRANSLATE("Status") };
		for (int32 column = 0; column < 3; column++) {
			BStringView* heading = new BStringView(NULL, headings[column]);
			heading->SetFont(be_bold_font);
			gridLayout->AddView(heading, column, 0);
		}
		for (int32 i = 0; i < count; i++) {
			char field[32];
			snprintf(field, sizeof(field), "vpn_%" B_PRId32, i);
			BMessage info;
			if (fDeviceInfo.FindMessage(field, &info) != B_OK)
				continue;
			BString name, type;
			info.FindString(kNMFieldVPNName, &name);
			info.FindString(kNMFieldVPNType, &type);
			gridLayout->AddView(new BStringView(NULL, name), 0, i + 1);
			gridLayout->AddView(new BStringView(NULL, type), 1, i + 1);
			gridLayout->AddView(new BStringView(NULL, _VPNStateLabel(info)),
				2, i + 1);
		}
		layout->AddView(grid);
	}

	BLayoutBuilder::Group<>(layout)
		.AddGroup(B_HORIZONTAL)
			.Add(new BButton("importVPN",
				B_TRANSLATE("Import VPN" B_UTF8_ELLIPSIS),
				new BMessage(kMsgImportVPN)))
			.AddGlue()
		.End()
		.AddGlue();
}


void
InterfaceDetailView::_AddHotspotSection(BGroupLayout* layout)
{
	BLayoutBuilder::Group<> builder(layout);

	bool active = false;
	bool canStart = true;
	fHotspotState.FindBool(kNMFieldHotspotActive, &active);
	fHotspotState.FindBool(kNMFieldHotspotCanStart, &canStart);

	if (active) {
		BString ssid, password;
		fHotspotState.FindString(kNMFieldHotspotSSID, &ssid);
		fHotspotState.FindString(kNMFieldHotspotPassword, &password);

		BGridView* grid = new BGridView(B_USE_HALF_ITEM_SPACING,
			B_USE_HALF_ITEM_SPACING);
		BGridLayout* layout = grid->GridLayout();
		layout->AddView(new BStringView(NULL, B_TRANSLATE("Status:")), 0, 0);
		BStringView* statusValue = new BStringView(NULL,
			B_TRANSLATE("Active"));
		statusValue->SetFont(be_bold_font);
		layout->AddView(statusValue, 1, 0);
		layout->AddView(new BStringView(NULL, B_TRANSLATE("Name:")), 0, 1);
		BStringView* ssidValue = new BStringView(NULL, ssid.String());
		ssidValue->SetFont(be_bold_font);
		layout->AddView(ssidValue, 1, 1);
		layout->AddView(new BStringView(NULL, B_TRANSLATE("Password:")),
			0, 2);
		BStringView* passwordValue = new BStringView(NULL,
			password.String());
		passwordValue->SetFont(be_bold_font);
		layout->AddView(passwordValue, 1, 2);

		fHotspotStopButton = new BButton("stopHotspot",
			B_TRANSLATE("Stop"), new BMessage(kMsgStopHotspot));
		fHotspotStopButton->SetTarget(this);

		builder.Add(grid)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(fHotspotStopButton)
				.AddGlue()
			.End();
	} else {
		fHotspotStartButton = new BButton("startHotspot",
			B_TRANSLATE("Turn On Wi-Fi Hotspot"),
			new BMessage(kMsgStartHotspot));
		fHotspotStartButton->SetTarget(this);
		fHotspotStartButton->SetEnabled(canStart);
		builder.Add(fHotspotStartButton);
	}
}


void
InterfaceDetailView::_RequestHotspotState()
{
	BString devicePath;
	fDeviceInfo.FindString(kNMFieldPath, &devicePath);
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL || devicePath.IsEmpty())
		return;
	backend->GetHotspotStateAsync(devicePath.String(),
		fHotspotSettings.ProfileUUID().String(), BMessenger(this),
		kMsgHotspotStateReply);
}


void
InterfaceDetailView::_ShowHotspotError(BMessage* message)
{
	int32 status = B_ERROR;
	message->FindInt32("status", &status);
	BString reason;
	message->FindString("reason", &reason);
	BString text(B_TRANSLATE("Could not update the hotspot."));
	if (status == B_NOT_SUPPORTED)
		text = B_TRANSLATE("This Wi-Fi adapter cannot start a hotspot.");
	else if (status == B_BAD_VALUE)
		text = B_TRANSLATE("Hotspot name and password are required; "
			"password must be at least 8 characters.");
	else if (!reason.IsEmpty())
		text << "\n" << reason;
	BAlert* alert = new BAlert(B_TRANSLATE("Hotspot"), text.String(),
		B_TRANSLATE("OK"));
	alert->Go(NULL);
}


void
InterfaceDetailView::_StartHotspot(const BString& ssid, const BString& password)
{
	BString devicePath;
	fDeviceInfo.FindString(kNMFieldPath, &devicePath);
	if (devicePath.IsEmpty() || ssid.IsEmpty() || password.Length() < 8)
		return;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;

	status_t status = backend->StartHotspotAsync(devicePath.String(),
		fHotspotSettings.ProfileUUID().String(), ssid.String(),
		password.String(), BMessenger(this), kMsgHotspotActionResult);
	if (status != B_OK) {
		BMessage failure(kMsgHotspotActionResult);
		failure.AddInt32("status", (int32)status);
		failure.AddString("reason", strerror(status));
		BMessenger(this).SendMessage(&failure);
	}
}


void
InterfaceDetailView::_StopHotspot()
{
	if (fHotspotSettings.ProfileUUID().IsEmpty())
		return;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;

	status_t status = backend->StopHotspotAsync(
		fHotspotSettings.ProfileUUID().String(), BMessenger(this),
		kMsgHotspotActionResult);
	if (status != B_OK) {
		BMessage failure(kMsgHotspotActionResult);
		failure.AddInt32("status", (int32)status);
		failure.AddString("reason", strerror(status));
		BMessenger(this).SendMessage(&failure);
	}
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
