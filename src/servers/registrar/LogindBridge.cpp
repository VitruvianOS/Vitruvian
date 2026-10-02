/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LogindBridge.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>

#include <Message.h>


static const char* kLogin1Bus       = "org.freedesktop.login1";
static const char* kLogin1Path      = "/org/freedesktop/login1";
static const char* kLogin1Manager   = "org.freedesktop.login1.Manager";
static const char* kLogin1Session   = "org.freedesktop.login1.Session";


static void
session_path_for_pid(char* out, size_t outSize)
{
	out[0] = '\0';
	char* sid = NULL;
	int r = sd_pid_get_session(0, &sid);
	if (r < 0 || sid == NULL)
		return;
	// sid is "c2" style; logind path is /org/freedesktop/login1/session/c2
	snprintf(out, outSize, "/org/freedesktop/login1/session/%s", sid);
	free(sid);
}

enum sleep_request {
	kSleepRequestNone = 0,
	kSleepRequestSuspend,
	kSleepRequestHibernate,
};


LogindBridge::LogindBridge(const BMessenger& target)
	:
	fTarget(target),
	fBus(NULL),
	fShutdownFd(-1),
	fSleepFd(-1),
	fThread(-1),
	fRunning(false),
	fPendingSleep(kSleepRequestNone),
	fSleepCheckTime(0),
	fCanSuspend(false),
	fCanHibernate(false)
{
}


LogindBridge::~LogindBridge()
{
	Stop();
	if (fShutdownFd >= 0)
		close(fShutdownFd);
	if (fSleepFd >= 0)
		close(fSleepFd);
	if (fBus != NULL)
		sd_bus_unref((sd_bus*)fBus);
}


status_t
LogindBridge::Start()
{
	sd_bus* bus = NULL;
	int r = sd_bus_open_system(&bus);
	if (r < 0) {
		fprintf(stderr, "LogindBridge: sd_bus_open_system: %s\n", strerror(-r));
		return B_ERROR;
	}
	fBus = bus;

	if (_AcquireShutdownInhibit() != B_OK
			|| _AcquireSleepInhibit() != B_OK) {
		fprintf(stderr, "LogindBridge: failed to acquire inhibit locks\n");
		// Non-fatal — carry on watching signals without inhibitors.
	}

	r = sd_bus_match_signal(bus, NULL, kLogin1Bus, kLogin1Path, kLogin1Manager,
		"PrepareForShutdown", NULL, this);
	if (r < 0)
		fprintf(stderr, "LogindBridge: match PrepareForShutdown: %s\n", strerror(-r));
	// systemd >= v257: carries type= "reboot"/"poweroff"/... . Emitted
	// before PrepareForShutdown, so a match here wins the race.
	r = sd_bus_match_signal(bus, NULL, kLogin1Bus, kLogin1Path, kLogin1Manager,
		"PrepareForShutdownWithMetadata", NULL, this);
	if (r < 0)
		fprintf(stderr, "LogindBridge: match PrepareForShutdownWithMetadata: %s\n",
			strerror(-r));
	r = sd_bus_match_signal(bus, NULL, kLogin1Bus, kLogin1Path, kLogin1Manager,
		"PrepareForSleep", NULL, this);
	if (r < 0)
		fprintf(stderr, "LogindBridge: match PrepareForSleep: %s\n", strerror(-r));

	// Session Lock/Unlock (loginctl lock-session). Session path may be
	// unknown early; match failure is non-fatal.
	char sessionPath[256];
	session_path_for_pid(sessionPath, sizeof(sessionPath));
	if (sessionPath[0] != '\0') {
		r = sd_bus_match_signal(bus, NULL, kLogin1Bus, sessionPath,
			kLogin1Session, "Lock", NULL, this);
		if (r < 0)
			fprintf(stderr, "LogindBridge: match session Lock: %s\n",
				strerror(-r));
		r = sd_bus_match_signal(bus, NULL, kLogin1Bus, sessionPath,
			kLogin1Session, "Unlock", NULL, this);
		if (r < 0)
			fprintf(stderr, "LogindBridge: match session Unlock: %s\n",
				strerror(-r));
	} else {
		fprintf(stderr, "LogindBridge: no logind session id yet; "
			"session Lock/Unlock not subscribed\n");
	}

	fRunning = true;
	fThread = spawn_thread(_ThreadEntry, "logind_bridge", B_NORMAL_PRIORITY, this);
	if (fThread < 0) {
		fRunning = false;
		return B_ERROR;
	}
	resume_thread(fThread);
	return B_OK;
}


void
LogindBridge::Stop()
{
	if (!fRunning)
		return;
	fRunning = false;
	if (fThread >= 0) {
		status_t exit;
		wait_for_thread(fThread, &exit);
		fThread = -1;
	}
}


status_t
LogindBridge::_AcquireShutdownInhibit()
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	int fd = -1;
	int r = sd_bus_call_method((sd_bus*)fBus, kLogin1Bus, kLogin1Path,
		kLogin1Manager, "Inhibit", &err, &reply, "ssss",
		"shutdown", "Vitruvian", "Run BeAPI quit dance", "delay");
	if (r >= 0)
		r = sd_bus_message_read(reply, "h", &fd);
	if (r >= 0)
		fShutdownFd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	return fShutdownFd >= 0 ? B_OK : B_ERROR;
}


status_t
LogindBridge::_AcquireSleepInhibit()
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	int fd = -1;
	int r = sd_bus_call_method((sd_bus*)fBus, kLogin1Bus, kLogin1Path,
		kLogin1Manager, "Inhibit", &err, &reply, "ssss",
		"sleep", "Vitruvian", "Notify apps of suspend", "delay");
	if (r >= 0)
		r = sd_bus_message_read(reply, "h", &fd);
	if (r >= 0)
		fSleepFd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	return fSleepFd >= 0 ? B_OK : B_ERROR;
}


void
LogindBridge::ReleaseShutdownInhibit()
{
	if (fShutdownFd >= 0) {
		close(fShutdownFd);
		fShutdownFd = -1;
	}
}


void
LogindBridge::ReleaseSleepInhibit()
{
	if (fSleepFd >= 0) {
		close(fSleepFd);
		fSleepFd = -1;
	}
}


bool
LogindBridge::CanSuspend()
{
	_UpdateSleepAvailability();
	return fCanSuspend;
}


bool
LogindBridge::CanHibernate()
{
	_UpdateSleepAvailability();
	return fCanHibernate;
}


status_t
LogindBridge::Suspend()
{
	return _RequestSleep("Suspend");
}


status_t
LogindBridge::Hibernate()
{
	return _RequestSleep("Hibernate");
}


// logind only checks for swap. Without a resume device the initramfs
// ignores the image and the hibernated session is lost at the next boot.
static bool
resume_configured()
{
	FILE* file = fopen("/sys/power/resume", "r");
	if (file == NULL)
		return false;
	char device[32] = "";
	bool configured = fgets(device, sizeof(device), file) != NULL
		&& strncmp(device, "0:0", 3) != 0;
	fclose(file);
	return configured;
}


void
LogindBridge::_UpdateSleepAvailability()
{
	// Deskbar asks on every menu build; the answers rarely change.
	bigtime_t now = system_time();
	if (fSleepCheckTime != 0 && now - fSleepCheckTime < 60000000)
		return;
	fSleepCheckTime = now;

	// Fresh connection: the bridge thread owns fBus for signals.
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0) {
		fCanSuspend = fCanHibernate = false;
		return;
	}
	fCanSuspend = _CanSleep(bus, "CanSuspend");
	fCanHibernate = _CanSleep(bus, "CanHibernate") && resume_configured();
	sd_bus_unref(bus);
}


bool
LogindBridge::_CanSleep(void* busHandle, const char* method)
{
	sd_bus* bus = (sd_bus*)busHandle;
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	const char* answer = NULL;
	int r = sd_bus_call_method(bus, kLogin1Bus, kLogin1Path, kLogin1Manager,
		method, &err, &reply, NULL);
	if (r >= 0)
		r = sd_bus_message_read(reply, "s", &answer);
	bool available = r >= 0 && answer != NULL
		&& (strcmp(answer, "yes") == 0 || strcmp(answer, "challenge") == 0);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	return available;
}


status_t
LogindBridge::_RequestSleep(const char* method)
{
	if (!fRunning)
		return B_NO_INIT;

	// Hand off to the bridge thread so the caller's looper is never
	// blocked across a polkit prompt or the sleep itself.
	atomic_set(&fPendingSleep, strcmp(method, "Suspend") == 0
		? kSleepRequestSuspend : kSleepRequestHibernate);
	return B_OK;
}


void
LogindBridge::_ProcessPendingSleep()
{
	int32 request = atomic_get_and_set(&fPendingSleep, kSleepRequestNone);
	if (request == kSleepRequestNone)
		return;

	const char* method = request == kSleepRequestSuspend
		? "Suspend" : "Hibernate";

	// Use a dedicated connection so the signal bus keeps running; interactive=true lets polkit ask.
	sd_bus* bus = NULL;
	int r = sd_bus_open_system(&bus);
	if (r < 0) {
		fprintf(stderr, "LogindBridge: %s: sd_bus_open_system: %s\n", method,
			strerror(-r));
		return;
	}

	sd_bus_error err = SD_BUS_ERROR_NULL;
	r = sd_bus_call_method(bus, kLogin1Bus, kLogin1Path, kLogin1Manager,
		method, &err, NULL, "b", 1);
	if (r < 0) {
		const char* reason = err.message != NULL ? err.message : strerror(-r);
		fprintf(stderr, "LogindBridge: %s refused: %s\n", method, reason);
		BMessage refused(kMsgLogindSleepRefused);
		refused.AddBool("hibernate", request == kSleepRequestHibernate);
		refused.AddString("reason", reason);
		fTarget.SendMessage(&refused);
	}
	sd_bus_error_free(&err);
	sd_bus_unref(bus);
}


int32
LogindBridge::_ThreadEntry(void* self)
{
	return ((LogindBridge*)self)->_ThreadLoop();
}


int32
LogindBridge::_ThreadLoop()
{
	sd_bus* bus = (sd_bus*)fBus;

	while (fRunning) {
		sd_bus_message* m = NULL;
		int r = sd_bus_process(bus, &m);
		if (r < 0)
			break;

		if (m != NULL) {
			const char* member = sd_bus_message_get_member(m);
			if (member != NULL) {
				int active = 0;
				if (strcmp(member, "Lock") == 0
						|| strcmp(member, "Unlock") == 0) {
					// Session signals carry no payload we need.
					BMessage post;
					post.what = (strcmp(member, "Lock") == 0)
						? kMsgLogindSessionLock
						: kMsgLogindSessionUnlock;
					fTarget.SendMessage(&post);
				} else if (sd_bus_message_read(m, "b", &active) >= 0) {
					BMessage post;
					if (strcmp(member, "PrepareForShutdown") == 0
						|| strcmp(member,
							"PrepareForShutdownWithMetadata") == 0) {
						post.what = kMsgLogindPrepareForShutdown;
						post.AddBool("active", active != 0);
						if (strcmp(member,
								"PrepareForShutdownWithMetadata") == 0) {
							const char* type = NULL;
							if (sd_bus_message_enter_container(m,
									'a', "{sv}") >= 0) {
								while (sd_bus_message_enter_container(
										m, 'e', "sv") > 0) {
									const char* key = NULL;
									if (sd_bus_message_read(m, "s",
											&key) >= 0
										&& key != NULL
										&& strcmp(key, "type") == 0) {
										sd_bus_message_read(m, "v",
											"s", &type);
									} else {
										sd_bus_message_skip(m, "v");
									}
									sd_bus_message_exit_container(m);
								}
								sd_bus_message_exit_container(m);
							}
							if (type != NULL)
								post.AddString("type", type);
						}
						fTarget.SendMessage(&post);
					} else if (strcmp(member, "PrepareForSleep") == 0) {
						post.what = kMsgLogindPrepareForSleep;
						post.AddBool("active", active != 0);
						fTarget.SendMessage(&post);
						// Auto-reacquire on resume so the next cycle
						// has the lock held before its signal fires.
						if (active == 0 && fSleepFd < 0)
							_AcquireSleepInhibit();
					}
				}
			}
			sd_bus_message_unref(m);
		}

		_ProcessPendingSleep();

		if (r > 0)
			continue;

		// r == 0: block until next event (500ms cap so fRunning flip is seen).
		sd_bus_wait(bus, 500000);

		_ProcessPendingSleep();
	}
	return 0;
}
