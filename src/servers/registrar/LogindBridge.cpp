/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LogindBridge.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#include <Message.h>


static const char* kLogin1Bus       = "org.freedesktop.login1";
static const char* kLogin1Path      = "/org/freedesktop/login1";
static const char* kLogin1Manager   = "org.freedesktop.login1.Manager";


LogindBridge::LogindBridge(const BMessenger& target)
	:
	fTarget(target),
	fBus(NULL),
	fShutdownFd(-1),
	fSleepFd(-1),
	fThread(-1),
	fRunning(false)
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
	r = sd_bus_match_signal(bus, NULL, kLogin1Bus, kLogin1Path, kLogin1Manager,
		"PrepareForSleep", NULL, this);
	if (r < 0)
		fprintf(stderr, "LogindBridge: match PrepareForSleep: %s\n", strerror(-r));

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
				if (sd_bus_message_read(m, "b", &active) >= 0) {
					BMessage post;
					if (strcmp(member, "PrepareForShutdown") == 0) {
						post.what = kMsgLogindPrepareForShutdown;
						post.AddBool("active", active != 0);
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

		if (r > 0)
			continue;

		// r == 0: block until next event (500ms cap so fRunning flip is seen).
		sd_bus_wait(bus, 500000);
	}
	return 0;
}
