/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "notifications_bridge.h"

#include "logging.h"

#include <Notification.h>


//	#pragma mark - Constructor/Destructor


NotificationsBridge::NotificationsBridge()
{
}


NotificationsBridge::~NotificationsBridge()
{
}


//	#pragma mark - Public


void
NotificationsBridge::Show(const std::string& title, const std::string& body)
{
	BNotification notif(B_INFORMATION_NOTIFICATION);
	notif.SetGroup("NetPlus");
	notif.SetTitle(title.c_str());
	notif.SetContent(body.c_str());
	status_t err = notif.Send();
	if (err != B_OK)
		log_error("NotificationsBridge: BNotification::Send failed (%s)",
			strerror(err));
}
