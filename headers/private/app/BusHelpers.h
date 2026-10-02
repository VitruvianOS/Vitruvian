/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BUS_HELPERS_H
#define _BUS_HELPERS_H


#include <String.h>
#include <SupportDefs.h>


class BMessage;


namespace BPrivate {


// Thin sd-bus wrappers used by preferences apps and the User pref.
// All calls are synchronous. Return B_OK on success, a translated
// status_t on failure. On failure the optional error string receives
// the D-Bus error message (polkit denial text included) for the UI.


// systemd-timedated (org.freedesktop.timedate1).
status_t bus_timedate1_set_time(int64 usec_since_epoch, bool relative,
	BString* error = NULL);
status_t bus_timedate1_set_timezone(const char* tz, BString* error = NULL);
status_t bus_timedate1_set_ntp(bool enabled, BString* error = NULL);
status_t bus_timedate1_set_local_rtc(bool local, bool fix_system,
	bool interactive, BString* error = NULL);
status_t bus_timedate1_get_timezone(BString& outZone, BString* error = NULL);
status_t bus_timedate1_get_ntp(bool& outEnabled, BString* error = NULL);
status_t bus_timedate1_get_ntp_synchronized(bool& outSynced,
	BString* error = NULL);
status_t bus_timedate1_get_local_rtc(bool& outLocal, BString* error = NULL);


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
