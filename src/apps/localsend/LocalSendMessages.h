/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _LOCAL_SEND_MESSAGES_H
#define _LOCAL_SEND_MESSAGES_H

// Message constants shared between the manager threads and the UI.

enum {
	// A peer announced or registered itself. Carries "alias",
	// "model", "fingerprint", "address", "typeName", "version"
	// (BString), "port" (uint16), "protocol" (BString), "download"
	// (bool).
	kMsgDeviceUpdate = 'LSdv',

	// Incoming files; show the accept prompt. Carries "alias" and
	// repeated "file"/"size" pairs.
	kMsgReceivePrompt = 'LSrc',

	// Send progress: "index" (int32), "sent"/"total" (int64),
	// "peer"/"file" (BString), "fraction" (float).
	kMsgSendProgress = 'LSpg',

	// Send finished: "success" (bool), "peer" (BString), "files"
	// (int32), "bytes" (int64).
	kMsgSendDone = 'LSdn'
};

#endif // _LOCAL_SEND_MESSAGES_H
