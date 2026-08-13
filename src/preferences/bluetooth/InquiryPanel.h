/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef INQUIRY_PANEL_H
#define INQUIRY_PANEL_H


#include <String.h>
#include <Window.h>

namespace Bluetooth {
	class DiscoveryAgent;
	class DiscoveryListener;
}

class BButton;
class BListView;
class BScrollView;
class BStatusBar;
class BStringView;


// Discovery dialog: "Add..." from BluetoothWindow. Drives BlueZ discovery
// through DiscoveryAgent/DiscoveryListener (never polls); found devices
// appear live. Selecting one and pressing Pair sends the pairing request --
// which fails until the Agent1 pairing agent is registered, and this dialog
// shows that failure honestly rather than pretending success.
class InquiryPanel : public BWindow {
public:
	// inquirySeconds: client-side scan duration -- see DiscoveryAgent.h,
	// BlueZ's StartDiscovery has no daemon-side timeout of its own. Defaults
	// to DiscoveryAgent's own default when the caller has no preference.
	InquiryPanel(const BString& adapterPath, const BString& adapterName,
		bigtime_t inquirySeconds = 0);
	virtual ~InquiryPanel();

	virtual void MessageReceived(BMessage* message);
	virtual bool QuitRequested();

	// Called by the internal DiscoveryListener subclass, always on the
	// listener's own BLooper thread -- forwarded here as a BMessage so all
	// BListView access still happens on this window's thread.
	void DeviceFound(BMessage* deviceInfo);
	void InquiryStarted();
	void InquiryFinished();

private:
	void _StartInquiry();
	void _StopInquiry();
	void _UpdateButtons();

	BStringView* fMessageView;
	BStatusBar* fProgressBar;
	BListView* fDeviceList;
	BScrollView* fScrollView;
	BButton* fPairButton;
	BButton* fRescanButton;

	BString fAdapterPath;
	Bluetooth::DiscoveryAgent* fDiscoveryAgent;
	Bluetooth::DiscoveryListener* fDiscoveryListener;
	bigtime_t fInquirySeconds;
	bool fScanning;
};


#endif // INQUIRY_PANEL_H
