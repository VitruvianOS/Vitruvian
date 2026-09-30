/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINTCUPS_H
#define _PRINTCUPS_H


#include <SupportDefs.h>


class BMessage;


struct printcups_queue_info {
	char	name[256];
	char	uri[512];
	char	make_model[256];
	bool	is_default;
	bool	is_color;
	bool	is_everywhere;
	int32	xres;
	int32	yres;
	char	media[64];			// e.g. "iso_a4_210x297mm"
	char	media_name[64];		// e.g. "A4"
	float	paper_width;		// points (1/72")
	float	paper_height;
	float	margin_left;
	float	margin_right;
	float	margin_top;
	float	margin_bottom;
};


struct printcups_job {
	int32	id;
	char	name[256];
	char	queue[256];
	int32	state;				// 0 pending, 1 held, 2 processing, 3 stopped,
								// 4 canceled, 5 aborted, 6 completed
	int32	size;
	int32	impressions;			// pages
};


extern "C" {

// Returns number of queues written, or -1 on error.
int		printcups_list_queues(printcups_queue_info* queues, int maxQueues);

// Fills info for one queue. Returns B_OK or B_ERROR.
status_t
		printcups_queue_info_for(const char* name, printcups_queue_info* info);

// Default queue name; empty string if none. Returns B_OK / B_ERROR.
status_t
		printcups_default_queue(char* name, size_t size);

// Fill *settings with the paper, resolution, page range and copies keys BPrintJob understands.
// Uses the default queue, or *settings' current_printer if already set.
status_t
		printcups_default_settings(BMessage* settings);

// Modal page-setup panel in the calling app. Returns B_OK, B_CANCELED, B_ERROR.
status_t
		printcups_page_setup(BMessage* settings, bool* canceled);

// Modal print-setup panel (copies, page range, quality).
status_t
		printcups_job_setup(BMessage* settings, bool* canceled);

// Render the legacy spool file at spoolPath to PDF and cupsPrintFile it, using *settings.
// Returns B_OK and fills *jobId, or an error; fails cleanly when no printer exists.
status_t
		printcups_submit_spool(const char* spoolPath, BMessage* settings,
			const char* jobName, int32* jobId);

// BPrintJob::PrinterType() helper.
int32	printcups_printer_type(const char* queue);

// Preflet helpers.
status_t	printcups_set_default(const char* queue);
status_t	printcups_cancel_job(const char* queue, int32 jobId);
int			printcups_list_jobs(const char* queue, printcups_job* jobs,
					int maxJobs);
// Driverless IPP Everywhere queue creation (lpadmin -m everywhere).
status_t	printcups_add_printer_everywhere(const char* name,
					const char* uri);

}	// extern "C"


#endif // _PRINTCUPS_H
