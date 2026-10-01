/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "notifications_bridge.h"

#include "logging.h"

#include <Alert.h>
#include <Notification.h>


//	#pragma mark - Constructor/Destructor


NotificationsBridge::NotificationsBridge()
	:
	fPermissionGranted(false)
{
}


NotificationsBridge::~NotificationsBridge()
{
}


//	#pragma mark - Public


void
NotificationsBridge::RequestPermission()
{
	// Show a real permission prompt instead of auto-granting.
	BAlert* alert = new BAlert("Web Notification Permission",
		"This page wants to show notifications.\n"
		"Allow notifications from this site?",
		"Deny", "Allow", nullptr,
		B_WIDTH_AS_USUAL, B_INFO_ALERT);

	int32 result = alert->Go(nullptr);
	fPermissionGranted = (result == 1);
	log_info("NotificationsBridge: permission %s",
		fPermissionGranted ? "granted" : "denied");
}


void
NotificationsBridge::Show(const std::string& title, const std::string& body)
{
	if (!fPermissionGranted) {
		log_info("NotificationsBridge::Show: permission not granted");
		return;
	}

	BNotification notif(B_INFORMATION_NOTIFICATION);
	notif.SetGroup("Net+");
	notif.SetTitle(title.c_str());
	notif.SetContent(body.c_str());
	status_t err = notif.Send();
	if (err != B_OK)
		log_error("NotificationsBridge: BNotification::Send failed (%s)",
			strerror(err));

	if (onNotification)
		onNotification(title, body);
}
