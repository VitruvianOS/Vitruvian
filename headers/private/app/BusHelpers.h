/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BUS_HELPERS_H
#define _BUS_HELPERS_H


#include <SupportDefs.h>


class BMessage;


namespace BPrivate {


// Thin sd-bus wrappers used by preferences apps and the User pref.
// All calls are synchronous. Return B_OK on success, a translated
// status_t on failure. Non-zero D-Bus errors are logged via fprintf
// to stderr at call site; callers don't need to unwrap sd_bus_error.


// systemd-timedated (org.freedesktop.timedate1).
status_t bus_timedate1_set_time(int64 usec_since_epoch, bool relative);
status_t bus_timedate1_set_timezone(const char* tz);
status_t bus_timedate1_set_ntp(bool enabled);


// AccountsService (org.freedesktop.Accounts).
// bus_accounts_list_users returns a BMessage with a "users" string
// array field: each entry is the D-Bus object path
// ("/org/freedesktop/Accounts/User<uid>"). Caller owns the message.
status_t bus_accounts_list_users(BMessage* outUsers);

// Read a single property from an accounts user object.
// property is one of: "UserName", "RealName", "IconFile",
// "AutomaticLogin", "SystemAccount", "Locked". Output is written to
// outValue as string / bool depending on property type; caller
// inspects the returned kind via BMessage::GetInfo.
status_t bus_accounts_user_property(const char* userObjectPath,
	const char* property, BMessage* outValue);


}	// namespace BPrivate


#endif	// _BUS_HELPERS_H
