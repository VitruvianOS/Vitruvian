/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SHARE_WORKER_H
#define _SHARE_WORKER_H


#include <Messenger.h>
#include <String.h>


// Spawns pkexec vos-network-shares-helper off the window thread.
// stdinData is written to the helper's stdin (SMB password stays off argv).
class ShareWorker {
public:
	static	bool			Post(const char* command, const BString& args,
								const BMessenger& reply,
								const BString& stdinData = BString());
};


#endif	// _SHARE_WORKER_H
