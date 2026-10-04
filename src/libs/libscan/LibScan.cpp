/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * LibScan: private C API for V\OS scanning over SANE. Follows the
 * libprintcups shape: a small shared kit the preflet links directly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <Autolock.h>
#include <Bitmap.h>
#include <Locker.h>
#include <Message.h>
#include <OS.h>
#include <Rect.h>
#include <String.h>

#include <sane/sane.h>

#include "libscan.h"


static BLocker gLock(false);
static bool gSaneReady = false;
static bool gSaneFailed = false;
static BString gLastError;
static BString gDeviceSignature;
static volatile bool gCancel = false;
static volatile bool gScanActive = false;
static thread_id gScanThread = -1;
static BBitmap* gPreviewBitmap = NULL;
static libscan_progress_cb gProgressCb = NULL;
static void* gProgressCookie = NULL;


static void
SetError(const char* text)
{
	gLastError = text != NULL ? text : "";
}


static status_t
EnsureSane()
{
	if (gSaneReady)
		return B_OK;
	if (gSaneFailed)
		return B_ERROR;

	SANE_Int version = 0;
	SANE_Status status = sane_init(&version, NULL);
	if (status != SANE_STATUS_GOOD) {
		gSaneFailed = true;
		SetError(sane_strstatus(status));
		return B_ERROR;
	}
	gSaneReady = true;
	return B_OK;
}


static bool
IsNetworkName(const char* name)
{
	if (name == NULL)
		return false;
	return strncmp(name, "airscan:", 8) == 0
		|| strncmp(name, "net:", 4) == 0
		|| strncmp(name, "escl:", 5) == 0;
}


static void
CopyField(char* dst, size_t size, const char* src)
{
	if (size == 0)
		return;
	if (src == NULL) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, size - 1);
	dst[size - 1] = '\0';
}


static bool
TitleMatches(const SANE_String_Const title, const char* needle)
{
	if (title == NULL || needle == NULL)
		return false;
	return strcasestr(title, needle) != NULL;
}


struct ScanSession {
	SANE_Handle	handle;
	SANE_Parameters params;
	bool		open;
};


static status_t
OpenDevice(const char* name, ScanSession* session)
{
	memset(session, 0, sizeof(*session));
	if (EnsureSane() != B_OK)
		return B_ERROR;

	SANE_Handle handle = NULL;
	SANE_Status status = sane_open(name, &handle);
	if (status != SANE_STATUS_GOOD) {
		SetError(sane_strstatus(status));
		return B_ERROR;
	}
	session->handle = handle;
	session->open = true;
	return B_OK;
}


static void
CloseDevice(ScanSession* session)
{
	if (session->open && session->handle != NULL) {
		sane_cancel(session->handle);
		sane_close(session->handle);
	}
	session->handle = NULL;
	session->open = false;
}


static status_t
SetOptionByTitle(SANE_Handle handle, const char* titleNeedle,
	const char* stringValue, int intValue, bool useInt)
{
	const SANE_Option_Descriptor* desc = NULL;
	int index = 0;
	for (desc = sane_get_option_descriptor(handle, index);
			desc != NULL;
			desc = sane_get_option_descriptor(handle, ++index)) {
		if ((desc->cap & SANE_CAP_INACTIVE) != 0)
			continue;
		if (!TitleMatches(desc->title, titleNeedle))
			continue;

		SANE_Status status;
		if (useInt) {
			if (desc->type == SANE_TYPE_FIXED) {
				SANE_Fixed value = SANE_FIX(intValue);
				status = sane_control_option(handle, index,
					SANE_ACTION_SET_VALUE, &value, NULL);
			} else {
				SANE_Int value = intValue;
				status = sane_control_option(handle, index,
					SANE_ACTION_SET_VALUE, &value, NULL);
			}
		} else if (desc->type == SANE_TYPE_STRING) {
			status = sane_control_option(handle, index,
				SANE_ACTION_SET_VALUE,
				(void*)(uintptr_t)stringValue, NULL);
		} else
			return B_BAD_VALUE;

		if (status != SANE_STATUS_GOOD) {
			SetError(sane_strstatus(status));
			return B_ERROR;
		}
		return B_OK;
	}
	return B_ERROR;
}


static status_t
ApplyOptions(SANE_Handle handle, const libscan_options* opts)
{
	if (opts->resolution_dpi > 0
		&& SetOptionByTitle(handle, "Resolution", NULL,
			opts->resolution_dpi, true) != B_OK
		&& SetOptionByTitle(handle, "resolution", NULL,
			opts->resolution_dpi, true) != B_OK) {
		// Some backends only expose a numeric resolution; ignore miss.
	}

	if (opts->format == LIBSCAN_RGB24)
		SetOptionByTitle(handle, "Mode", "Color", 0, false);
	else if (opts->format == LIBSCAN_BINARY)
		SetOptionByTitle(handle, "Mode", "Binary", 0, false);
	else if (opts->format == LIBSCAN_GRAY8)
		SetOptionByTitle(handle, "Mode", "Gray", 0, false);

	if (opts->source == 1) {
		SetOptionByTitle(handle, "Source", "ADF", 0, false);
		SetOptionByTitle(handle, "source", "ADF", 0, false);
	} else if (opts->source == 2) {
		SetOptionByTitle(handle, "Source", "ADF Duplex", 0, false);
		SetOptionByTitle(handle, "Source", "Duplex", 0, false);
	} else {
		SetOptionByTitle(handle, "Source", "Flatbed", 0, false);
		SetOptionByTitle(handle, "source", "flatbed", 0, false);
	}

	// Area options are backend-specific; best effort.
	if (opts->br_x > opts->tl_x) {
		SANE_Fixed tlx = SANE_FIX((int)(opts->tl_x * 2540));
		SANE_Fixed tly = SANE_FIX((int)(opts->tl_y * 2540));
		SANE_Fixed brx = SANE_FIX((int)(opts->br_x * 2540));
		SANE_Fixed bry = SANE_FIX((int)(opts->br_y * 2540));
		const char* areaTitles[] = {
			"Top-left x", "Top-left y",
			"Bottom-right x", "Bottom-right y", NULL
		};
		SANE_Fixed* values[] = { &tlx, &tly, &brx, &bry };
		for (int i = 0; areaTitles[i] != NULL; i++) {
			int index = 0;
			const SANE_Option_Descriptor* desc;
			for (desc = sane_get_option_descriptor(handle, index);
					desc != NULL;
					desc = sane_get_option_descriptor(handle, ++index)) {
				if (!TitleMatches(desc->title, areaTitles[i]))
					continue;
				if (desc->type == SANE_TYPE_FIXED) {
					sane_control_option(handle, index,
						SANE_ACTION_SET_VALUE, values[i], NULL);
				}
				break;
			}
		}
	}

	if (opts->brightness != 0)
		SetOptionByTitle(handle, "Brightness", NULL, opts->brightness,
			true);
	if (opts->threshold != 0)
		SetOptionByTitle(handle, "Threshold", NULL, opts->threshold,
			true);

	return B_OK;
}


static status_t
ReadIntoBitmap(SANE_Handle handle, const SANE_Parameters& params,
	libscan_progress_cb cb, void* cookie, BBitmap** out)
{
	*out = NULL;
	if (params.lines <= 0 || params.pixels_per_line <= 0)
		return B_ERROR;

	color_space space = B_RGB32;
	if (params.format == SANE_FRAME_GRAY && params.depth == 8)
		space = B_GRAY8;
	else if (params.format == SANE_FRAME_GRAY && params.depth == 1)
		space = B_RGB32;

	BRect bounds(0, 0, params.pixels_per_line - 1, params.lines - 1);
	BBitmap* bitmap = new BBitmap(bounds, B_BITMAP_NO_SERVER_LINK, space);
	if (bitmap->InitCheck() != B_OK || bitmap->Bits() == NULL) {
		delete bitmap;
		SetError("out of memory for scan bitmap");
		return B_NO_MEMORY;
	}

	uint8* bits = static_cast<uint8*>(bitmap->Bits());
	int32 rowBytes = bitmap->BytesPerRow();
	uint8* line = new uint8[params.bytes_per_line + 16];

	gCancel = false;
	int32 rowsDone = 0;
	while (rowsDone < params.lines) {
		if (gCancel) {
			sane_cancel(handle);
			delete[] line;
			delete bitmap;
			SetError("scan canceled");
			return B_CANCELED;
		}

		SANE_Status status = sane_read(handle, line,
			params.bytes_per_line, NULL);
		if (status == SANE_STATUS_EOF)
			break;
		if (status != SANE_STATUS_GOOD) {
			delete[] line;
			delete bitmap;
			SetError(sane_strstatus(status));
			return B_ERROR;
		}

		uint8* dst = bits + (size_t)rowsDone * rowBytes;
		if (params.format == SANE_FRAME_RGB) {
			for (int32 x = 0; x < params.pixels_per_line; x++) {
				size_t src = (size_t)x * 3;
				if (src + 2 >= (size_t)params.bytes_per_line)
					break;
				// B_RGB32 on little endian stores B,G,R,A.
				dst[x * 4 + 0] = line[src + 2];
				dst[x * 4 + 1] = line[src + 1];
				dst[x * 4 + 2] = line[src + 0];
				dst[x * 4 + 3] = 0xff;
			}
		} else if (params.format == SANE_FRAME_GRAY && params.depth == 1) {
			// Bit packing: expand each bit to a black/white RGB pixel.
			for (int32 x = 0; x < params.pixels_per_line; x++) {
				int32 byte = x / 8;
				int32 bit = 7 - (x % 8);
				if (byte >= params.bytes_per_line)
					break;
				bool on = (line[byte] & (1 << bit)) != 0;
				uint8 v = on ? 0 : 0xff;
				dst[x * 4 + 0] = v;
				dst[x * 4 + 1] = v;
				dst[x * 4 + 2] = v;
				dst[x * 4 + 3] = 0xff;
			}
		} else {
			int32 count = params.pixels_per_line;
			if (count > params.bytes_per_line)
				count = params.bytes_per_line;
			memcpy(dst, line, count);
		}

		rowsDone++;
		if (cb != NULL)
			cb(cookie, (float)rowsDone / (float)params.lines, 0);
	}

	delete[] line;
	if (rowsDone <= 0) {
		delete bitmap;
		SetError("scanner returned no data");
		return B_ERROR;
	}

	*out = bitmap;
	return B_OK;
}


static void
ResetOptionsDefaults(libscan_options* opts)
{
	memset(opts, 0, sizeof(*opts));
	opts->format = LIBSCAN_RGB24;
	opts->resolution_dpi = 300;
	opts->source = 0;
	opts->tl_x = 0;
	opts->tl_y = 0;
	opts->br_x = 8.5f;
	opts->br_y = 11.0f;
}


int
libscan_list_devices(libscan_device* out, int max)
{
	if (out == NULL || max <= 0)
		return 0;
	if (EnsureSane() != B_OK)
		return -1;

	const SANE_Device** list = NULL;
	SANE_Status status = sane_get_devices(&list, SANE_FALSE);
	if (status != SANE_STATUS_GOOD) {
		SetError(sane_strstatus(status));
		return -1;
	}

	int count = 0;
	BString signature;
	for (int i = 0; list != NULL && list[i] != NULL && count < max; i++) {
		const SANE_Device* dev = list[i];
		CopyField(out[count].name, sizeof(out[count].name), dev->name);
		CopyField(out[count].model, sizeof(out[count].model), dev->model);
		CopyField(out[count].type, sizeof(out[count].type), dev->type);
		out[count].is_network = IsNetworkName(dev->name);
		signature << dev->name << "|";
		count++;
	}

	if (gDeviceSignature != signature) {
		gDeviceSignature = signature;
	}
	return count;
}


status_t
libscan_device_info(const char* name, libscan_device* info)
{
	if (name == NULL || info == NULL)
		return B_BAD_VALUE;

	libscan_device devices[64];
	int count = libscan_list_devices(devices, 64);
	if (count < 0)
		return B_ERROR;

	for (int i = 0; i < count; i++) {
		if (strcmp(devices[i].name, name) == 0) {
			*info = devices[i];
			return B_OK;
		}
	}
	SetError("scanner not found");
	return B_BAD_VALUE;
}


status_t
libscan_get_options(const char* name, BMessage* options)
{
	if (name == NULL || options == NULL)
		return B_BAD_VALUE;

	ScanSession session;
	status_t status = OpenDevice(name, &session);
	if (status != B_OK)
		return status;

	libscan_options opts;
	ResetOptionsDefaults(&opts);

	const SANE_Option_Descriptor* desc = NULL;
	for (int index = 0;
			(desc = sane_get_option_descriptor(session.handle, index))
				!= NULL;
			index++) {
		if ((desc->cap & SANE_CAP_INACTIVE) != 0
			|| (desc->cap & SANE_CAP_SOFT_DETECT) == 0)
			continue;

		if (TitleMatches(desc->title, "Resolution")
			&& (desc->type == SANE_TYPE_INT
				|| desc->type == SANE_TYPE_FIXED)) {
			SANE_Int value = 0;
			if (sane_control_option(session.handle, index,
					SANE_ACTION_GET_VALUE, &value, NULL)
					== SANE_STATUS_GOOD) {
				if (desc->type == SANE_TYPE_FIXED)
					value = (SANE_Int)(value >> 16);
				opts.resolution_dpi = value;
			}
		} else if (TitleMatches(desc->title, "Mode")
			&& desc->type == SANE_TYPE_STRING) {
			char text[64];
			memset(text, 0, sizeof(text));
			if (sane_control_option(session.handle, index,
					SANE_ACTION_GET_VALUE, text, NULL)
					== SANE_STATUS_GOOD) {
				if (strcasestr(text, "color") != NULL
					|| strcasestr(text, "rgb") != NULL)
					opts.format = LIBSCAN_RGB24;
				else if (strcasestr(text, "binary")
					!= NULL || strcasestr(text, "lineart")
					!= NULL || strcasestr(text, "black") != NULL)
					opts.format = LIBSCAN_BINARY;
				else
					opts.format = LIBSCAN_GRAY8;
			}
		} else if (TitleMatches(desc->title, "Source")
			&& desc->type == SANE_TYPE_STRING) {
			char text[64];
			memset(text, 0, sizeof(text));
			if (sane_control_option(session.handle, index,
					SANE_ACTION_GET_VALUE, text, NULL)
					== SANE_STATUS_GOOD) {
				if (strcasestr(text, "adf") != NULL
					&& strcasestr(text, "duplex") != NULL)
					opts.source = 2;
				else if (strcasestr(text, "adf") != NULL)
					opts.source = 1;
				else
					opts.source = 0;
			}
		}
	}

	CloseDevice(&session);

	options->MakeEmpty();
	options->AddInt32("resolution", opts.resolution_dpi);
	const char* mode = "Color";
	if (opts.format == LIBSCAN_GRAY8)
		mode = "Gray";
	else if (opts.format == LIBSCAN_BINARY)
		mode = "Binary";
	options->AddString("mode", mode);
	options->AddInt32("format", opts.format);
	options->AddInt32("source", opts.source);
	options->AddFloat("tl-x", opts.tl_x);
	options->AddFloat("tl-y", opts.tl_y);
	options->AddFloat("br-x", opts.br_x);
	options->AddFloat("br-y", opts.br_y);
	options->AddInt32("brightness", opts.brightness);
	options->AddInt32("threshold", opts.threshold);
	return B_OK;
}


status_t
libscan_set_options(const char* name, const BMessage* options)
{
	if (name == NULL || options == NULL)
		return B_BAD_VALUE;

	ScanSession session;
	status_t status = OpenDevice(name, &session);
	if (status != B_OK)
		return status;

	libscan_options opts;
	ResetOptionsDefaults(&opts);
	options->FindInt32("resolution", &opts.resolution_dpi);
	int32 format = opts.format;
	options->FindInt32("format", &format);
	opts.format = format;
	options->FindInt32("source", &opts.source);
	options->FindFloat("tl-x", &opts.tl_x);
	options->FindFloat("tl-y", &opts.tl_y);
	options->FindFloat("br-x", &opts.br_x);
	options->FindFloat("br-y", &opts.br_y);
	options->FindInt32("brightness", &opts.brightness);
	options->FindInt32("threshold", &opts.threshold);

	const char* mode = NULL;
	if (options->FindString("mode", &mode) == B_OK && mode != NULL) {
		if (strcasecmp(mode, "Gray") == 0
			|| strcasecmp(mode, "Grey") == 0)
			opts.format = LIBSCAN_GRAY8;
		else if (strcasecmp(mode, "Binary") == 0
			|| strcasecmp(mode, "B&W") == 0)
			opts.format = LIBSCAN_BINARY;
		else
			opts.format = LIBSCAN_RGB24;
	}

	status = ApplyOptions(session.handle, &opts);
	CloseDevice(&session);
	return status;
}


static int32
ScanEntry(void* data)
{
	char* name = static_cast<char*>(data);
	libscan_options* opts = reinterpret_cast<libscan_options*>(
		reinterpret_cast<char*>(data) + 256);

	ScanSession session;
	if (OpenDevice(name, &session) != B_OK) {
		gScanActive = false;
		gPreviewBitmap = NULL;
		free(name);
		return 0;
	}

	ApplyOptions(session.handle, opts);
	sane_start(session.handle);

	SANE_Parameters params;
	memset(&params, 0, sizeof(params));
	if (sane_get_parameters(session.handle, &params) != SANE_STATUS_GOOD
		|| params.lines <= 0) {
		CloseDevice(&session);
		free(name);
		gScanActive = false;
		gPreviewBitmap = NULL;
		return 0;
	}

	BBitmap* bitmap = NULL;
	ReadIntoBitmap(session.handle, params, gProgressCb, gProgressCookie,
		&bitmap);
	CloseDevice(&session);

	gPreviewBitmap = bitmap;
	gScanActive = false;
	free(name);
	return 0;
}


status_t
libscan_preview_start(const char* name, const libscan_options* opts,
	libscan_progress_cb cb, void* cookie)
{
	if (name == NULL || opts == NULL)
		return B_BAD_VALUE;
	BAutolock lock(&gLock);
	if (gScanActive)
		return B_BUSY;

	libscan_options preview = *opts;
	if (preview.resolution_dpi <= 0 || preview.resolution_dpi > 150)
		preview.resolution_dpi = 75;
	preview.format = LIBSCAN_GRAY8;

	char* payload = (char*)calloc(1, 256 + sizeof(libscan_options));
	if (payload == NULL)
		return B_NO_MEMORY;
	strncpy(payload, name, 255);
	memcpy(payload + 256, &preview, sizeof(preview));

	gProgressCb = cb;
	gProgressCookie = cookie;
	gScanActive = true;
	gPreviewBitmap = NULL;

	thread_id thread = spawn_thread(ScanEntry, "libscan:preview",
		B_NORMAL_PRIORITY, payload);
	if (thread < B_OK) {
		gScanActive = false;
		free(payload);
		SetError("could not start preview thread");
		return B_ERROR;
	}
	gScanThread = thread;
	resume_thread(thread);
	return B_OK;
}


status_t
libscan_preview_wait(BBitmap** out)
{
	if (out == NULL)
		return B_BAD_VALUE;
	while (gScanActive)
		snooze(20000);

	BAutolock lock(&gLock);
	*out = gPreviewBitmap;
	gPreviewBitmap = NULL;
	return *out != NULL ? B_OK : B_ERROR;
}


void
libscan_preview_cancel(void)
{
	gCancel = true;
}


status_t
libscan_scan_page(const char* name, const libscan_options* opts,
	libscan_progress_cb cb, void* cookie, BBitmap** out)
{
	if (name == NULL || opts == NULL || out == NULL)
		return B_BAD_VALUE;
	*out = NULL;

	BAutolock lock(&gLock);
	if (gScanActive)
		return B_BUSY;

	ScanSession session;
	status_t status = OpenDevice(name, &session);
	if (status != B_OK)
		return status;

	ApplyOptions(session.handle, opts);
	if (sane_start(session.handle) != SANE_STATUS_GOOD) {
		CloseDevice(&session);
		return B_ERROR;
	}

	SANE_Parameters params;
	memset(&params, 0, sizeof(params));
	if (sane_get_parameters(session.handle, &params) != SANE_STATUS_GOOD) {
		CloseDevice(&session);
		return B_ERROR;
	}

	status = ReadIntoBitmap(session.handle, params, cb, cookie, out);
	CloseDevice(&session);
	return status;
}


void
libscan_scan_cancel(void)
{
	gCancel = true;
}


bool
libscan_devices_changed(void)
{
	libscan_device devices[64];
	BString before = gDeviceSignature;
	int count = libscan_list_devices(devices, 64);
	if (count < 0)
		return false;
	return before != gDeviceSignature;
}


const char*
libscan_last_error()
{
	return gLastError.String();
}
