/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_NOTIFICATIONS_BRIDGE_H
#define VOS_NOTIFICATIONS_BRIDGE_H


#include <functional>
#include <string>


class NotificationsBridge {
public:
						NotificationsBridge();
						~NotificationsBridge();

			void		RequestPermission();
			bool		HasPermission() const { return fPermissionGranted; }
			void		Show(const std::string& title, const std::string& body);

			std::function<void(const std::string& title,
				const std::string& body)> onNotification;

private:
			bool		fPermissionGranted;
};


#endif // VOS_NOTIFICATIONS_BRIDGE_H
