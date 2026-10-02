/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#ifndef _PRINTER_WORKER_H
#define _PRINTER_WORKER_H


#include <Messenger.h>
#include <String.h>

#include "printcups.h"


// Ops that block on cupsd; each runs on its own thread and posts
// kMsgWorkerResult back to the reply messenger.
enum {
	kWorkerOpListQueues		= 1,
	kWorkerOpListJobs,
	kWorkerOpSetDefault,
	kWorkerOpCancelJob,
	kWorkerOpHoldJob,
	kWorkerOpReleaseJob,
	kWorkerOpPurgeJobs,
	kWorkerOpRemovePrinter,
	kWorkerOpRenamePrinter,
	kWorkerOpSetAccepting,
	kWorkerOpSetOptions,
	kWorkerOpTestPage,
	kWorkerOpAddPrinter,
	kWorkerOpDiscover,
	kWorkerOpListPPDs,
	kWorkerOpListChoices,
};


const uint32 kMsgWorkerResult = 'PWrk';


class PrinterWorker {
public:
	static	bool			Post(uint32 op, const BMessenger& reply,
								const BString& name = BString(),
								const BString& name2 = BString(),
								const BString& name3 = BString(),
								int32 jobId = 0, bool flag = false,
								const printcups_options* options = NULL);
};


#endif // _PRINTER_WORKER_H
