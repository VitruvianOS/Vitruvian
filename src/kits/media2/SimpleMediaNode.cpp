/*
 * Copyright 2015-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/SimpleMediaNode.h>


BSimpleMediaNode::BSimpleMediaNode(const char* name, media_type type,
	media_client_kinds kinds)
	:
	BMediaNode(name, type, kinds),
	fNotifyHook(NULL),
	fProcessHook(NULL),
	fFormatHook(NULL),
	fNotifyCookie(NULL),
	fProcessCookie(NULL),
	fFormatCookie(NULL)
{
}


BSimpleMediaNode::~BSimpleMediaNode()
{
}


void
BSimpleMediaNode::SetNotifyHook(notify_hook hook, void* cookie)
{
	fNotifyHook   = hook;
	fNotifyCookie = cookie;
}


void
BSimpleMediaNode::SetProcessHook(process_hook hook, void* cookie)
{
	fProcessHook   = hook;
	fProcessCookie = cookie;
}


void
BSimpleMediaNode::SetFormatHook(format_hook hook, void* cookie)
{
	fFormatHook   = hook;
	fFormatCookie = cookie;
}


void
BSimpleMediaNode::HandleStart(bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_START, performanceTime);
}


void
BSimpleMediaNode::HandleStop(bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_STOP, performanceTime);
}


void
BSimpleMediaNode::HandleSeek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	if (fNotifyHook != NULL)
		fNotifyHook(fNotifyCookie, B_WILL_SEEK, mediaTime, performanceTime);
}


void
BSimpleMediaNode::ProcessCallback(BMediaConnection* connection, void* buffer,
	size_t bufferSize, uint32 frameCount)
{
	if (fProcessHook == NULL) {
		BMediaClient::ProcessCallback(NULL, buffer, bufferSize, frameCount);
		return;
	}

	BMediaFormat format;
	if (connection != NULL)
		format = connection->Format();
	else
		format.SetToDefault();

	fProcessHook(fProcessCookie, buffer, bufferSize, frameCount, format);
}
