/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_NOTIFICATIONS_BRIDGE_H
#define VOS_NOTIFICATIONS_BRIDGE_H


#include <string>


// Delivers web notifications through the notification server.
// Permission is decided by the WebKit permission-request path in
// WebKitBridge; by the time Show runs the site is already allowed.
class NotificationsBridge {
public:
						NotificationsBridge();
						~NotificationsBridge();

			void		Show(const std::string& title,
							const std::string& body);
};


#endif // VOS_NOTIFICATIONS_BRIDGE_H
