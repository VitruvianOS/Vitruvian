/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef INTERFACE_DETAIL_VIEW_H
#define INTERFACE_DETAIL_VIEW_H


#include <Message.h>
#include <String.h>
#include <View.h>

#include "NetworkHotspotSettings.h"


class BGridLayout;
class BGroupLayout;
class BGroupView;
class BTabView;
class BListView;
class BButton;
class StaticIPView;


// Sent to the window by the VPN section's Import button.
const uint32 kMsgImportVPN = 'imvp';


// Right-hand detail pane of the Network preflet, swapped in when a device or
// VPN row is selected in the outline list.
// Wired/WiFi device fields are live (GetDeviceInfo() + sysfs statistics +
// BNetworkInterface + ScanWiFiNetworks()); VPN mode shows the NM connection
// snapshot handed in by SetToVPN().
//
// Owns the embedded StaticIPView so its Apply/Revert dirty state
// can be surfaced to NetworkWindow's single Revert button -- a
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
			// Overview of every VPN profile, with an Import button.
			void			ShowVPNSection(const BMessage& vpns);
			// Refill only the AP list from the cached scan, keeping the
			// selection; no-op unless showing that Wi-Fi device.
			void			RefreshWiFiNetworks(const char* devicePath);

			bool			IsRevertable() const;
			void			Revert();

private:
			void			_Rebuild();
			void			_RebuildDeviceView();
			void			_RebuildVPNView();
			void			_RebuildVPNSectionView();
			void			_AddHotspotSection(BGroupLayout* layout);
			void			_FillWiFiList(const BMessage& networks);
			void			_RequestHotspotState();
			void			_ShowHotspotError(BMessage* message);
			void			_StartHotspot(const BString& ssid,
							const BString& password);
			void			_StopHotspot();
			void			_UpdateWiFiButtons();
			void			_RequestSavedNetworks();
			void			_RebuildSavedList();
			void			_UpdateSavedButtons();
			void			_UpdateWiFiSavedMarkers();
			bool			_HasSavedProfile(const BString& ssid) const;
			void			_RenumberSavedList();
			BGroupLayout*	_AddTab(const char* label);
			void			_RebuildHotspotTab();

			enum Mode {
				MODE_EMPTY,
				MODE_DEVICE,
				MODE_VPN,
				MODE_VPN_SECTION
			};

			BGridLayout*	fGridLayout;
			BTabView*		fTabView;
			int32			fSelectedTab;
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
			BButton*		fVPNRemoveButton;

			// Hotspot (WiFi AP mode) controls and last known backend state.
			NetworkHotspotSettings	fHotspotSettings;
			BMessage		fHotspotState;
			BButton*		fHotspotStartButton;
			BButton*		fHotspotStopButton;
			BGroupView*		fHotspotPage;
};


#endif	// INTERFACE_DETAIL_VIEW_H
