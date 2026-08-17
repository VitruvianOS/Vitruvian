/*
 * Copyright 2015-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/SimpleMediaUnit.h>


BSimpleMediaUnit::BSimpleMediaUnit(const char* name, media_client_kinds kinds)
	:
	BMediaUnit(name, kinds),
	fNotifyHook(NULL),
	fCycleHook(NULL),
	fFormatHook(NULL),
	fNotifyCookie(NULL),
	fCycleCookie(NULL),
	fFormatCookie(NULL)
{
}


BSimpleMediaUnit::~BSimpleMediaUnit()
{
}


void
BSimpleMediaUnit::SetNotifyHook(notify_hook hook, void* cookie)
{
	fNotifyHook   = hook;
	fNotifyCookie = cookie;
}


void
BSimpleMediaUnit::SetCycleHook(cycle_hook hook, void* cookie)
{
	fCycleHook   = hook;
	fCycleCookie = cookie;
}


void
BSimpleMediaUnit::SetFormatHook(format_hook hook, void* cookie)
{
	fFormatHook   = hook;
	fFormatCookie = cookie;
}


void
BSimpleMediaUnit::HandleStart(bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_START, performanceTime);
}


void
BSimpleMediaUnit::HandleStop(bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_STOP, performanceTime);
}


void
BSimpleMediaUnit::HandleSeek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_SEEK, mediaTime, performanceTime);
}


void
BSimpleMediaUnit::ProcessCycle(uint32 frameCount)
{
	if (fCycleHook == NULL) {
		BMediaUnit::ProcessCycle(frameCount);
		return;
	}

	fCycleHook(fCycleCookie, this, frameCount);
}
