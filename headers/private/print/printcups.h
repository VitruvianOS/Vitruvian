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
	char	location[256];
	bool	is_default;
	bool	is_color;
	bool	is_everywhere;
	bool	accepting;
	int32	state;				// IPP printer-state (idle/processing/stopped)
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


struct printcups_device {
	char	uri[512];
	char	make_model[256];
	char	info[256];
	char	location[256];
	char	device_class[32];	// "local", "network", "usb"
	bool	everywhere;			// probed IPP Everywhere support
};


struct printcups_ppd {
	char	name[256];			// ppd-name used by cupsd
	char	make_model[256];
};


struct printcups_option_choice {
	char	value[64];
	char	text[128];
};


struct printcups_options {
	char	media[64];			// IPP media name; empty leaves as-is
	char	sides[32];			// one-sided / two-sided-long-edge / ...
	char	color_mode[32];		// color / monochrome; empty leaves as-is
	int32	quality;			// 0 leaves as-is; 1 draft, 2 normal, 3 best
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

// Preflet helpers. All of these can block on cupsd; call off the window
// thread. On failure printcups_last_error() holds cupsd's text.
status_t	printcups_set_default(const char* queue);
status_t	printcups_cancel_job(const char* queue, int32 jobId);
int			printcups_list_jobs(const char* queue, printcups_job* jobs,
					int maxJobs);

// model: "everywhere" for driverless IPP Everywhere, otherwise a PPD name
// from printcups_list_ppds. Needs lpadmin group: B_PERMISSION_DENIED.
status_t	printcups_add_printer(const char* name, const char* uri,
					const char* model);
status_t	printcups_remove_printer(const char* name);

// description is printer-info; location is printer-location. Empty keeps
// the current value.
status_t	printcups_rename_printer(const char* name, const char* description,
					const char* location);

// Accept/reject jobs (printer-is-accepting-jobs).
status_t	printcups_set_accepting(const char* name, bool accepting);

// Save media/sides/colour/quality as the queue defaults via cupsSetDests.
status_t	printcups_set_options(const char* name,
					const printcups_options* options);

status_t	printcups_print_test_page(const char* name);

status_t	printcups_hold_job(const char* queue, int32 jobId);
status_t	printcups_release_job(const char* queue, int32 jobId);
status_t	printcups_purge_jobs(const char* queue);

// Network (DNS-SD) and USB printers found by cupsd's backends.
// Returns count written, or -1 on error.
int			printcups_discover_devices(printcups_device* devices,
					int maxDevices);

// Drivers cupsd knows (lpinfo -m equivalent). uri may be NULL to list all.
int			printcups_list_ppds(const char* uri, printcups_ppd* ppds,
					int maxPpds);

// Supported values for one option on a queue ("media", "sides",
// "print-color-mode"). Returns count written, or -1 on error.
int			printcups_list_choices(const char* queue, const char* option,
					printcups_option_choice* choices, int maxChoices);

// cupsd's last error text for a failed call; empty string if none.
const char*	printcups_last_error();

}	// extern "C"


#endif // _PRINTCUPS_H
