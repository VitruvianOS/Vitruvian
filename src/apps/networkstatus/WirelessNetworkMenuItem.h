/*
 * Copyright 2010, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef WIRELESS_NETWORK_MENU_ITEM_H
#define WIRELESS_NETWORK_MENU_ITEM_H


#include <MenuItem.h>
#include <String.h>


class WirelessNetworkMenuItem : public BMenuItem {
public:
								WirelessNetworkMenuItem(const char* ssid,
									int32 signalStrength,
									const char* security, bool isConnected,
									BMessage* message);
	virtual						~WirelessNetworkMenuItem();

			const char*			SSID() const { return fSSID.String(); }
			int32				SignalStrength() const
									{ return fSignalStrength; }
			const char*			Security() const
									{ return fSecurity.String(); }
			bool				IsConnected() const { return fIsConnected; }

	static	int					CompareSignalStrength(const BMenuItem* a,
									const BMenuItem* b);

protected:
	virtual	void				DrawContent();
	virtual	void				GetContentSize(float* width, float* height);
			void				DrawRadioIcon();

private:
			BString				fSSID;
			int32				fSignalStrength;	// 0-100
			BString				fSecurity;			// "", "WEP", "WPA", ...
			bool				fIsConnected;
};


#endif	// WIRELESS_NETWORK_MENU_ITEM_H
