/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LIBSCAN_H
#define _LIBSCAN_H


#include <SupportDefs.h>


class BBitmap;
class BMessage;


enum {
	LIBSCAN_GRAY8	= 0,
	LIBSCAN_RGB24	= 1,
	LIBSCAN_BINARY	= 2,
};


struct libscan_device {
	char	name[256];		// SANE device name
	char	model[256];
	char	type[64];		// "flatbed", "ADF", "camera", ...
	bool	is_network;		// airscan:/net:
};


struct libscan_options {
	int32	format;			// libscan_GRAY8 / RGB24 / BINARY
	int32	resolution_dpi;	// 75/150/300/600
	int32	source;			// 0 flatbed, 1 ADF, 2 ADF-duplex
	float	tl_x, tl_y;		// inches, scan area
	float	br_x, br_y;
	int32	brightness;		// -1000..1000, 0 = default
	int32	threshold;		// for B&W
	char	mode[32];		// exact SANE word; empty maps from format
	char	source_name[32];	// exact SANE word; empty maps from source
};


typedef void (*libscan_progress_cb)(void* cookie, float done, int32 page);


extern "C" {

// Discovery. Returns number of devices written, or -1 on error.
int		libscan_list_devices(libscan_device* out, int max);
status_t	libscan_device_info(const char* name, libscan_device* info);

// Options merged into a BMessage with stable keys:
// "resolution" int32, "mode" string, "source" int32, "source-name" string,
// "tl-x"/"tl-y"/"br-x"/"br-y" float, "brightness"/"threshold" int32.
// Choice lists come straight from the SANE option descriptors so the UI
// offers only what the device really has:
// "mode-choices"/"source-choices" strings, "resolution-choices" int32,
// "max-width"/"max-height" float (inches, from the br-x/br-y ranges).
status_t	libscan_get_options(const char* name, BMessage* options);
status_t	libscan_set_options(const char* name, const BMessage* options);

// Preview: low-dpi area scan, async, cancelable.
status_t	libscan_preview_start(const char* name, const libscan_options* opts,
			libscan_progress_cb cb, void* cookie);
status_t	libscan_preview_wait(BBitmap** out);
void	libscan_preview_cancel(void);

// Full scan, one page at a time (ADF loops by calling again).
status_t	libscan_scan_page(const char* name, const libscan_options* opts,
			libscan_progress_cb cb, void* cookie, BBitmap** out);
void	libscan_scan_cancel(void);

// ADF batch: keep the device open across pages so the feeder is not
// reset between scans. next_page fails with "document feeder is empty"
// when the backend reports no more pages.
status_t	libscan_batch_start(const char* name, const libscan_options* opts);
status_t	libscan_batch_next_page(libscan_progress_cb cb, void* cookie,
			BBitmap** out);
void	libscan_batch_stop(void);

// Hotplug: true if the device list changed since the last call.
bool	libscan_devices_changed(void);

const char*	libscan_last_error();

}	// extern "C"


#endif	// _LIBSCAN_H
