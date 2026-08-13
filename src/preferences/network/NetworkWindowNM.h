/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef NETWORK_WINDOW_NM_H
#define NETWORK_WINDOW_NM_H


#include <String.h>
#include <Window.h>


class BButton;
class BListItem;
class BOutlineListView;
class InterfaceDetailView;


class NetworkWindowNM : public BWindow {
public:
	NetworkWindowNM();
	virtual ~NetworkWindowNM();

	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

private:
	void _RequestDeviceScan();
	void _PopulateDeviceList(BMessage* devices);
	BListItem* _PopulateVPNList(const BString& previousSelectionPath);
	void _SelectItem(BListItem* item);
	void _UpdateRevertButton();
	void _RevertSettings();
	void _ToggleReplicant();
	bool _IsReplicantInstalled();

	BOutlineListView* fListView;
	InterfaceDetailView* fDetailView;
	BButton* fRevertButton;

	BListItem* fServicesItem;
	BListItem* fDialUpItem;
	BListItem* fVPNItem;
	BListItem* fOtherItem;
	BListItem* fWiredItem;
	BListItem* fWirelessItem;
};


#endif // NETWORK_WINDOW_NM_H
