/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef BLUETOOTH_STATUS_VIEW_H
#define BLUETOOTH_STATUS_VIEW_H


#include <Messenger.h>
#include <View.h>
#include <Message.h>
#include <String.h>

#include <map>

#include "PairingDialogWindow.h"


class BBitmap;
class BMenu;
class BMessageRunner;


enum {
	kMsgUpdateStatus = 'btup',
	kMsgConnectDevice = 'btcn',
	kMsgDisconnectDevice = 'btdc',
	kMsgScanDevices = 'btsc',
	kMsgEnableAdapter = 'ENAB',
	kMsgDisableAdapter = 'DISB',
	kMsgOpenBluetoothPreferences = 'obtp',
	kMsgStatusReady = 'btsr',
	kMsgScanReady = 'btcr',
	kMsgOperationDone = 'btod',

	// Single well-defined entry point for pairing dialog results. The real
	// Agent1 callbacks wire in here to open the dialog and to whatever
	// consumes this message; nothing else about the dialogs changes.
	kMsgPairingResult = 'btpr',

	// Mirrors Bluetooth::LocalDevice::kAgentRequest/kAgentCancel exactly --
	// BlueZBackend posts these directly to whatever BMessenger registered
	// via LocalDevice::RegisterAgent().
	kMsgAgentRequest = 'BTAR',
	kMsgAgentCancel = 'BTAC'

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
	, kMsgDebugOpenPairingDialog = 'btdd'
#endif
};


enum {
	kStatusUnknown = 0,
	kStatusNoAdapter,
	kStatusDisabled,
	kStatusEnabled,
	kStatusConnected,

	kStatusCount
};


class BluetoothStatusView : public BView {
public:
	BluetoothStatusView(BRect frame, int32 resizingMode,
		bool inDeskbar = false);
	BluetoothStatusView(BMessage* archive);
	virtual	~BluetoothStatusView();

	static	BluetoothStatusView* Instantiate(BMessage* archive);
	virtual	status_t Archive(BMessage* archive, bool deep = true) const;

	virtual	void	AttachedToWindow();
	virtual	void	DetachedFromWindow();

	virtual	void	MessageReceived(BMessage* message);
	virtual void	FrameResized(float width, float height);
	virtual	void	MouseDown(BPoint where);
	virtual	void	Draw(BRect updateRect);

private:
	void			_DrawBluetoothIcon(BRect bounds);
	BBitmap*		_GetTrayIcon();
	void			_DrawIconWithAlpha(BBitmap* icon, BRect bounds,
						uint8 alpha);
	void			_RequestStatusUpdate();
	void			_ApplyStatusUpdate(BMessage* status);
	void			_ScanDevices();
	void			_ShowMenu(BPoint where);
	void			_AboutRequested();
	void			_Quit();
	static	int32	_RemoveFromDeskbarThread(void* data);
	void			_SetAdapterPowered(bool powered);
	void			_OpenBluetoothPreferences();
	void			_OpenPairingDialog(pairing_dialog_kind kind,
						const BMessage& request);
	void			_HandlePairingResult(BMessage* message);
	BString			_CachedDeviceName(const char* devicePath);

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
	void			_BuildDebugDialogMenu(BMenu* parent);
#endif

	int32			fAdapterCount;
	bool			fPowered;
	bool			fConnected;
	bool			fDiscovering;
	bool			fPulsePhase;
	BString			fAdapterPath;
	BString			fConnectedDevice;
	bool			fInDeskbar;
	BBitmap*		fTrayIcon;

	// At most one live pairing dialog -- opening another cancels this
	// one first. A BMessenger survives the target window quitting (SendMessage
	// then just fails), unlike holding the BWindow* directly.
	BMessenger		fPairingDialogTarget;

	// Populated from each status poll's "devices" snapshot (path -> alias/
	// name), so an incoming Agent1 request can resolve a friendly name
	// without a synchronous D-Bus round trip from the dispatch thread.
	std::map<BString, BString> fDeviceNames;
};


#endif	// BLUETOOTH_STATUS_VIEW_H