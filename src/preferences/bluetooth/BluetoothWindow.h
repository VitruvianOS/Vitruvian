/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef BLUETOOTH_WINDOW_H
#define BLUETOOTH_WINDOW_H


#include <String.h>
#include <Window.h>

#include "BluetoothSettings.h"


class BButton;
class BCardLayout;
class BCheckBox;
class BListView;
class BOutlineListView;
class BStringView;
class BTabView;
class BTextControl;
class BluetoothSettingsView;


class BluetoothWindow : public BWindow {
public:
	BluetoothWindow();
	virtual ~BluetoothWindow();

	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

private:
	void _RequestStatusUpdate();
	void _ApplyStatusUpdate(BMessage* message);
	void _ApplyPropertyChanged(BMessage* message);
	void _ShowNoAdapterState();
	void _ShowLoadingState();

	void _RebuildDeviceList(BMessage* devicesReply);
	class DeviceListItem* _SelectedDevice();

	void _ToggleReplicant();
	bool _IsReplicantInstalled();
	// Runs the HasItem()/AddItem()/RemoveItem() round trips off the window
	// thread -- BDeskbar's calls target Deskbar's own team and can block
	// on a full replicant instantiation; doing that inline froze this
	// window on rapid clicks. Posts kMsgReplicantToggled back with the
	// outcome; fReplicantOpPending blocks a second click from stacking a
	// concurrent op while one is in flight.
	static int32 _ToggleReplicantThread(void* data);
	void _ApplyReplicantToggled(BMessage* message);
	bool fReplicantOpPending;

	void _SetPowered(bool powered);
	void _SetDiscoverable(bool discoverable);

	void _DoAdd();
	void _DoRemove();
	void _DoPair();
	void _DoDisconnect();
	void _DoToggleTrust();
	void _DoToggleBlock();

	void _UpdateButtons();

	BCheckBox* fPoweredCheckBox;
	BCheckBox* fDiscoverableCheckBox;
	BStringView* fAdapterNameView;
	BStringView* fAdapterAddressView;

	BOutlineListView* fDeviceList;
	class BListItem* fPairedHeader;
	class BListItem* fAvailableHeader;
	BStringView* fEmptyStateView;
	class BScrollView* fScrollView;
	class BCardLayout* fContentCards;

	BButton* fAddButton;
	BButton* fRemoveButton;
	BButton* fPairButton;
	BButton* fDisconnectButton;
	BButton* fTrustButton;
	BButton* fBlockButton;
	BButton* fRefreshButton;

	BCheckBox* fShowReplicantCheckBox;

	BString fAdapterPath;
	bool fHasAdapter;
	bool fAdapterPowered;

	// Shared with fSettingsView -- see BluetoothSettingsView.h's constructor
	// comment for why it isn't a private copy on each side.
	BluetoothSettings fSettings;
	BTabView* fTabView;
	BluetoothSettingsView* fSettingsView;
};


#endif // BLUETOOTH_WINDOW_H
