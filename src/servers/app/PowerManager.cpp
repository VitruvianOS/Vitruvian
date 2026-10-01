/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PowerManager.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <systemd/sd-bus.h>

#include <Accelerant.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

#include "Desktop.h"
#include "EventDispatcher.h"
#include "HWInterface.h"


static const bigtime_t kCheckInterval = 2000000;
// logind counts IdleActionSec from when the hint goes idle; the Power
// preferences subtract this from the time they write.
static const bigtime_t kIdleHintAfter = 30000000;
static const char* kSettingsFile = "Power settings";
static const char* kDisplayOffKey = "power:display_off_minutes";
// Same default as the Power preferences.
static const int32 kDefaultDisplayOff = 10;


PowerManager::PowerManager(Desktop* desktop)
	:
	fDesktop(desktop),
	fThread(-1),
	fQuit(0),
	fDisplayOff(0),
	fSettingsTime(-1),
	fIdleHint(false),
	fBus(NULL)
{
}


PowerManager::~PowerManager()
{
	if (fThread >= 0) {
		atomic_set(&fQuit, 1);
		status_t result;
		wait_for_thread(fThread, &result);
	}
	if (fBus != NULL)
		sd_bus_flush_close_unref(fBus);
}


status_t
PowerManager::Start()
{
	fThread = spawn_thread(_ThreadEntry, "power manager", B_LOW_PRIORITY,
		this);
	if (fThread < 0)
		return fThread;
	return resume_thread(fThread);
}


status_t
PowerManager::_ThreadEntry(void* self)
{
	((PowerManager*)self)->_Run();
	return B_OK;
}


void
PowerManager::_Run()
{
	while (atomic_get(&fQuit) == 0) {
		snooze(kCheckInterval);
		_LoadSettings();

		::EventDispatcher& dispatcher = fDesktop->EventDispatcher();
		bigtime_t idle = dispatcher.IdleTime();
		if (fDisplayOff > 0 && idle >= fDisplayOff
			&& !dispatcher.IsDisplayAsleep()) {
			// Asleep even when the driver refused, so a display without
			// DPMS is not asked again every interval; input clears it.
			dispatcher.SetDisplayAsleep(true);
			fDesktop->HWInterface()->SetDPMSMode(B_DPMS_OFF);
		}

		bool idleHint = idle >= kIdleHintAfter;
		if (idleHint != fIdleHint)
			_SetIdleHint(idleHint);
	}
}


void
PowerManager::_LoadSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK
		|| path.Append(kSettingsFile) != B_OK)
		return;

	struct stat st;
	if (stat(path.Path(), &st) != 0) {
		fDisplayOff = (bigtime_t)kDefaultDisplayOff * 60000000;
		fSettingsTime = -1;
		return;
	}
	if (st.st_mtime == fSettingsTime)
		return;
	fSettingsTime = st.st_mtime;

	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;
	if (file.InitCheck() != B_OK || settings.Unflatten(&file) != B_OK)
		return;
	fDisplayOff = (bigtime_t)settings.GetInt32(kDisplayOffKey,
		kDefaultDisplayOff) * 60000000;
}


void
PowerManager::_SetIdleHint(bool idle)
{
	if (fBus == NULL && sd_bus_open_system(&fBus) < 0) {
		fBus = NULL;
		return;
	}

	// "auto" is the caller's own session; app_server runs inside it.
	sd_bus_error error = SD_BUS_ERROR_NULL;
	int r = sd_bus_call_method(fBus, "org.freedesktop.login1",
		"/org/freedesktop/login1/session/auto",
		"org.freedesktop.login1.Session", "SetIdleHint", &error, NULL, "b",
		idle ? 1 : 0);
	if (r < 0) {
		fprintf(stderr, "app_server: SetIdleHint(%d): %s\n", idle,
			error.message != NULL ? error.message : strerror(-r));
		sd_bus_flush_close_unref(fBus);
		fBus = NULL;
	} else
		fIdleHint = idle;
	sd_bus_error_free(&error);
}
