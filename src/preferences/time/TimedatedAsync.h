/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _TIMEDATED_ASYNC_H
#define _TIMEDATED_ASYNC_H


#include <Message.h>
#include <Messenger.h>
#include <SupportDefs.h>


namespace BPrivate {


// timedated work runs on a worker thread so a polkit prompt cannot
// freeze the Time preflet. The worker posts kTimedatedResult back.
enum {
	kTimedatedOpSetTime = 1,
	kTimedatedOpSetTimezone,
	kTimedatedOpSetNTP,
	kTimedatedOpSetLocalRTC,
	kTimedatedOpGetTimezone,
	kTimedatedOpGetNTP,
	kTimedatedOpGetNTPSynced,
	kTimedatedOpGetLocalRTC,
};


// args: usec, relative, zone, enable, local, fixSystem. Result: op, status,
// error, zone, ntp, synced, localRTC, plus enable/local echoed.
status_t
TimedatedAsyncRun(int32 op, const BMessage& args,
	const BMessenger& replyTo);


}	// namespace BPrivate


#endif	// _TIMEDATED_ASYNC_H
