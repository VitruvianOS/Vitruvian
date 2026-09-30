/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _CUPS_BRIDGE_H
#define _CUPS_BRIDGE_H


#include <String.h>

#include "printcups.h"


// CupsBridge: thin libcups wrapper used by libprintcups. No Haiku print
// CupsBridge: thin libcups wrapper used by libprintcups; jobs go straight to cupsd.
class CupsBridge {
public:
							CupsBridge();
							~CupsBridge();

		status_t			ListQueues(printcups_queue_info* queues,
								int maxQueues);
		status_t			QueueInfo(const char* name,
								printcups_queue_info* info);
		status_t			DefaultQueue(char* name, size_t size);
		status_t			SetDefault(const char* name);

		// Submit a PDF file. settings keys: current_printer, copies,
		// media (Haiku name or cups media), xres/yres.
		status_t			SubmitPdf(const char* filePath,
								BMessage* settings, int32* jobId);
		status_t			CancelJob(const char* queue, int32 jobId);
		int					ListJobs(const char* queue, printcups_job* jobs,
								int maxJobs);
		status_t			AddPrinterEverywhere(const char* name,
								const char* uri);

private:
		void				_FillFromDest(const void* dest,
								printcups_queue_info* info);
};


extern "C" void*			printcups_load();
extern "C" void				printcups_unload();


#endif // _CUPS_BRIDGE_H
