/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef NETWORK_WINDOW_H
#define NETWORK_WINDOW_H


#include <String.h>
#include <Window.h>


class BButton;
class BFilePanel;
class BListItem;
class BMessageRunner;
class BOutlineListView;
class InterfaceDetailView;
class MobileBroadbandView;
class ProxyView;


class NetworkWindow : public BWindow {
public:
	NetworkWindow();
	virtual ~NetworkWindow();

	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

private:
	void _RequestDeviceScan();
	void _PopulateDeviceList(BMessage* devices);
	BListItem* _PopulateVPNList(const BString& previousSelectionPath);
	void _SelectItem(BListItem* item);
	void _ShowPane(BView* pane);
	void _UpdateRevertButton();
	void _RevertSettings();
	void _ToggleReplicant();
	bool _IsReplicantInstalled();
	void _ImportVPNRequested();
	void _ImportVPNRefs(BMessage* message);
	void _ImportVPNResult(BMessage* message);

	BOutlineListView* fListView;
	InterfaceDetailView* fDetailView;
	MobileBroadbandView* fMobileView;
	ProxyView* fProxyView;
	BButton* fRevertButton;
	// Coalesces a scan's burst of AP add/remove notifications.
	BMessageRunner* fWiFiRefreshRunner;
	BFilePanel* fImportVPNPanel;
	// Selected once the list refresh after an import lands.
	BString fPendingVPNImportPath;

	BListItem* fProxyItem;
	BListItem* fMobileItem;
	BListItem* fServicesItem;
	BListItem* fDialUpItem;
	BListItem* fVPNItem;
	BListItem* fOtherItem;
	BListItem* fWiredItem;
	BListItem* fWirelessItem;
};


#endif // NETWORK_WINDOW_H
