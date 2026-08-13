/*
 * Copyright 2006-2009, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef NETWORK_STATUS_VIEW_H
#define NETWORK_STATUS_VIEW_H


#include <MenuItem.h>
#include <Messenger.h>
#include <MessageRunner.h>
#include <Notification.h>
#include <ObjectList.h>
#include <PopUpMenu.h>
#include <View.h>
#include <Window.h>

#include <map>

#include "SecretDialogWindow.h"


class BBitmap;


enum {
	kMsgUpdateStatus = 'upst',
	kMsgStatusReady = 'stdy',
	kMsgConnectDevice = 'cndv',
	kMsgDisconnectDevice = 'dscd',
	kMsgScanWiFi = 'scwf',
	kMsgConnectWiFi = 'cnwf',
	kMsgConnectVPN = 'cnvp',
	kMsgDisconnectVPN = 'dsvp',
	kMsgOpenNetworkPreferences = 'onwp',
	kMsgToggleNetworking = 'tgnw',
	kMsgToggleWireless = 'tgwl',
	kMsgConnectionInfo = 'cnin',

	// Single well-defined entry point for WiFi/802.1x secret dialog results.
	// The real SecretAgent.GetSecrets callback wires in here to open the
	// dialog and to whatever consumes this message; nothing else changes.
	kMsgSecretResult = 'nssr',

	// Mirrors NMBackend::{SECRET_REQUEST,SECRET_CANCEL} exactly --
	// NMBackend posts these directly to whatever BMessenger registered via
	// NMBackend::RegisterSecretAgentAsync().
	kMsgAgentRequest = 'NSAR',
	kMsgAgentCancel = 'NSAC',

	// Fire-and-forget ack for RegisterSecretAgentAsync(); nothing to do
	// with it besides letting the reply land somewhere instead of being
	// dropped by an unmatched BMessenger.
	kMsgAgentRegistered = 'nsgr',

	// Reply target for ConnectToWiFiAsync() -- "status" (status_t) and, on
	// failure, "reason" (BString).
	kMsgConnectWiFiResult = 'nscr'

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
	, kMsgDebugOpenSecretDialog = 'nsdd'
#endif
};


enum {
	kStatusUnknown = 0,
	kStatusNoLink,
	kStatusLinkNoConfig,
	kStatusConnecting,
	kStatusReady,

	kStatusCount
};


class NetworkStatusView : public BView {
	public:
		NetworkStatusView(BRect frame, int32 resizingMode,
			bool inDeskbar = false);
		NetworkStatusView(BMessage* archive);
		virtual	~NetworkStatusView();

		static	NetworkStatusView* Instantiate(BMessage* archive);
		virtual	status_t Archive(BMessage* archive, bool deep = true) const;

		virtual	void	AttachedToWindow();
		virtual	void	DetachedFromWindow();

		virtual	void	MessageReceived(BMessage* message);
		virtual void	FrameResized(float width, float height);
		virtual	void	MouseDown(BPoint where);
		virtual	void	Draw(BRect updateRect);

	private:
		void			_DrawNetworkIcon(BRect bounds);
		BBitmap*		_GetIcon(int32 iconID);
		void			_RequestStatusUpdate();
		void			_ApplyStatusUpdate(BMessage* devices);
		void			_ScanWiFiNetworks(const char* devicePath);
		void			_ShowMenu(BPoint where);
		void			_ShowOperationFailedAlert(const char* message);
		void			_BuildWiFiSection(BMenu* menu, const char* devicePath);
		void			_BuildVPNSection(BMenu* menu);
		void			_ConnectionInfoRequested();
		void			_AboutRequested();
		void			_Quit();
	static	int32			_RemoveFromDeskbarThread(void* data);
		void			_OpenSecretDialog(secret_dialog_kind kind,
							const BMessage& request);
		void			_HandleSecretResult(BMessage* message);

#ifdef VITRUVIAN_DIALOG_SHELL_DEBUG
		void			_BuildDebugDialogMenu(BMenu* parent);
#endif

		int32			fDeviceIndex;
		bool			fConnected;
		bool			fHasDevice;
		int				fSignalStrength;
		BString			fDevicePath;
		BString			fDeviceType;
		bool			fInDeskbar;
		BMessage		fDevices;		// last snapshot from GetDevicesAsync
		BBitmap*		fIcons[5];		// lazily loaded, indexed by iconID - 1000

		// At most one live secret dialog -- opening another cancels
		// this one first, same pattern as the Bluetooth pairing dialog.
		BMessenger		fSecretDialogTarget;
};

#endif	// NETWORK_STATUS_VIEW_H