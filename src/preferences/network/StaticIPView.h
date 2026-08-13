/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef STATIC_IP_VIEW_H
#define STATIC_IP_VIEW_H


#include <Messenger.h>
#include <String.h>
#include <View.h>


class BButton;
class BMenuField;
class BPopUpMenu;
class BStringView;
class BTextControl;
class IPAddressControl;


// IPv4 configuration pane. Ported from
// haiku-latest's InterfaceAddressView with two changes: a DNS servers field
// (upstream splits that into a separate view we don't have), and Apply/Revert
// wired to a dirty snapshot instead of writing straight through -- under
// NetworkManager, IPv4 configuration is a connection-profile transaction, not
// a live interface mutation, so it cannot use upstream's "Apply on message"
// shortcut untouched.
//
// Apply is disabled whenever it cannot succeed (no device selected, nothing
// changed, an invalid field, or -- the one case NMBackend itself reports --
// the device has no active connection profile to write to);
// ReasonApplyDisabled() always explains why.
class StaticIPView : public BView {
public:
			// Posted to Window() whenever IsDirty()/ReasonApplyDisabled()
			// may have changed, so the owning window can keep its own
			// Revert button in sync without polling every keystroke.
			static const uint32 kMsgDirtyChanged = 'iDty';

							StaticIPView();
	virtual					~StaticIPView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

			// Re-snapshots the pane for a newly selected device from its
			// GetDeviceInfo() message -- interface name and D-Bus device
			// path, plus an initial kNMFieldIP4* seed from the active
			// connection (a reasonable placeholder while the profile list
			// below is still loading; SetProfiles()/the per-profile reload it
			// triggers supersedes it once that arrives). Apply stays disabled
			// until a profile is selected either way.
			void			SetToDevice(const BMessage& deviceInfo);
			void			Clear();

			// Populates the profile chooser from a
			// GetDeviceConnectionProfilesAsync() reply (kNMFieldProfile*
			// shape) and selects the active profile (or the only one) by
			// default, kicking a GetConnectionIP4ConfigAsync() reload for it.
			// Zero profiles leaves the chooser empty and Apply disabled with
			// a stated reason -- no profile-creation fallback.
			void			SetProfiles(const BMessage& profiles);

			// Called once the user commits a name in the "New" profile
			// dialog. Wired devices only; see StaticIPView.cpp for why WiFi
			// is refused before this reaches NMBackend.
			void			CreateProfile(const BString& name);

			bool			IsDirty() const;
			void			Revert();

			// NULL when Apply would be enabled; otherwise the human-readable
			// reason it's disabled.
			const char*		ReasonApplyDisabled() const;

private:
			void			_CaptureSnapshot();
			void			_UpdateApplyState();
			void			_SetMode(uint32 mode);
			void			_EnableFields(bool enable);
			uint32			_Mode() const;
			status_t		_ValidateDNSField(BString* invalidToken) const;
			void			_DoApply();
			void			_SelectProfile(const BString& path,
								bool warnIfDirty);
			void			_RequestProfileIP4(const BString& path);
			void			_ApplyIP4Fields(const BMessage& fields);
			void			_SetProfilePlaceholder(const char* label);
			void			_ShowNewProfileDialog();
			void			_UpdateNewProfileButtonState();

			// Path of a just-created profile to select once the post-create
			// GetDeviceConnectionProfilesAsync() refresh (reusing
			// SetProfiles()) replies.
			BString			fPendingSelectPath;

			BString			fInterfaceName;
			BString			fDevicePath;
			BString			fDeviceType;

			BPopUpMenu*		fModePopUpMenu;
			BMenuField*		fModeField;
			IPAddressControl* fAddressField;
			IPAddressControl* fNetmaskField;
			IPAddressControl* fGatewayField;
			BTextControl*	fDNSField;
			BButton*		fApplyButton;
			BButton*		fRevertButton;
			BStringView*	fReasonView;

			// Profile chooser -- see SetProfiles(). fSelectedProfilePath is
			// what SetStaticIPConfigAsync() writes to; empty means "nothing
			// selected yet" (no profiles, or still loading), and Apply stays
			// disabled in that state.
			BPopUpMenu*		fProfilePopUpMenu;
			BMenuField*		fProfileField;
			BString			fSelectedProfilePath;
			int32			fProfileCount;
			// True between selecting a profile and its settings arriving: the
			// fields still hold the previous profile's values, so an edit made
			// now must not be appliable to the newly selected one.
			bool			fProfileLoadPending;

			// "New" profile creation -- wired devices only, see .cpp. True
			// while CreateWiredConnectionProfileAsync() is in flight, so a
			// second click can't fire a second create before the first
			// finishes.
			BButton*		fNewProfileButton;
			bool			fCreateProfilePending;

			uint32			fSnapshotMode;
			BString			fSnapshotAddress;
			BString			fSnapshotNetmask;
			BString			fSnapshotGateway;
			BString			fSnapshotDNS;
};


#endif	// STATIC_IP_VIEW_H
