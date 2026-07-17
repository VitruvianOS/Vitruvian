/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <BusHelpers.h>

#include <errno.h>
#include <stdio.h>

#include <systemd/sd-bus.h>

#include <Message.h>


namespace BPrivate {


static status_t
_translate_sd(int r)
{
	if (r >= 0)
		return B_OK;
	if (r == -ENOMEM)
		return B_NO_MEMORY;
	if (r == -EACCES || r == -EPERM)
		return B_PERMISSION_DENIED;
	if (r == -ENOENT)
		return B_ENTRY_NOT_FOUND;
	if (r == -ETIMEDOUT)
		return B_TIMED_OUT;
	return B_ERROR;
}


static void
_log(const char* what, sd_bus_error* err, int r)
{
	fprintf(stderr, "BusHelpers: %s failed: %s (%s)\n", what,
		err != NULL && err->message != NULL ? err->message : "no message",
		strerror(-r));
}


static status_t
_call_timedate1(const char* method, const char* signature, ...)
{
	sd_bus* bus = NULL;
	int r = sd_bus_open_system(&bus);
	if (r < 0)
		return _translate_sd(r);

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;

	va_list ap;
	va_start(ap, signature);
	// sd_bus_call_method is not va-friendly; we build a message manually.
	sd_bus_message* msg = NULL;
	r = sd_bus_message_new_method_call(bus, &msg,
		"org.freedesktop.timedate1",
		"/org/freedesktop/timedate1",
		"org.freedesktop.timedate1",
		method);
	if (r >= 0)
		r = sd_bus_message_appendv(msg, signature, ap);
	if (r >= 0)
		r = sd_bus_call(bus, msg, 0, &err, &reply);
	va_end(ap);

	if (r < 0)
		_log(method, &err, r);

	sd_bus_message_unref(msg);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	sd_bus_unref(bus);
	return _translate_sd(r);
}


status_t
bus_timedate1_set_time(int64 usec_since_epoch, bool relative)
{
	// SetTime(x usec, b relative, b user_interaction)
	return _call_timedate1("SetTime", "xbb",
		(int64_t)usec_since_epoch, (int)relative, 1);
}


status_t
bus_timedate1_set_timezone(const char* tz)
{
	if (tz == NULL || *tz == '\0')
		return B_BAD_VALUE;
	// SetTimezone(s tz, b user_interaction)
	return _call_timedate1("SetTimezone", "sb", tz, 1);
}


status_t
bus_timedate1_set_ntp(bool enabled)
{
	// SetNTP(b enabled, b user_interaction)
	return _call_timedate1("SetNTP", "bb", (int)enabled, 1);
}


status_t
bus_accounts_list_users(BMessage* outUsers)
{
	if (outUsers == NULL)
		return B_BAD_VALUE;

	sd_bus* bus = NULL;
	int r = sd_bus_open_system(&bus);
	if (r < 0)
		return _translate_sd(r);

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	r = sd_bus_call_method(bus,
		"org.freedesktop.Accounts",
		"/org/freedesktop/Accounts",
		"org.freedesktop.Accounts",
		"ListCachedUsers",
		&err, &reply, "");
	if (r < 0) {
		_log("ListCachedUsers", &err, r);
		sd_bus_error_free(&err);
		sd_bus_unref(bus);
		return _translate_sd(r);
	}

	r = sd_bus_message_enter_container(reply, 'a', "o");
	if (r >= 0) {
		const char* path = NULL;
		while ((r = sd_bus_message_read(reply, "o", &path)) > 0)
			outUsers->AddString("users", path);
		sd_bus_message_exit_container(reply);
	}

	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	sd_bus_unref(bus);
	return _translate_sd(r);
}


status_t
bus_accounts_user_property(const char* userObjectPath,
	const char* property, BMessage* outValue)
{
	if (userObjectPath == NULL || property == NULL || outValue == NULL)
		return B_BAD_VALUE;

	sd_bus* bus = NULL;
	int r = sd_bus_open_system(&bus);
	if (r < 0)
		return _translate_sd(r);

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	r = sd_bus_call_method(bus,
		"org.freedesktop.Accounts",
		userObjectPath,
		"org.freedesktop.DBus.Properties",
		"Get",
		&err, &reply, "ss",
		"org.freedesktop.Accounts.User", property);
	if (r < 0) {
		_log("Properties.Get", &err, r);
		sd_bus_error_free(&err);
		sd_bus_unref(bus);
		return _translate_sd(r);
	}

	// Variant: peek its contained type.
	const char* contents = NULL;
	r = sd_bus_message_peek_type(reply, NULL, &contents);
	if (r < 0) {
		sd_bus_message_unref(reply);
		sd_bus_error_free(&err);
		sd_bus_unref(bus);
		return _translate_sd(r);
	}

	r = sd_bus_message_enter_container(reply, 'v', contents);
	if (r >= 0 && contents != NULL) {
		switch (contents[0]) {
			case 's': {
				const char* s = NULL;
				if (sd_bus_message_read(reply, "s", &s) > 0 && s != NULL)
					outValue->AddString(property, s);
				break;
			}
			case 'b': {
				int b = 0;
				if (sd_bus_message_read(reply, "b", &b) > 0)
					outValue->AddBool(property, b != 0);
				break;
			}
			case 'x': {
				int64_t x = 0;
				if (sd_bus_message_read(reply, "x", &x) > 0)
					outValue->AddInt64(property, x);
				break;
			}
			case 't': {
				uint64_t t = 0;
				if (sd_bus_message_read(reply, "t", &t) > 0)
					outValue->AddInt64(property, (int64)t);
				break;
			}
			default:
				fprintf(stderr, "BusHelpers: unhandled variant type '%c' for "
					"property %s\n", contents[0], property);
				break;
		}
		sd_bus_message_exit_container(reply);
	}

	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	sd_bus_unref(bus);
	return _translate_sd(r);
}


}	// namespace BPrivate
