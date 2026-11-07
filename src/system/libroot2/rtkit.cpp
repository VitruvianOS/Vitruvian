/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "rtkit.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>


namespace BKernelPrivate {


namespace {


// Local mirrors of the sd-bus entry points, resolved at run time.
struct sd_bus;
struct sd_bus_message;
struct sd_bus_error {
	const char* name;
	const char* message;
	int need_free;
};

typedef int (*open_system_fn)(sd_bus**);
typedef sd_bus* (*unref_fn)(sd_bus*);
typedef int (*call_method_fn)(sd_bus*, const char*, const char*, const char*,
	const char*, sd_bus_error*, sd_bus_message**, const char*, ...);
typedef int (*get_property_fn)(sd_bus*, const char*, const char*, const char*,
	const char*, sd_bus_error*, char, void*);
typedef void (*error_free_fn)(sd_bus_error*);
typedef sd_bus_message* (*message_unref_fn)(sd_bus_message*);

const char* const kDest = "org.freedesktop.RealtimeKit1";
const char* const kPath = "/org/freedesktop/RealtimeKit1";
const char* const kIface = "org.freedesktop.RealtimeKit1";

pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
bool sUnavailable = false;
pid_t sBusPid = 0;
sd_bus* sBus = NULL;
int sMaxRealtime = 0;
int sMinNice = 0;
long long sRTTimeMax = 200000;

open_system_fn sOpen;
unref_fn sUnref;
call_method_fn sCall;
get_property_fn sGetProperty;
error_free_fn sErrorFree;
message_unref_fn sMessageUnref;


// Called with sLock held. The bus is per process: after fork() the child
// drops the inherited handle and reconnects.
bool
Connect()
{
	if (sUnavailable)
		return false;

	pid_t self = getpid();
	if (sBus != NULL && sBusPid == self)
		return true;

	sBus = NULL;

	if (sOpen == NULL) {
		void* lib = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
		if (lib != NULL) {
			sOpen = (open_system_fn)dlsym(lib, "sd_bus_open_system");
			sUnref = (unref_fn)dlsym(lib, "sd_bus_unref");
			sCall = (call_method_fn)dlsym(lib, "sd_bus_call_method");
			sGetProperty
				= (get_property_fn)dlsym(lib, "sd_bus_get_property_trivial");
			sErrorFree = (error_free_fn)dlsym(lib, "sd_bus_error_free");
			sMessageUnref
				= (message_unref_fn)dlsym(lib, "sd_bus_message_unref");
		}
		if (sOpen == NULL || sUnref == NULL || sCall == NULL
			|| sGetProperty == NULL || sErrorFree == NULL
			|| sMessageUnref == NULL) {
			sUnavailable = true;
			return false;
		}
	}

	sd_bus* bus = NULL;
	if (sOpen(&bus) < 0 || bus == NULL) {
		sUnavailable = true;
		return false;
	}

	sd_bus_error error = { NULL, NULL, 0 };
	int32_t maxRealtime = 0;
	int32_t minNice = 0;
	int64_t rtTime = 0;
	if (sGetProperty(bus, kDest, kPath, kIface, "MaxRealtimePriority", &error,
			'i', &maxRealtime) < 0
		|| sGetProperty(bus, kDest, kPath, kIface, "MinNiceLevel", &error, 'i',
			&minNice) < 0
		|| sGetProperty(bus, kDest, kPath, kIface, "RTTimeUSecMax", &error,
			'x', &rtTime) < 0) {
		sErrorFree(&error);
		sUnref(bus);
		sUnavailable = true;
		return false;
	}

	sMaxRealtime = maxRealtime;
	sMinNice = minNice;
	if (rtTime > 0)
		sRTTimeMax = rtTime;
	sBus = bus;
	sBusPid = self;
	return true;
}


int
Call(const char* method, const char* types, uint64_t tid, int64_t value)
{
	sd_bus_error error = { NULL, NULL, 0 };
	sd_bus_message* reply = NULL;
	int result;
	if (types[1] == 'u')
		result = sCall(sBus, kDest, kPath, kIface, method, &error, &reply,
			types, tid, (uint32_t)value);
	else
		result = sCall(sBus, kDest, kPath, kIface, method, &error, &reply,
			types, tid, (int32_t)value);
	if (reply != NULL)
		sMessageUnref(reply);
	sErrorFree(&error);
	return result < 0 ? -EPERM : 0;
}


}


int
RtkitMakeRealtime(pid_t tid, int priority, int* granted)
{
	pthread_mutex_lock(&sLock);
	int result = -ENOTSUP;
	if (Connect()) {
		int value = priority < sMaxRealtime ? priority : sMaxRealtime;
		if (value < 1)
			value = 1;
		result = Call("MakeThreadRealtime", "tu", (uint64_t)tid, value);
		if (result == 0 && granted != NULL)
			*granted = value;
	}
	pthread_mutex_unlock(&sLock);
	return result;
}


int
RtkitMakeHighPriority(pid_t tid, int nice, int* granted)
{
	pthread_mutex_lock(&sLock);
	int result = -ENOTSUP;
	if (Connect()) {
		int value = nice > sMinNice ? nice : sMinNice;
		result = Call("MakeThreadHighPriority", "ti", (uint64_t)tid, value);
		if (result == 0 && granted != NULL)
			*granted = value;
	}
	pthread_mutex_unlock(&sLock);
	return result;
}


long long
RtkitRTTimeMax()
{
	pthread_mutex_lock(&sLock);
	Connect();
	long long value = sRTTimeMax;
	pthread_mutex_unlock(&sLock);
	return value;
}


}
