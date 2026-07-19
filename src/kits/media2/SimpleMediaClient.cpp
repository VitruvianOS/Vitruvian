/*
 * Copyright 2025-2026, Dario Casalinuovo. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/SimpleMediaClient.h>


BSimpleMediaClient::BSimpleMediaClient(const char* name, media_type type,
	media_client_kinds kinds)
	:
	BMediaClient(name, type, kinds),
	fNotifyHook(NULL),
	fProcessHook(NULL),
	fNotifyCookie(NULL),
	fProcessCookie(NULL)
{
}


BSimpleMediaClient::~BSimpleMediaClient()
{
}


BSimpleMediaInput*
BSimpleMediaClient::BeginInput()
{
	// BSimpleMediaInput lands when Cortex (Batch I) needs callback-driven
	// untyped pass-through panels.
	return NULL;
}


BSimpleMediaOutput*
BSimpleMediaClient::BeginOutput()
{
	return NULL;
}


void
BSimpleMediaClient::SetNotifyHook(notify_hook hook, void* cookie)
{
	fNotifyHook   = hook;
	fNotifyCookie = cookie;
}


void
BSimpleMediaClient::SetProcessHook(process_hook hook, void* cookie)
{
	fProcessHook   = hook;
	fProcessCookie = cookie;
}


void
BSimpleMediaClient::HandleStart(bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_START, performanceTime);
}


void
BSimpleMediaClient::HandleStop(bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_STOP, performanceTime);
}


void
BSimpleMediaClient::HandleSeek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_SEEK, mediaTime, performanceTime);
}


void
BSimpleMediaClient::ProcessCallback(BMediaConnection* connection, void* buffer,
	size_t bufferSize, uint32 frameCount)
{
	if (fProcessHook == NULL) {
		BMediaClient::ProcessCallback(connection, buffer, bufferSize, frameCount);
		return;
	}
	fProcessHook(fProcessCookie, connection, buffer, bufferSize, frameCount);
}
