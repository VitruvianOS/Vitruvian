/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef JOIN_WIFI_WINDOW_H
#define JOIN_WIFI_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BButton;
class BCheckBox;
class BMenuField;
class BPopUpMenu;
class BTextControl;


// Reply: "device", "ssid", "password", "security", "remember".
const uint32 kMsgJoinHiddenWiFi = 'jhwf';


// Joins a network by name, for networks that do not broadcast it.
class JoinWiFiWindow : public BWindow {
public:
								JoinWiFiWindow(const BMessenger& target,
									const BMessage& adapters,
									const char* selectedDevice);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_UpdateControls();
			const char*			_Security() const;
			const char*			_Device() const;

			BMessenger			fTarget;
			BTextControl*		fSSID;
			BPopUpMenu*			fSecurityMenu;
			BTextControl*		fPassword;
			BPopUpMenu*			fAdapterMenu;
			BString				fDevice;
			BCheckBox*			fRemember;
			BButton*			fConnect;
};


#endif	// JOIN_WIFI_WINDOW_H
