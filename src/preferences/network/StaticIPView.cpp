/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "StaticIPView.h"

#include "IPAddressControl.h"
#include "NMBackend.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <NetworkAddress.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <stdlib.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "StaticIPView"


static const uint32 kModeAuto = 'iAto';
static const uint32 kModeStatic = 'iStc';
static const uint32 kModeDisabled = 'iOff';
static const uint32 kMsgApply = 'iApl';
static const uint32 kMsgRevert = 'iRvt';
static const uint32 kMsgFieldModified = 'iFld';
static const uint32 kMsgApplyResult = 'iApR';
static const uint32 kMsgProfileSelected = 'iPsl';
static const uint32 kMsgProfileIP4Loaded = 'iPil';
static const uint32 kMsgNewProfile = 'iNPr';
static const uint32 kMsgNewProfileCommit = 'iNPc';
static const uint32 kMsgCreateProfileResult = 'iCPr';
static const uint32 kMsgCreateProfilesReloaded = 'iCPl';


// Small non-modal dialog that only collects a name -- there is no password
// or security field here (and never should be: the NM SecretAgent owns
// credential prompts), just the connection id. Posts kMsgNewProfileCommit
// to fTarget and quits itself; the parent StaticIPView never blocks on it.
class NewProfileWindow : public BWindow {
public:
	NewProfileWindow(const BMessenger& target, const char* defaultName)
		:
		BWindow(BRect(0, 0, 320, 100), B_TRANSLATE("New connection profile"),
			B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS),
		fTarget(target)
	{
		fNameControl = new BTextControl(B_TRANSLATE("Name:"), defaultName,
			NULL);
		fNameControl->TextView()->SetExplicitMinSize(BSize(200, B_SIZE_UNSET));

		BButton* cancelButton = new BButton(B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));
		BButton* createButton = new BButton(B_TRANSLATE("Create"),
			new BMessage(kMsgNewProfileCommit));
		createButton->MakeDefault(true);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(fNameControl)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancelButton)
				.Add(createButton)
			.End();

		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgNewProfileCommit) {
			BString name(fNameControl->Text());
			name.Trim();
			if (!name.IsEmpty()) {
				BMessage commit(kMsgNewProfileCommit);
				commit.AddString("name", name);
				fTarget.SendMessage(&commit);
				Quit();
			}
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BMessenger		fTarget;
	BTextControl*	fNameControl;
};


StaticIPView::StaticIPView()
	:
	BView("staticIP", B_WILL_DRAW),
	fProfilePopUpMenu(NULL),
	fProfileField(NULL),
	fProfileCount(0),
	fProfileLoadPending(false),
	fNewProfileButton(NULL),
	fCreateProfilePending(false),
	fSnapshotMode(kModeAuto)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fModePopUpMenu = new BPopUpMenu("ipv4modes");
	fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("DHCP"),
		new BMessage(kModeAuto)));
	fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("Static"),
		new BMessage(kModeStatic)));
	fModePopUpMenu->AddSeparatorItem();
	fModePopUpMenu->AddItem(new BMenuItem(B_TRANSLATE("Disabled"),
		new BMessage(kModeDisabled)));

	fModeField = new BMenuField(B_TRANSLATE("Mode:"), fModePopUpMenu);
	fModeField->SetToolTip(
		B_TRANSLATE("The method for obtaining an IPv4 address"));

	// Populated by SetProfiles() once the device's saved connection profiles
	// are known -- empty (and disabled) until then, see _UpdateApplyState().
	fProfilePopUpMenu = new BPopUpMenu(B_TRANSLATE("(none)"));
	fProfileField = new BMenuField(B_TRANSLATE("Profile:"), fProfilePopUpMenu);
	fProfileField->SetToolTip(B_TRANSLATE(
		"The saved connection profile these settings apply to"));
	fProfileField->SetEnabled(false);

	float minimumWidth = be_control_look->DefaultItemSpacing() * 15;

	fAddressField = new IPAddressControl(AF_INET, B_TRANSLATE("Address:"),
		NULL);
	fAddressField->SetToolTip(B_TRANSLATE("Your IPv4 address"));
	fAddressField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));
	fAddressField->SetAllowEmpty(false);
	fAddressField->SetModificationMessage(new BMessage(kMsgFieldModified));

	fNetmaskField = new IPAddressControl(AF_INET, B_TRANSLATE("Netmask:"),
		NULL);
	fNetmaskField->SetToolTip(B_TRANSLATE(
		"The netmask defines your local network"));
	fNetmaskField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));
	fNetmaskField->SetModificationMessage(new BMessage(kMsgFieldModified));

	fGatewayField = new IPAddressControl(AF_INET, B_TRANSLATE("Gateway:"),
		NULL);
	fGatewayField->SetToolTip(B_TRANSLATE("Your gateway to the internet"));
	fGatewayField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));
	fGatewayField->SetModificationMessage(new BMessage(kMsgFieldModified));

	fDNSField = new BTextControl(NULL, B_TRANSLATE("DNS servers:"), "",
		new BMessage(kMsgFieldModified));
	fDNSField->SetToolTip(
		B_TRANSLATE("Comma-separated list of DNS server addresses"));
	fDNSField->TextView()->SetExplicitMinSize(
		BSize(minimumWidth, B_SIZE_UNSET));

	fApplyButton = new BButton("applyIP", B_TRANSLATE("Apply"),
		new BMessage(kMsgApply));
	fRevertButton = new BButton("revertIP", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert));

	// Wired-only -- see CreateProfile() -- so disabled until SetToDevice()
	// reports an Ethernet device.
	fNewProfileButton = new BButton("newProfile", B_TRANSLATE("New" B_UTF8_ELLIPSIS),
		new BMessage(kMsgNewProfile));
	fNewProfileButton->SetEnabled(false);

	fReasonView = new BStringView(NULL, "");
	fReasonView->SetFont(be_plain_font);
	fReasonView->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_3_TINT));
	fReasonView->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_UNSET));

	_CaptureSnapshot();
	_SetMode(kModeAuto);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.AddGrid(B_USE_HALF_ITEM_SPACING, B_USE_HALF_ITEM_SPACING)
			.AddMenuField(fProfileField, 0, 0, B_ALIGN_RIGHT)
				.Add(fNewProfileButton, 2, 0)
			.AddMenuField(fModeField, 0, 1, B_ALIGN_RIGHT)
			.AddTextControl(fAddressField, 0, 2, B_ALIGN_RIGHT)
			.AddTextControl(fNetmaskField, 0, 3, B_ALIGN_RIGHT)
			.AddTextControl(fGatewayField, 0, 4, B_ALIGN_RIGHT)
			.AddTextControl(fDNSField, 0, 5, B_ALIGN_RIGHT)
		.End()
		.Add(fReasonView)
		.AddGroup(B_HORIZONTAL)
			.Add(fRevertButton)
			.AddGlue()
			.Add(fApplyButton)
		.End();

	_UpdateApplyState();
}


StaticIPView::~StaticIPView()
{
}


void
StaticIPView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fModePopUpMenu->SetTargetForItems(this);
	fProfilePopUpMenu->SetTargetForItems(this);
	fApplyButton->SetTarget(this);
	fRevertButton->SetTarget(this);
	fNewProfileButton->SetTarget(this);
	fAddressField->SetTarget(this);
	fNetmaskField->SetTarget(this);
	fGatewayField->SetTarget(this);
	fDNSField->SetTarget(this);
}


void
StaticIPView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kModeAuto:
		case kModeStatic:
		case kModeDisabled:
			_SetMode(message->what);
			_UpdateApplyState();
			break;

		case kMsgFieldModified:
			_UpdateApplyState();
			break;

		case kMsgApply:
			_DoApply();
			break;

		case kMsgRevert:
			Revert();
			break;

		case kMsgProfileSelected:
		{
			BString path;
			if (message->FindString("path", &path) == B_OK)
				_SelectProfile(path, true);
			break;
		}

		case kMsgProfileIP4Loaded:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			BString path;
			message->FindString(kNMFieldPath, &path);
			// A reply for a profile that is no longer the selected one (the
			// user switched again before this arrived) must not clobber
			// whatever the current selection already seeded -- last request
			// wins, not last reply.
			if (path != fSelectedProfilePath)
				break;
			// Clear the pending flag whatever the outcome, or a profile whose
			// settings failed to load would leave Apply disabled forever.
			fProfileLoadPending = false;
			if (status == B_OK)
				_ApplyIP4Fields(*message);
			else
				_UpdateApplyState();
			break;
		}

		case kMsgApplyResult:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status == B_OK) {
				_CaptureSnapshot();
			} else {
				BString reason;
				message->FindString("reason", &reason);
				BString text(B_TRANSLATE("Could not apply the IPv4 settings."));
				if (!reason.IsEmpty())
					text << "\n" << reason;
				BAlert* alert = new BAlert(B_TRANSLATE("Not applied"),
					text.String(), B_TRANSLATE("OK"));
				alert->Go(NULL);
			}
			_UpdateApplyState();
			break;
		}

		case kMsgNewProfile:
			_ShowNewProfileDialog();
			break;

		case kMsgNewProfileCommit:
		{
			BString name;
			if (message->FindString("name", &name) == B_OK)
				CreateProfile(name);
			break;
		}

		case kMsgCreateProfileResult:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status == B_OK) {
				BString path;
				message->FindString(kNMFieldProfilePath, &path);
				fPendingSelectPath = path;

				NMBackend* backend = NMBackend::Instance();
				if (backend != NULL && !fDevicePath.IsEmpty()) {
					backend->GetDeviceConnectionProfilesAsync(
						fDevicePath.String(), BMessenger(this),
						kMsgCreateProfilesReloaded);
				} else {
					fCreateProfilePending = false;
				}
			} else {
				BString reason;
				message->FindString("reason", &reason);
				BString text(
					B_TRANSLATE("Could not create the connection profile."));
				if (!reason.IsEmpty())
					text << "\n" << reason;
				BAlert* alert = new BAlert(B_TRANSLATE("Not created"),
					text.String(), B_TRANSLATE("OK"));
				alert->Go(NULL);
				fCreateProfilePending = false;
			}
			_UpdateNewProfileButtonState();
			break;
		}

		case kMsgCreateProfilesReloaded:
		{
			// Refresh via the normal SetProfiles() path, then override its
			// default selection with the profile just created -- reuses
			// _SelectProfile()/_RequestProfileIP4()/_ApplyIP4Fields() so the
			// new profile is seeded and the dirty snapshot recaptured
			// exactly like any other profile switch.
			SetProfiles(*message);
			if (!fPendingSelectPath.IsEmpty()) {
				_SelectProfile(fPendingSelectPath, false);
				// SetProfiles() already marked its own default choice in the
				// menu; correct that to the profile actually selected above.
				for (int32 i = 0; i < fProfilePopUpMenu->CountItems(); i++) {
					BMenuItem* item = fProfilePopUpMenu->ItemAt(i);
					BString itemPath;
					if (item->Message() != NULL
						&& item->Message()->FindString("path", &itemPath)
							== B_OK
						&& itemPath == fPendingSelectPath) {
						item->SetMarked(true);
						break;
					}
				}
				fPendingSelectPath = "";
			}
			fCreateProfilePending = false;
			_UpdateNewProfileButtonState();
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


// Seeds mode/address/netmask/gateway/DNS from deviceInfo's kNMFieldIP4*
// fields -- what NMBackend read off the active connection's real
// NMSettingIPConfig, not a fabricated default. A device with no active
// connection (kNMFieldIP4Method "unknown") falls back to the DHCP/blank
// state, same as before this call existed.
void
StaticIPView::SetToDevice(const BMessage& deviceInfo)
{
	deviceInfo.FindString(kNMFieldInterface, &fInterfaceName);
	deviceInfo.FindString(kNMFieldPath, &fDevicePath);
	deviceInfo.FindString(kNMFieldType, &fDeviceType);

	// Seeds from the device's active-connection snapshot as an initial
	// placeholder -- SetProfiles(), called right after by the caller once
	// GetDeviceConnectionProfilesAsync() replies, supersedes this with the
	// selected profile's own data. fSelectedProfilePath stays empty until
	// then, so Apply is disabled the whole time regardless of what these
	// fields show.
	BString method, address, netmask, gateway, dns;
	deviceInfo.FindString(kNMFieldIP4Method, &method);
	deviceInfo.FindString(kNMFieldIP4Address, &address);
	deviceInfo.FindString(kNMFieldIP4Netmask, &netmask);
	deviceInfo.FindString(kNMFieldIP4Gateway, &gateway);
	deviceInfo.FindString(kNMFieldIP4DNS, &dns);

	uint32 mode = kModeAuto;
	if (method == "manual")
		mode = kModeStatic;
	else if (method == "disabled")
		mode = kModeDisabled;

	_SetMode(mode);
	fAddressField->SetText(mode == kModeStatic ? address.String() : "");
	fNetmaskField->SetText(mode == kModeStatic ? netmask.String() : "");
	fGatewayField->SetText(mode == kModeStatic ? gateway.String() : "");
	fDNSField->SetText(mode == kModeStatic ? dns.String() : "");

	fSelectedProfilePath = "";
	fProfileCount = 0;
	_SetProfilePlaceholder(B_TRANSLATE("Loading" B_UTF8_ELLIPSIS));
	fProfileField->SetEnabled(false);

	_CaptureSnapshot();
	_UpdateApplyState();
	_UpdateNewProfileButtonState();
}


void
StaticIPView::Clear()
{
	fInterfaceName = "";
	fDevicePath = "";
	fDeviceType = "";
	fSelectedProfilePath = "";
	fProfileCount = 0;
	_SetProfilePlaceholder(B_TRANSLATE("(none)"));
	fProfileField->SetEnabled(false);
	_SetMode(kModeAuto);
	fAddressField->SetText("");
	fNetmaskField->SetText("");
	fGatewayField->SetText("");
	fDNSField->SetText("");
	_CaptureSnapshot();
	_UpdateApplyState();
	_UpdateNewProfileButtonState();
}


// Populates the chooser from GetDeviceConnectionProfilesAsync()'s reply and
// selects the active profile (or the only one) by default. Does not itself
// warn about unsaved edits -- SetToDevice() has already reset the pane for
// the newly selected device by the time this runs.
void
StaticIPView::SetProfiles(const BMessage& profiles)
{
	while (fProfilePopUpMenu->CountItems() > 0)
		delete fProfilePopUpMenu->RemoveItem((int32)0);

	int32 count = 0;
	profiles.FindInt32(kNMFieldProfileCount, &count);
	fProfileCount = count;

	BString defaultPath;
	for (int32 i = 0; i < count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "profile_%" B_PRId32, i);
		BMessage entry;
		if (profiles.FindMessage(name, &entry) != B_OK)
			continue;

		BString id, path;
		bool active = false;
		entry.FindString(kNMFieldProfileID, &id);
		entry.FindString(kNMFieldProfilePath, &path);
		entry.FindBool(kNMFieldProfileActive, &active);
		if (path.IsEmpty())
			continue;

		BMessage* itemMessage = new BMessage(kMsgProfileSelected);
		itemMessage->AddString("path", path);
		BMenuItem* item = new BMenuItem(
			id.IsEmpty() ? path.String() : id.String(), itemMessage);
		fProfilePopUpMenu->AddItem(item);

		if (active || defaultPath.IsEmpty())
			defaultPath = path;
		if (active)
			item->SetMarked(true);
	}

	if (Window() != NULL)
		fProfilePopUpMenu->SetTargetForItems(this);

	fProfileField->SetEnabled(fProfileCount > 0);

	if (defaultPath.IsEmpty()) {
		_SetProfilePlaceholder(B_TRANSLATE("No saved profile"));
		fSelectedProfilePath = "";
		_UpdateApplyState();
		_UpdateNewProfileButtonState();
		return;
	}

	// Not a "user switched" event -- nothing to warn about, and there is no
	// previous selection's edits to lose.
	_SelectProfile(defaultPath, false);
	_UpdateNewProfileButtonState();
}


// Switches the pane to a different saved profile. If the currently shown
// profile has unsaved edits, asks first -- silently discarding them would
// lose work, and silently keeping them would apply Home's edits to Office
// (see the header comment on why this exists). Declining the switch restores
// the previously-marked menu item so the menu doesn't show a profile whose
// fields it didn't actually load.
void
StaticIPView::_SelectProfile(const BString& path, bool warnIfDirty)
{
	if (path == fSelectedProfilePath)
		return;

	if (warnIfDirty && IsDirty()) {
		BAlert* alert = new BAlert(B_TRANSLATE("Unsaved changes"),
			B_TRANSLATE("Switching profiles will discard your unsaved IPv4 "
				"changes for this profile. Discard them?"),
			B_TRANSLATE("Cancel"), B_TRANSLATE("Discard"), NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() == 0) {
			// Cancelled -- re-mark whichever item matches the profile still
			// actually selected so the menu doesn't lie about what's shown.
			for (int32 i = 0; i < fProfilePopUpMenu->CountItems(); i++) {
				BMenuItem* item = fProfilePopUpMenu->ItemAt(i);
				BString itemPath;
				if (item->Message() != NULL
					&& item->Message()->FindString("path", &itemPath) == B_OK
					&& itemPath == fSelectedProfilePath) {
					item->SetMarked(true);
					break;
				}
			}
			return;
		}
	}

	fSelectedProfilePath = path;
	fProfileLoadPending = true;
	_UpdateApplyState();
	_RequestProfileIP4(path);
}


// A BPopUpMenu shows whatever item is marked (or blank if none is); a
// disabled, marked placeholder item is the standard way to show
// "loading"/"none" text in the field before any real choice exists.
void
StaticIPView::_SetProfilePlaceholder(const char* label)
{
	while (fProfilePopUpMenu->CountItems() > 0)
		delete fProfilePopUpMenu->RemoveItem((int32)0);

	BMenuItem* placeholder = new BMenuItem(label, NULL);
	placeholder->SetEnabled(false);
	fProfilePopUpMenu->AddItem(placeholder);
	placeholder->SetMarked(true);
}


void
StaticIPView::_RequestProfileIP4(const BString& path)
{
	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;
	backend->GetConnectionIP4ConfigAsync(path.String(), BMessenger(this),
		kMsgProfileIP4Loaded);
}


// Applies a GetConnectionIP4ConfigAsync() reply's kNMFieldIP4* fields to the
// pane, then recaptures the dirty snapshot -- selecting a profile must never
// itself look like an edit, or Revert would have nothing to restore to and
// IsDirty() would be true the instant the menu changes.
void
StaticIPView::_ApplyIP4Fields(const BMessage& fields)
{
	BString method, address, netmask, gateway, dns;
	fields.FindString(kNMFieldIP4Method, &method);
	fields.FindString(kNMFieldIP4Address, &address);
	fields.FindString(kNMFieldIP4Netmask, &netmask);
	fields.FindString(kNMFieldIP4Gateway, &gateway);
	fields.FindString(kNMFieldIP4DNS, &dns);

	uint32 mode = kModeAuto;
	if (method == "manual")
		mode = kModeStatic;
	else if (method == "disabled")
		mode = kModeDisabled;

	_SetMode(mode);
	fAddressField->SetText(mode == kModeStatic ? address.String() : "");
	fNetmaskField->SetText(mode == kModeStatic ? netmask.String() : "");
	fGatewayField->SetText(mode == kModeStatic ? gateway.String() : "");
	fDNSField->SetText(mode == kModeStatic ? dns.String() : "");

	_CaptureSnapshot();
	_UpdateApplyState();
}


void
StaticIPView::_CaptureSnapshot()
{
	fSnapshotMode = _Mode();
	fSnapshotAddress = fAddressField != NULL ? fAddressField->Text() : "";
	fSnapshotNetmask = fNetmaskField != NULL ? fNetmaskField->Text() : "";
	fSnapshotGateway = fGatewayField != NULL ? fGatewayField->Text() : "";
	fSnapshotDNS = fDNSField != NULL ? fDNSField->Text() : "";
}


bool
StaticIPView::IsDirty() const
{
	if (fAddressField == NULL)
		return false;

	return _Mode() != fSnapshotMode
		|| fSnapshotAddress != fAddressField->Text()
		|| fSnapshotNetmask != fNetmaskField->Text()
		|| fSnapshotGateway != fGatewayField->Text()
		|| fSnapshotDNS != fDNSField->Text();
}


void
StaticIPView::Revert()
{
	_SetMode(fSnapshotMode);
	fAddressField->SetText(fSnapshotAddress);
	fNetmaskField->SetText(fSnapshotNetmask);
	fGatewayField->SetText(fSnapshotGateway);
	fDNSField->SetText(fSnapshotDNS);
	_UpdateApplyState();
}


void
StaticIPView::_SetMode(uint32 mode)
{
	BMenuItem* item = fModePopUpMenu->FindItem(mode);
	if (item != NULL)
		item->SetMarked(true);

	_EnableFields(mode == kModeStatic);

	if (mode == kModeDisabled) {
		fAddressField->SetText("");
		fNetmaskField->SetText("");
		fGatewayField->SetText("");
	}
}


void
StaticIPView::_EnableFields(bool enable)
{
	fAddressField->SetEnabled(enable);
	fNetmaskField->SetEnabled(enable);
	fGatewayField->SetEnabled(enable);
	fDNSField->SetEnabled(enable);
}


uint32
StaticIPView::_Mode() const
{
	uint32 mode = kModeAuto;
	BMenuItem* item = fModePopUpMenu->FindMarked();
	if (item != NULL)
		mode = item->Message()->what;

	return mode;
}


/*!	Validates the comma-separated DNS server list. Returns B_BAD_VALUE and
	the offending token in \a invalidToken if any entry doesn't parse as an
	address; B_OK (with \a invalidToken untouched) otherwise. An empty list
	is valid -- DNS is optional even in Static mode.
*/
status_t
StaticIPView::_ValidateDNSField(BString* invalidToken) const
{
	BString text(fDNSField->Text());
	text.Trim();
	if (text.IsEmpty())
		return B_OK;

	int32 start = 0;
	while (start < text.Length()) {
		int32 comma = text.FindFirst(',', start);
		BString token = comma < 0
			? BString(text.String() + start)
			: BString(text.String() + start, comma - start);
		token.Trim();
		start = comma < 0 ? text.Length() : comma + 1;

		if (token.IsEmpty())
			continue;

		BNetworkAddress address;
		if (address.SetTo(AF_INET, token.String(), (char*)NULL,
				B_NO_ADDRESS_RESOLUTION) != B_OK) {
			if (invalidToken != NULL)
				*invalidToken = token;
			return B_BAD_VALUE;
		}
	}

	return B_OK;
}


const char*
StaticIPView::ReasonApplyDisabled() const
{
	static BString reason;

	if (fDevicePath.IsEmpty()) {
		reason = B_TRANSLATE("No device selected.");
		return reason.String();
	}

	if (fSelectedProfilePath.IsEmpty()) {
		reason = fProfileCount == 0
			? B_TRANSLATE("This device has no saved connection profile.")
			: B_TRANSLATE("Loading connection profiles" B_UTF8_ELLIPSIS);
		return reason.String();
	}

	if (fProfileLoadPending) {
		reason = B_TRANSLATE("Loading this profile's settings"
			B_UTF8_ELLIPSIS);
		return reason.String();
	}

	if (!IsDirty()) {
		reason = B_TRANSLATE("No changes to apply.");
		return reason.String();
	}

	if (_Mode() == kModeStatic) {
		if (fAddressField->Text()[0] == '\0') {
			reason = B_TRANSLATE("Enter an IPv4 address.");
			return reason.String();
		}
		BNetworkAddress address;
		if (address.SetTo(AF_INET, fAddressField->Text(), (char*)NULL,
				B_NO_ADDRESS_RESOLUTION) != B_OK) {
			reason = B_TRANSLATE("The address is not a valid IPv4 address.");
			return reason.String();
		}
		// Required, not optional: without one there is no honest prefix
		// length to write, and guessing /24 would fabricate configuration.
		if (fNetmaskField->Text()[0] == '\0') {
			reason = B_TRANSLATE("Enter a netmask.");
			return reason.String();
		}
		if (address.SetTo(AF_INET, fNetmaskField->Text(), (char*)NULL,
				B_NO_ADDRESS_RESOLUTION) != B_OK) {
			reason = B_TRANSLATE("The netmask is not a valid IPv4 netmask.");
			return reason.String();
		}
		if (fGatewayField->Text()[0] != '\0'
			&& address.SetTo(AF_INET, fGatewayField->Text(), (char*)NULL,
				B_NO_ADDRESS_RESOLUTION) != B_OK) {
			reason = B_TRANSLATE("The gateway is not a valid IPv4 address.");
			return reason.String();
		}

		BString badToken;
		if (_ValidateDNSField(&badToken) != B_OK) {
			reason = B_TRANSLATE("\"%token%\" is not a valid DNS address.");
			reason.ReplaceFirst("%token%", badToken);
			return reason.String();
		}
	}

	// Everything validates and something actually changed: Apply is allowed.
	return NULL;
}


void
StaticIPView::_UpdateApplyState()
{
	_EnableFields(_Mode() == kModeStatic);

	const char* reason = ReasonApplyDisabled();
	fApplyButton->SetEnabled(reason == NULL);
	fApplyButton->SetToolTip(reason != NULL ? reason : "");

	fRevertButton->SetEnabled(IsDirty());

	fReasonView->SetText(reason != NULL ? reason : "");

	if (Window() != NULL)
		Window()->PostMessage(StaticIPView::kMsgDirtyChanged);
}


void
StaticIPView::_DoApply()
{
	// Mirrors _UpdateApplyState()'s enable condition -- not reachable through
	// the button when it disagrees, but not a silent no-op either if it ever
	// does.
	if (ReasonApplyDisabled() != NULL)
		return;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL) {
		fReasonView->SetText(B_TRANSLATE("NetworkManager is not available."));
		return;
	}

	NMBackend::IP4ConfigMode mode = NMBackend::IP4_CONFIG_AUTO;
	if (_Mode() == kModeStatic)
		mode = NMBackend::IP4_CONFIG_MANUAL;
	else if (_Mode() == kModeDisabled)
		mode = NMBackend::IP4_CONFIG_DISABLED;

	fApplyButton->SetEnabled(false);

	status_t status = backend->SetStaticIPConfigAsync(
		fSelectedProfilePath.String(), mode, fAddressField->Text(),
		fNetmaskField->Text(), fGatewayField->Text(), fDNSField->Text(),
		BMessenger(this), kMsgApplyResult);
	if (status != B_OK) {
		BMessage failure(kMsgApplyResult);
		failure.AddInt32("status", (int32)status);
		failure.AddString("reason", strerror(status));
		BMessenger(this).SendMessage(&failure);
	}
}


void
StaticIPView::_ShowNewProfileDialog()
{
	NewProfileWindow* window = new NewProfileWindow(BMessenger(this),
		fInterfaceName.String());
	window->Show();
}


// Enabled only for a selected, wired device while no create is already in
// flight -- CreateProfile() enforces the wired-only rule again itself, this
// only governs whether the button can be clicked at all.
void
StaticIPView::_UpdateNewProfileButtonState()
{
	if (fNewProfileButton == NULL)
		return;

	bool enabled = !fDevicePath.IsEmpty() && fDeviceType == "ethernet"
		&& !fCreateProfilePending;
	fNewProfileButton->SetEnabled(enabled);
}


// Wired only -- see NMBackend::CreateWiredConnectionProfileAsync()'s comment
// for why WiFi profile creation has no route through here.
void
StaticIPView::CreateProfile(const BString& name)
{
	if (fDeviceType != "ethernet" || fDevicePath.IsEmpty())
		return;

	if (fCreateProfilePending)
		return;

	NMBackend* backend = NMBackend::Instance();
	if (backend == NULL)
		return;

	fCreateProfilePending = true;
	_UpdateNewProfileButtonState();

	status_t status = backend->CreateWiredConnectionProfileAsync(
		fDevicePath.String(), name.String(), BMessenger(this),
		kMsgCreateProfileResult);
	if (status != B_OK) {
		BMessage failure(kMsgCreateProfileResult);
		failure.AddInt32("status", (int32)status);
		failure.AddString("reason", strerror(status));
		BMessenger(this).SendMessage(&failure);
	}
}
