/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CONNECTION_INFO_WINDOW_H
#define CONNECTION_INFO_WINDOW_H


#include <String.h>
#include <Window.h>


class BGroupLayout;
class BView;


class ConnectionInfoWindow : public BWindow {
public:
							ConnectionInfoWindow(const char* devicePath);
	virtual					~ConnectionInfoWindow();

	virtual	void			MessageReceived(BMessage* message);

	static const uint32		kMsgDeviceInfoReply = 'cidr';

private:
			void			_BuildLoadingLayout();
			void			_PopulateFrom(const BMessage& deviceInfo);

			BGroupLayout*	fRootLayout;
			BView*			fInfoView;
			BString			fDevicePath;
};


#endif	// CONNECTION_INFO_WINDOW_H
