/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "TimedatedAsync.h"

#include <BusHelpers.h>
#include <OS.h>
#include <String.h>

#include "TimeMessages.h"


namespace BPrivate {


struct TimedatedJob {
	int32		op;
	BMessage	args;
	BMessenger	replyTo;
};


static int32
_TimedatedThread(void* data)
{
	TimedatedJob* job = static_cast<TimedatedJob*>(data);

	BString error;
	status_t status = B_BAD_VALUE;
	BString zone;
	bool ntp = false;
	bool synced = false;
	bool localRTC = false;
	bool enable = false;
	bool local = false;

	switch (job->op) {
		case kTimedatedOpSetTime:
		{
			int64 usec = 0;
			bool relative = false;
			job->args.FindInt64("usec", &usec);
			job->args.FindBool("relative", &relative);
			status = bus_timedate1_set_time(usec, relative, &error);
			break;
		}

		case kTimedatedOpSetTimezone:
		{
			const char* zoneArg = NULL;
			job->args.FindString("zone", &zoneArg);
			status = bus_timedate1_set_timezone(zoneArg, &error);
			break;
		}

		case kTimedatedOpSetNTP:
		{
			job->args.FindBool("enable", &enable);
			status = bus_timedate1_set_ntp(enable, &error);
			break;
		}

		case kTimedatedOpSetLocalRTC:
		{
			bool fixSystem = false;
			job->args.FindBool("local", &local);
			job->args.FindBool("fixSystem", &fixSystem);
			status = bus_timedate1_set_local_rtc(local, fixSystem, true,
				&error);
			break;
		}

		case kTimedatedOpGetTimezone:
			status = bus_timedate1_get_timezone(zone, &error);
			break;

		case kTimedatedOpGetNTP:
			status = bus_timedate1_get_ntp(ntp, &error);
			break;

		case kTimedatedOpGetNTPSynced:
			status = bus_timedate1_get_ntp_synchronized(synced, &error);
			break;

		case kTimedatedOpGetLocalRTC:
			status = bus_timedate1_get_local_rtc(localRTC, &error);
			break;

		default:
			break;
	}

	BMessage result(kTimedatedResult);
	result.AddInt32("op", job->op);
	result.AddInt32("status", status);
	if (error.Length() > 0)
		result.AddString("error", error.String());
	if (zone.Length() > 0)
		result.AddString("zone", zone.String());
	result.AddBool("ntp", ntp);
	result.AddBool("synced", synced);
	result.AddBool("localRTC", localRTC);
	result.AddBool("enable", enable);
	result.AddBool("local", local);

	job->replyTo.SendMessage(&result);
	delete job;
	return 0;
}


status_t
TimedatedAsyncRun(int32 op, const BMessage& args, const BMessenger& replyTo)
{
	if (!replyTo.IsValid())
		return B_BAD_VALUE;

	TimedatedJob* job = new(std::nothrow) TimedatedJob;
	if (job == NULL)
		return B_NO_MEMORY;

	job->op = op;
	job->args = args;
	job->replyTo = replyTo;

	thread_id thread = spawn_thread(_TimedatedThread, "timedated",
		B_NORMAL_PRIORITY, job);
	if (thread < B_OK) {
		delete job;
		return thread;
	}

	resume_thread(thread);
	return B_OK;
}


}	// namespace BPrivate
