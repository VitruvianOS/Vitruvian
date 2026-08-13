/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef INTERFACE_DETAIL_VIEW_H
#define INTERFACE_DETAIL_VIEW_H


#include <Message.h>
#include <String.h>
#include <View.h>


class BGridLayout;
class BListView;
class BButton;
class StaticIPView;


// Right-hand detail pane of the Network preflet, swapped in when a device or
// VPN row is selected in the outline list.
// Wired/WiFi device fields are live (GetDeviceInfo() + sysfs statistics +
// BNetworkInterface + ScanWiFiNetworks()); VPN mode shows the NM connection
// snapshot handed in by SetToVPN().
//
// Owns the embedded StaticIPView so its Apply/Revert dirty state
// can be surfaced to NetworkWindowNM's single Revert button -- a
// separate modal dialog would put that state a window away from the button
// that needs it.
class InterfaceDetailView : public BView {
public:
							InterfaceDetailView();
	virtual					~InterfaceDetailView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

			void			SetToDevice(const BMessage& deviceInfo);
			void			SetToVPN(const BMessage& vpnInfo);
			void			ShowEmpty(const char* message);

			bool			IsRevertable() const;
			void			Revert();

private:
			void			_Rebuild();
			void			_RebuildDeviceView();
			void			_RebuildVPNView();
			void			_UpdateWiFiButtons();
			void			_RequestSavedNetworks();
			void			_RebuildSavedList();
			void			_UpdateSavedButtons();
			void			_UpdateWiFiSavedMarkers();
			bool			_HasSavedProfile(const BString& ssid) const;
			void			_RenumberSavedList();

			enum Mode {
				MODE_EMPTY,
				MODE_DEVICE,
				MODE_VPN
			};

			BGridLayout*	fGridLayout;
			BMessage		fDeviceInfo;
			Mode			fMode;
			BString			fEmptyMessage;
			StaticIPView*	fStaticIPView;

			BListView*		fWiFiListView;
			BButton*		fJoinButton;
			BButton*		fForgetButton;

			// Saved-network management (NM's stored profiles, independent of
			// AP visibility) -- a separate list from fWiFiListView's in-range
			// scan results.
			BListView*		fSavedListView;
			BButton*		fSavedForgetButton;
			BButton*		fSavedAutoconnectButton;
			BButton*		fSavedMoveUpButton;
			BButton*		fSavedMoveDownButton;
			BMessage		fSavedNetworks;

			BButton*		fVPNConnectButton;
			BButton*		fVPNDisconnectButton;
};


#endif	// INTERFACE_DETAIL_VIEW_H
