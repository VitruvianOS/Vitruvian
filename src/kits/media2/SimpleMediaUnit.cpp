/*
 * Copyright 2015-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
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


status_t
BSimpleMediaUnit::Start()
{
	// Format suggestion runs before the filter is built so a client can
	// reject or adjust the unit's format once, not per connection.
	if (fFormatHook != NULL) {
		BMediaFormat format;
		status_t result = fFormatHook(fFormatCookie, &format);
		if (result != B_OK)
			return result;
		if (fNotifyHook != NULL)
			fNotifyHook(fNotifyCookie, B_FORMAT_SUGGESTION, &format);
	}
	return BMediaUnit::Start();
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
