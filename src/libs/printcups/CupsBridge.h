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
		status_t			AddPrinter(const char* name, const char* uri,
								const char* model);
		status_t			RemovePrinter(const char* name);
		status_t			RenamePrinter(const char* name,
								const char* description,
								const char* location);
		status_t			SetAccepting(const char* name, bool accepting);
		status_t			SetOptions(const char* name,
								const printcups_options* options);
		status_t			PrintTestPage(const char* name);
		status_t			HoldJob(const char* queue, int32 jobId);
		status_t			ReleaseJob(const char* queue, int32 jobId);
		status_t			PurgeJobs(const char* queue);

		int					ListDevices(printcups_device* devices,
								int maxDevices);
		int					ListPPDs(const char* uri, printcups_ppd* ppds,
								int maxPpds);
		int					ListChoices(const char* queue, const char* option,
								printcups_option_choice* choices,
								int maxChoices);

		const char*			LastError() const { return fLastError; }

private:
		void				_FillFromDest(const void* dest,
								printcups_queue_info* info);
		void				_SetError(const char* text);

		BString				fLastError;
};


extern "C" void*			printcups_load();
extern "C" void				printcups_unload();
extern "C" const char*		printcups_cups_last_error();


#endif // _CUPS_BRIDGE_H
