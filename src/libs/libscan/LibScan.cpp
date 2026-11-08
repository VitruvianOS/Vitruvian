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


// "step: status", so the app can tell where a scan failed.
static void
SetStepError(const char* what, const char* detail)
{
	BString text;
	text << what;
	if (detail != NULL && detail[0] != '\0')
		text << ": " << detail;
	gLastError = text;
}


static void
SetOptionError(const SANE_Option_Descriptor* desc, SANE_Status status)
{
	BString text;
	text << "option \"" << desc->title << "\": " << sane_strstatus(status);
	SetError(text.String());
}


static status_t
EnsureSane()
{
	if (gSaneReady)
		return B_OK;
	if (gSaneFailed) {
		SetError("SANE is not available");
		return B_ERROR;
	}

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


// Declared here (rather than just above their original batch-only call
// sites) so libscan_preview_start() can also check gBatchActive: a batch
// session keeps the device open between ADF pages (so the feeder isn't
// reset), and starting a preview's own sane_open() on that same device
// while it's held open is what triggers the SANE_STATUS_INVAL this guards
// against.
static ScanSession gBatchSession;
static bool gBatchActive = false;


static status_t
OpenDevice(const char* name, ScanSession* session)
{
	memset(session, 0, sizeof(*session));
	if (EnsureSane() != B_OK)
		return B_ERROR;

	SANE_Handle handle = NULL;
	SANE_Status status = sane_open(name, &handle);
	if (status != SANE_STATUS_GOOD) {
		BString text;
		text << "could not open scanner \"" << name << "\": "
			<< sane_strstatus(status);
		SetError(text.String());
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


// Clamps to the option's range or the nearest word list entry, in raw
// SANE units; backends refuse values they do not offer.
static void
SnapValueToConstraint(const SANE_Option_Descriptor* desc, SANE_Int* value)
{
	if (desc->constraint_type == SANE_CONSTRAINT_WORD_LIST) {
		const SANE_Word* words = desc->constraint.word_list;
		if (words == NULL || words[0] < 1)
			return;

		SANE_Int best = words[1];
		int64 bestDelta = *value >= (SANE_Int)best
			? (int64)*value - best : (int64)best - *value;
		for (int i = 2; i <= words[0]; i++) {
			int64 delta = *value >= (SANE_Int)words[i]
				? (int64)*value - words[i] : (int64)words[i] - *value;
			if (delta < bestDelta) {
				bestDelta = delta;
				best = words[i];
			}
		}
		*value = best;
		return;
	}

	if (desc->constraint_type != SANE_CONSTRAINT_RANGE)
		return;

	const SANE_Range* range = desc->constraint.range;
	SANE_Int v = *value;
	if (v < range->min)
		v = range->min;
	else if (v > range->max)
		v = range->max;

	if (range->quant > 0) {
		SANE_Int lower = range->min
			+ (v - range->min) / range->quant * range->quant;
		if (range->max - lower < range->quant)
			v = lower;
		else {
			SANE_Int upper = lower + range->quant;
			v = (v - lower) * 2 < range->quant ? lower : upper;
		}
	}
	*value = v;
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
		// Group headers have titles too ("Scan Mode"); skip to the option.
		if (desc->type == SANE_TYPE_GROUP)
			continue;
		if (!TitleMatches(desc->title, titleNeedle))
			continue;

		SANE_Status status;
		if (useInt) {
			SANE_Int value = intValue;
			if (desc->type == SANE_TYPE_FIXED)
				value = SANE_FIX(intValue);
			SnapValueToConstraint(desc, &value);
			status = sane_control_option(handle, index,
				SANE_ACTION_SET_VALUE, &value, NULL);
		} else if (desc->type == SANE_TYPE_STRING) {
			status = sane_control_option(handle, index,
				SANE_ACTION_SET_VALUE,
				(void*)(uintptr_t)stringValue, NULL);
		} else
			return B_BAD_VALUE;

		if (status != SANE_STATUS_GOOD) {
			SetOptionError(desc, status);
			return B_ERROR;
		}
		return B_OK;
	}
	return B_ERROR;
}


// Finds the first active string option whose title matches and that
// carries a string list, so callers can pick a real device word.
static const SANE_Option_Descriptor*
FindStringListOption(SANE_Handle handle, const char* titleNeedle,
	const SANE_String_Const** wordsOut)
{
	const SANE_Option_Descriptor* desc = NULL;
	int index = 0;
	for (desc = sane_get_option_descriptor(handle, index);
			desc != NULL;
			desc = sane_get_option_descriptor(handle, ++index)) {
		if ((desc->cap & SANE_CAP_INACTIVE) != 0)
			continue;
		if (desc->type != SANE_TYPE_STRING)
			continue;
		if (desc->constraint_type != SANE_CONSTRAINT_STRING_LIST)
			continue;
		if (!TitleMatches(desc->title, titleNeedle))
			continue;
		*wordsOut = desc->constraint.string_list;
		return desc;
	}
	return NULL;
}


// Picks the device word matching the requested colour format, falling
// back to the first choice when nothing matches.
static bool
PickModeWord(SANE_Handle handle, int32 format, char* out, size_t size)
{
	const SANE_String_Const* words = NULL;
	if (FindStringListOption(handle, "Mode", &words) == NULL
		|| words == NULL || words[0] == NULL)
		return false;

	const char* want = format == LIBSCAN_GRAY8 ? "gray"
		: format == LIBSCAN_BINARY ? "lineart" : "color";
	// String lists are NULL-terminated from index 0.
	for (int i = 0; words[i] != NULL; i++) {
		if (strcasestr(words[i], want) != NULL
			|| (format == LIBSCAN_BINARY
				&& strcasestr(words[i], "black") != NULL)
			|| (format == LIBSCAN_BINARY
				&& strcasestr(words[i], "binary") != NULL)) {
			strncpy(out, words[i], size - 1);
			out[size - 1] = '\0';
			return true;
		}
	}
	// Single-entry lists have no second word to fall back to.
	const char* fallback = words[1] != NULL ? words[1] : words[0];
	strncpy(out, fallback, size - 1);
	out[size - 1] = '\0';
	return true;
}


// Picks a Source word for the numeric source (0 flatbed, 1 ADF,
// 2 ADF duplex).
static bool
PickSourceWord(SANE_Handle handle, int32 source, char* out, size_t size)
{
	const SANE_String_Const* words = NULL;
	if (FindStringListOption(handle, "Source", &words) == NULL
		|| words == NULL || words[0] == NULL)
		return false;

	const char* want = source == 2 ? "duplex" : source == 1 ? "adf"
		: "flat";
	for (int i = 0; words[i] != NULL; i++) {
		if (strcasestr(words[i], want) != NULL
			|| (source == 1
				&& strcasestr(words[i], "feeder") != NULL)) {
			strncpy(out, words[i], size - 1);
			out[size - 1] = '\0';
			return true;
		}
	}
	// Duplex often is its own word without ADF in it; retry.
	if (source == 2) {
		for (int i = 0; words[i] != NULL; i++) {
			if (strcasestr(words[i], "adf") != NULL) {
				strncpy(out, words[i], size - 1);
				out[size - 1] = '\0';
				return true;
			}
		}
	}
	const char* fallback = words[1] != NULL ? words[1] : words[0];
	strncpy(out, fallback, size - 1);
	out[size - 1] = '\0';
	return true;
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

	char word[64];
	if (opts->mode[0] != '\0')
		SetOptionByTitle(handle, "Mode", opts->mode, 0, false);
	else if (PickModeWord(handle, opts->format, word, sizeof(word)))
		SetOptionByTitle(handle, "Mode", word, 0, false);

	if (opts->source_name[0] != '\0')
		SetOptionByTitle(handle, "Source", opts->source_name, 0, false);
	else if (PickSourceWord(handle, opts->source, word, sizeof(word)))
		SetOptionByTitle(handle, "Source", word, 0, false);

	// Area options are backend-specific; best effort. SANE geometry
	// units are millimetres, so inches convert by 25.4.
	if (opts->br_x > opts->tl_x) {
		SANE_Fixed tlx = SANE_FIX(opts->tl_x * 25.4f);
		SANE_Fixed tly = SANE_FIX(opts->tl_y * 25.4f);
		SANE_Fixed brx = SANE_FIX(opts->br_x * 25.4f);
		SANE_Fixed bry = SANE_FIX(opts->br_y * 25.4f);
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
				if (desc->type == SANE_TYPE_GROUP)
					continue;
				if (!TitleMatches(desc->title, areaTitles[i]))
					continue;
				if (desc->type == SANE_TYPE_FIXED) {
					SnapValueToConstraint(desc, values[i]);
					SANE_Status status = sane_control_option(
						handle, index, SANE_ACTION_SET_VALUE,
						values[i], NULL);
					if (status != SANE_STATUS_GOOD)
						SetOptionError(desc, status);
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
	if (params.lines <= 0 || params.pixels_per_line <= 0
		|| params.bytes_per_line <= 0) {
		BString text;
		text << "scanner reported an empty frame ("
			<< params.pixels_per_line << "x" << params.lines << ", "
			<< params.bytes_per_line << " bytes per line)";
		SetError(text.String());
		return B_BAD_VALUE;
	}

	color_space space = B_RGB32;
	if (params.format == SANE_FRAME_GRAY && params.depth == 8)
		space = B_GRAY8;
	else if (params.format == SANE_FRAME_GRAY && params.depth == 1)
		space = B_RGB32;

	// No B_BITMAP_NO_SERVER_LINK: this bitmap is handed to ScanView's
	// DrawBitmap() for the live preview, which only ever sends the
	// server-side token over to app_server, never the pixels themselves
	// (see BView::DrawBitmapAsync(), View.cpp). A no-server-link bitmap
	// never gets a token -- app_server silently draws nothing for it.
	BRect bounds(0, 0, params.pixels_per_line - 1, params.lines - 1);
	BBitmap* bitmap = new BBitmap(bounds, 0, space);
	if (bitmap->InitCheck() == B_BAD_VALUE) {
		delete bitmap;
		SetError("scan image format or size is not supported");
		return B_BAD_VALUE;
	}
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
	int32 rowFill = 0;
	while (rowsDone < params.lines) {
		if (gCancel) {
			sane_cancel(handle);
			delete[] line;
			delete bitmap;
			SetError("scan canceled");
			return B_CANCELED;
		}

		SANE_Int bytesRead = 0;
		SANE_Status status = sane_read(handle, line + rowFill,
			params.bytes_per_line - rowFill, &bytesRead);
		if (status == SANE_STATUS_EOF)
			break;
		if (status != SANE_STATUS_GOOD) {
			delete[] line;
			delete bitmap;
			SetStepError("reading scan data", sane_strstatus(status));
			return B_ERROR;
		}
		if (bytesRead <= 0)
			continue;

		rowFill += bytesRead;
		if (rowFill < params.bytes_per_line)
			continue;
		rowFill = 0;

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

	// Never report a stale error.
	SetError("");
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

	SetError("");
	ScanSession session;
	status_t status = OpenDevice(name, &session);
	if (status != B_OK)
		return status;

	libscan_options opts;
	ResetOptionsDefaults(&opts);

	// Choices are accumulated here; options is filled only at the end.
	BMessage values;

	const SANE_Option_Descriptor* desc = NULL;
	for (int index = 0;
			(desc = sane_get_option_descriptor(session.handle, index))
				!= NULL;
			index++) {
		if ((desc->cap & SANE_CAP_INACTIVE) != 0)
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
			if (desc->constraint_type == SANE_CONSTRAINT_WORD_LIST) {
				const SANE_Word* words = desc->constraint.word_list;
				for (int i = 1; i <= words[0]; i++) {
					int32 dpi = words[i];
					if (desc->type == SANE_TYPE_FIXED)
						dpi = (int32)(words[i] >> 16);
					values.AddInt32("resolution-choices", dpi);
				}
			} else if (desc->constraint_type == SANE_CONSTRAINT_RANGE) {
				const SANE_Range* range = desc->constraint.range;
				int32 min = range->min;
				int32 max = range->max;
				int32 quant = range->quant;
				if (desc->type == SANE_TYPE_FIXED) {
					min = (int32)(range->min >> 16);
					max = (int32)(range->max >> 16);
					quant = (int32)(range->quant >> 16);
				}
				if (quant <= 0)
					quant = 1;
				// Offer common presets inside the range instead
				// of every step.
				static const int32 kPresets[] = {
					50, 75, 100, 150, 200, 300, 400, 600,
					800, 1200, 2400, 4800
				};
				int32 count = 0;
				for (int32 i = 0; i < (int32)(sizeof(kPresets)
						/ sizeof(kPresets[0])); i++) {
					int32 dpi = kPresets[i];
					if (dpi < min || dpi > max
						|| (dpi - min) % quant != 0)
						continue;
					values.AddInt32("resolution-choices", dpi);
					count++;
				}
				if (count == 0) {
					values.AddInt32("resolution-choices", min);
					if (max > min)
						values.AddInt32("resolution-choices",
							max);
				}
			}
		} else if (TitleMatches(desc->title, "Mode")
			&& desc->type == SANE_TYPE_STRING) {
			char text[64];
			memset(text, 0, sizeof(text));
			if (sane_control_option(session.handle, index,
					SANE_ACTION_GET_VALUE, text, NULL)
					== SANE_STATUS_GOOD) {
				strlcpy(opts.mode, text, sizeof(opts.mode));
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
			if (desc->constraint_type == SANE_CONSTRAINT_STRING_LIST) {
				const SANE_String_Const* words
					= desc->constraint.string_list;
				for (int i = 0; words[i] != NULL; i++)
					values.AddString("mode-choices", words[i]);
			}
		} else if (TitleMatches(desc->title, "Source")
			&& desc->type == SANE_TYPE_STRING) {
			char text[64];
			memset(text, 0, sizeof(text));
			if (sane_control_option(session.handle, index,
					SANE_ACTION_GET_VALUE, text, NULL)
					== SANE_STATUS_GOOD) {
				strlcpy(opts.source_name, text,
					sizeof(opts.source_name));
				if (strcasestr(text, "adf") != NULL
					&& strcasestr(text, "duplex") != NULL)
					opts.source = 2;
				else if (strcasestr(text, "adf") != NULL)
					opts.source = 1;
				else
					opts.source = 0;
			}
			if (desc->constraint_type == SANE_CONSTRAINT_STRING_LIST) {
				const SANE_String_Const* words
					= desc->constraint.string_list;
				for (int i = 0; words[i] != NULL; i++)
					values.AddString("source-choices", words[i]);
			}
		} else if (TitleMatches(desc->title, "Bottom-right x")
			&& desc->constraint_type == SANE_CONSTRAINT_RANGE
			&& desc->type == SANE_TYPE_FIXED) {
			// Page width from the area range, in inches.
			float inches = (float)(desc->constraint.range->max >> 16)
				/ 25.4f;
			values.AddFloat("max-width", inches);
		} else if (TitleMatches(desc->title, "Bottom-right y")
			&& desc->constraint_type == SANE_CONSTRAINT_RANGE
			&& desc->type == SANE_TYPE_FIXED) {
			float inches = (float)(desc->constraint.range->max >> 16)
				/ 25.4f;
			values.AddFloat("max-height", inches);
		}
	}

	CloseDevice(&session);

	*options = values;
	options->AddInt32("resolution", opts.resolution_dpi);
	options->AddString("mode", opts.mode[0] != '\0' ? opts.mode : "Color");
	options->AddInt32("format", opts.format);
	options->AddInt32("source", opts.source);
	options->AddString("source-name", opts.source_name);
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

	SetError("");
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
		strncpy(opts.mode, mode, sizeof(opts.mode) - 1);
		if (strcasecmp(mode, "Gray") == 0
			|| strcasecmp(mode, "Grey") == 0)
			opts.format = LIBSCAN_GRAY8;
		else if (strcasecmp(mode, "Binary") == 0
			|| strcasecmp(mode, "B&W") == 0)
			opts.format = LIBSCAN_BINARY;
		else if (strcasestr(mode, "gray") != NULL
			|| strcasestr(mode, "grey") != NULL)
			opts.format = LIBSCAN_GRAY8;
		else if (strcasestr(mode, "lineart") != NULL
			|| strcasestr(mode, "black") != NULL
			|| strcasestr(mode, "binary") != NULL)
			opts.format = LIBSCAN_BINARY;
		else
			opts.format = LIBSCAN_RGB24;
	}

	const char* sourceName = NULL;
	if (options->FindString("source-name", &sourceName) == B_OK
		&& sourceName != NULL)
		strncpy(opts.source_name, sourceName,
			sizeof(opts.source_name) - 1);

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

	// ApplyOptions() tolerates a backend rejecting one setting but still
	// records its message; don't let it pass for this scan's own error.
	SetError("");

	ScanSession session;
	if (OpenDevice(name, &session) != B_OK) {
		gScanActive = false;
		gPreviewBitmap = NULL;
		free(name);
		return 0;
	}

	ApplyOptions(session.handle, opts);

	SANE_Status status = sane_start(session.handle);
	if (status != SANE_STATUS_GOOD) {
		SetStepError("could not start scan", sane_strstatus(status));
		CloseDevice(&session);
		free(name);
		gScanActive = false;
		gPreviewBitmap = NULL;
		return 0;
	}

	SANE_Parameters params;
	memset(&params, 0, sizeof(params));
	status = sane_get_parameters(session.handle, &params);
	if (status != SANE_STATUS_GOOD) {
		SetStepError("could not read scan parameters",
			sane_strstatus(status));
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

	SetError("");
	if (gScanActive) {
		SetError("another scan is still running");
		return B_BUSY;
	}
	// An ADF batch keeps its own sane_open() on this device between pages;
	// a second one here is what many network/eSCL backends answer with
	// SANE_STATUS_INVAL instead of "device busy". The UI already disables
	// the Preview button during a batch (PrintOMaticWindow::_UpdateButtons),
	// but libscan guards itself too rather than relying only on that.
	if (gBatchActive)
		return B_BUSY;

	libscan_options preview = *opts;
	if (preview.resolution_dpi <= 0 || preview.resolution_dpi > 150)
		preview.resolution_dpi = 75;
	// Grey thumbnail: without the mode word ApplyOptions picks a grey one.
	preview.format = LIBSCAN_GRAY8;
	preview.mode[0] = '\0';

	char* payload = (char*)calloc(1, 256 + sizeof(libscan_options));
	if (payload == NULL) {
		SetError("out of memory for preview");
		return B_NO_MEMORY;
	}
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
	SetError("");
	if (gScanActive) {
		SetError("another scan is still running");
		return B_BUSY;
	}

	ScanSession session;
	status_t status = OpenDevice(name, &session);
	if (status != B_OK)
		return status;

	ApplyOptions(session.handle, opts);
	SANE_Status saneStatus = sane_start(session.handle);
	if (saneStatus != SANE_STATUS_GOOD) {
		SetStepError("could not start scan",
			sane_strstatus(saneStatus));
		CloseDevice(&session);
		return B_ERROR;
	}

	SANE_Parameters params;
	memset(&params, 0, sizeof(params));
	saneStatus = sane_get_parameters(session.handle, &params);
	if (saneStatus != SANE_STATUS_GOOD) {
		SetStepError("could not read scan parameters",
			sane_strstatus(saneStatus));
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


status_t
libscan_batch_start(const char* name, const libscan_options* opts)
{
	if (name == NULL || opts == NULL)
		return B_BAD_VALUE;

	BAutolock lock(&gLock);
	SetError("");
	if (gScanActive || gBatchActive) {
		SetError("another scan is still running");
		return B_BUSY;
	}

	status_t status = OpenDevice(name, &gBatchSession);
	if (status != B_OK)
		return status;

	ApplyOptions(gBatchSession.handle, opts);
	gBatchActive = true;
	return B_OK;
}


status_t
libscan_batch_next_page(libscan_progress_cb cb, void* cookie,
	BBitmap** out)
{
	if (out == NULL)
		return B_BAD_VALUE;
	*out = NULL;

	BAutolock lock(&gLock);
	SetError("");
	if (!gBatchActive) {
		SetError("no batch scan is running");
		return B_NO_INIT;
	}

	SANE_Status start = sane_start(gBatchSession.handle);
	if (start == SANE_STATUS_EOF || start == SANE_STATUS_JAMMED
		|| start == SANE_STATUS_NO_DOCS) {
		SetError("document feeder is empty");
		return B_ERROR;
	}
	if (start != SANE_STATUS_GOOD) {
		SetStepError("could not start scan", sane_strstatus(start));
		return B_ERROR;
	}

	SANE_Parameters params;
	memset(&params, 0, sizeof(params));
	SANE_Status paramsStatus = sane_get_parameters(gBatchSession.handle,
		&params);
	if (paramsStatus != SANE_STATUS_GOOD) {
		SetStepError("could not read scan parameters",
			sane_strstatus(paramsStatus));
		return B_ERROR;
	}

	return ReadIntoBitmap(gBatchSession.handle, params, cb, cookie, out);
}


void
libscan_batch_stop(void)
{
	BAutolock lock(&gLock);
	if (!gBatchActive)
		return;
	CloseDevice(&gBatchSession);
	gBatchActive = false;
}
