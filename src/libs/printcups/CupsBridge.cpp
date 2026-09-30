/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * CupsBridge: libcups integration for V\OS printing. There is no Haiku
 * print_server; BPrintJob talks to cupsd through this library.
 */

#include "CupsBridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Message.h>

#include <cups/cups.h>
#include <cups/ppd.h>


static const char*
MediaFromSettings(BMessage* settings)
{
	const char* media = NULL;
	if (settings != NULL
		&& settings->FindString("media", &media) == B_OK
		&& media != NULL && media[0] != '\0')
		return media;

	return "iso_a4_210x297mm";
}


static const char*
CupsMediaName(const char* media)
{
	if (media == NULL || media[0] == '\0')
		return "iso_a4_210x297mm";

	// Accept both cups media keys and short Haiku names.
	if (strcasecmp(media, "A4") == 0
		|| strcasecmp(media, "iso_a4_210x297mm") == 0)
		return "iso_a4_210x297mm";
	if (strcasecmp(media, "Letter") == 0
		|| strcasecmp(media, "us_letter_8.5x11in") == 0)
		return "us_letter_8.5x11in";
	if (strcasecmp(media, "Legal") == 0
		|| strcasecmp(media, "us_legal_8.5x14in") == 0)
		return "us_legal_8.5x14in";
	if (strcasecmp(media, "A5") == 0
		|| strcasecmp(media, "iso_a5_148x210mm") == 0)
		return "iso_a5_148x210mm";
	if (strcasecmp(media, "A3") == 0
		|| strcasecmp(media, "iso_a3_297x420mm") == 0)
		return "iso_a3_297x420mm";

	return media;
}


static void
MediaToHaikuName(const char* media, char* out, size_t outSize)
{
	if (media == NULL || media[0] == '\0') {
		strlcpy(out, "A4", outSize);
		return;
	}

	if (strncasecmp(media, "iso_a4", 6) == 0)
		strlcpy(out, "A4", outSize);
	else if (strncasecmp(media, "iso_a3", 6) == 0)
		strlcpy(out, "A3", outSize);
	else if (strncasecmp(media, "iso_a5", 6) == 0)
		strlcpy(out, "A5", outSize);
	else if (strncasecmp(media, "us_letter", 9) == 0)
		strlcpy(out, "Letter", outSize);
	else if (strncasecmp(media, "us_legal", 8) == 0)
		strlcpy(out, "Legal", outSize);
	else
		strlcpy(out, media, outSize);
}


// IPP media name -> paper size in points (1/72 inch) and margins.
static bool
MediaGeometry(const char* media, float* width, float* height,
	float* marginLeft, float* marginRight, float* marginTop,
	float* marginBottom)
{
	*marginLeft = 18.0f;
	*marginRight = 18.0f;
	*marginTop = 18.0f;
	*marginBottom = 18.0f;

	if (media == NULL || media[0] == '\0') {
		*width = 595.28f;		// A4
		*height = 841.89f;
		return true;
	}

	if (strncasecmp(media, "iso_a4", 6) == 0) {
		*width = 595.28f;
		*height = 841.89f;
		return true;
	}
	if (strncasecmp(media, "iso_a3", 6) == 0) {
		*width = 841.89f;
		*height = 1190.55f;
		return true;
	}
	if (strncasecmp(media, "iso_a5", 6) == 0) {
		*width = 419.53f;
		*height = 595.28f;
		return true;
	}
	if (strncasecmp(media, "us_letter", 9) == 0) {
		*width = 612.0f;
		*height = 792.0f;
		return true;
	}
	if (strncasecmp(media, "us_legal", 8) == 0) {
		*width = 612.0f;
		*height = 1008.0f;
		return true;
	}

	// Fall back to parsing WxH in mm or in from the IPP media name.
	const char* x = strchr(media, '_');
	if (x == NULL)
		x = strchr(media, 'x');
	if (x == NULL) {
		*width = 595.28f;
		*height = 841.89f;
		return true;
	}

	float w = atof(x + 1);
	const char* y = strchr(x + 1, 'x');
	float h = y != NULL ? atof(y + 1) : 0.0f;

	if (strstr(media, "mm") != NULL) {
		*width = w * 72.0f / 25.4f;
		*height = h * 72.0f / 25.4f;
	} else if (strstr(media, "in") != NULL) {
		*width = w * 72.0f;
		*height = h * 72.0f;
	} else if (w > 0 && h > 0) {
		// already in points
		*width = w;
		*height = h;
	} else {
		*width = 595.28f;
		*height = 841.89f;
	}

	if (*width < 72.0f || *height < 72.0f) {
		*width = 595.28f;
		*height = 841.89f;
	}

	return true;
}


// ----- CupsBridge -----

CupsBridge::CupsBridge()
{
}


CupsBridge::~CupsBridge()
{
}


void
CupsBridge::_FillFromDest(const void* destVoid, printcups_queue_info* info)
{
	const cups_dest_t* dest = static_cast<const cups_dest_t*>(destVoid);
	memset(info, 0, sizeof(*info));

	if (dest == NULL || dest->name == NULL)
		return;

	strlcpy(info->name, dest->name, sizeof(info->name));
	info->is_default = dest->is_default != 0;

	const char* uri = cupsGetOption("device-uri", dest->num_options,
		dest->options);
	if (uri != NULL)
		strlcpy(info->uri, uri, sizeof(info->uri));

	const char* makeModel = cupsGetOption("printer-make-and-model",
		dest->num_options, dest->options);
	if (makeModel != NULL)
		strlcpy(info->make_model, makeModel, sizeof(info->make_model));

	const char* state = cupsGetOption("printer-state", dest->num_options,
		dest->options);
	(void)state;

	// Colour: PPD/IPP "ColorDevice" or make/model heuristics.
	const char* colorDevice = cupsGetOption("ColorDevice", dest->num_options,
		dest->options);
	if (colorDevice != NULL && strcasecmp(colorDevice, "true") == 0)
		info->is_color = true;
	else if (makeModel != NULL
		&& (strcasestr(makeModel, "color") != NULL
			|| strcasestr(makeModel, "colour") != NULL
			|| strcasestr(makeModel, "inkjet") != NULL
			|| strcasestr(makeModel, "photosmart") != NULL))
		info->is_color = true;

	const char* media = cupsGetOption("media-default", dest->num_options,
		dest->options);
	if (media == NULL)
		media = cupsGetOption("media", dest->num_options, dest->options);
	if (media != NULL)
		strlcpy(info->media, media, sizeof(info->media));
	else
		strlcpy(info->media, "iso_a4_210x297mm", sizeof(info->media));

	MediaToHaikuName(info->media, info->media_name, sizeof(info->media_name));

	const char* resolution = cupsGetOption("printer-resolution",
		dest->num_options, dest->options);
	if (resolution != NULL)
		sscanf(resolution, "%dx%d", &info->xres, &info->yres);
	if (info->xres <= 0) {
		info->xres = 300;
		info->yres = 300;
	}

	const char* everywhere = cupsGetOption("printer-is-remote",
		dest->num_options, dest->options);
	if (everywhere != NULL && strcasecmp(everywhere, "true") == 0)
		info->is_everywhere = true;
	else if (info->uri != NULL && strstr(info->uri, "ipp://") != NULL)
		info->is_everywhere = true;

	MediaGeometry(info->media, &info->paper_width, &info->paper_height,
		&info->margin_left, &info->margin_right, &info->margin_top,
		&info->margin_bottom);
}


status_t
CupsBridge::ListQueues(printcups_queue_info* queues, int maxQueues)
{
	if (queues == NULL || maxQueues <= 0)
		return B_BAD_VALUE;

	cups_dest_t* dests = NULL;
	int count = cupsGetDests(&dests);
	if (count < 0)
		return B_ERROR;

	int written = 0;
	for (int i = 0; i < count && written < maxQueues; i++) {
		_FillFromDest(&dests[i], &queues[written]);
		written++;
	}

	cupsFreeDests(count, dests);
	return written;
}


status_t
CupsBridge::QueueInfo(const char* name, printcups_queue_info* info)
{
	if (name == NULL || info == NULL)
		return B_BAD_VALUE;

	// cupsGetDest() only searches an array the caller already holds.
	cups_dest_t* dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, name, NULL);
	if (dest == NULL)
		return B_ERROR;

	_FillFromDest(dest, info);
	cupsFreeDests(1, dest);
	return B_OK;
}


status_t
CupsBridge::DefaultQueue(char* name, size_t size)
{
	if (name == NULL || size == 0)
		return B_BAD_VALUE;

	name[0] = '\0';

	cups_dest_t* dests = NULL;
	int count = cupsGetDests(&dests);
	if (count <= 0)
		return B_ERROR;

	const char* def = cupsGetDefault();
	if (def != NULL)
		strlcpy(name, def, size);
	else {
		for (int i = 0; i < count; i++) {
			if (dests[i].is_default) {
				strlcpy(name, dests[i].name, size);
				break;
			}
		}
		if (name[0] == '\0' && count > 0)
			strlcpy(name, dests[0].name, size);
	}

	cupsFreeDests(count, dests);
	return name[0] != '\0' ? B_OK : B_ERROR;
}


status_t
CupsBridge::SetDefault(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	cups_dest_t* dests = NULL;
	int count = cupsGetDests(&dests);
	if (count <= 0)
		return B_ERROR;

	// Mark the chosen queue default and write dests back (lpadmin -d).
	cups_dest_t* target = NULL;
	for (int i = 0; i < count; i++) {
		if (dests[i].is_default)
			dests[i].is_default = 0;
		if (strcmp(dests[i].name, name) == 0) {
			dests[i].is_default = 1;
			target = &dests[i];
		}
	}

	if (target == NULL) {
		cupsFreeDests(count, dests);
		return B_ERROR;
	}

	cupsSetDests(count, dests);
	cupsFreeDests(count, dests);
	return B_OK;
}


status_t
CupsBridge::SubmitPdf(const char* filePath, BMessage* settings, int32* jobId)
{
	if (filePath == NULL || filePath[0] == '\0' || jobId == NULL)
		return B_BAD_VALUE;

	*jobId = -1;

	if (settings != NULL && settings->HasString("current_printer")) {
		const char* printer = NULL;
		if (settings->FindString("current_printer", &printer) == B_OK
			&& printer != NULL && printer[0] != '\0') {
			printcups_queue_info info;
			if (QueueInfo(printer, &info) != B_OK)
				return B_ERROR;
		}
	}

	char options[1024];
	options[0] = '\0';
	char buf[128];

	int32 copies = 1;
	if (settings != NULL)
		settings->FindInt32("copies", &copies);
	if (copies > 1) {
		snprintf(buf, sizeof(buf), "copies=%d ", copies);
		strlcat(options, buf, sizeof(options));
	}

	const char* media = MediaFromSettings(settings);
	snprintf(buf, sizeof(buf), "media=%s ", CupsMediaName(media));
	strlcat(options, buf, sizeof(options));

	int32 xres = 0;
	int32 yres = 0;
	if (settings != NULL) {
		int64 xr = 0;
		int64 yr = 0;
		if (settings->FindInt64("xres", &xr) == B_OK)
			xres = (int32)xr;
		if (settings->FindInt64("yres", &yr) == B_OK)
			yres = (int32)yr;
		if (xres <= 0 && settings->FindInt32("xres", &xres) != B_OK)
			xres = 0;
		if (yres <= 0 && settings->FindInt32("yres", &yres) != B_OK)
			yres = 0;
	}
	if (xres > 0 && yres > 0) {
		snprintf(buf, sizeof(buf), "Resolution=%ddpi ", xres);
		strlcat(options, buf, sizeof(options));
	}

	int32 quality = 100;
	if (settings != NULL)
		settings->FindInt32("quality", &quality);
	if (quality < 50)
		strlcat(options, "cupsPrintQuality=Draft ", sizeof(options));
	else if (quality < 90)
		strlcat(options, "cupsPrintQuality=Normal ", sizeof(options));
	else
		strlcat(options, "cupsPrintQuality=Best ", sizeof(options));

	int numOptions = 0;
	cups_option_t* cupsOptions = NULL;
	if (options[0] != '\0')
		numOptions = cupsParseOptions(options, 0, &cupsOptions);

	const char* dest = NULL;
	if (settings != NULL
		&& settings->FindString("current_printer", &dest) == B_OK
		&& dest != NULL && dest[0] != '\0') {
		// dest set
	} else
		dest = NULL;

	const char* jobName = "V\\OS print job";
	if (settings != NULL && settings->HasString("job_name")) {
		const char* n = NULL;
		if (settings->FindString("job_name", &n) == B_OK && n != NULL)
			jobName = n;
	}

	int id = cupsPrintFile(dest, filePath, jobName, numOptions, cupsOptions);
	if (cupsOptions != NULL)
		cupsFreeOptions(numOptions, cupsOptions);

	if (id < 0) {
		fprintf(stderr, "libprintcups: cupsPrintFile failed: %s\n",
			cupsLastErrorString());
		return B_ERROR;
	}

	*jobId = id;
	return B_OK;
}


status_t
CupsBridge::CancelJob(const char* queue, int32 jobId)
{
	if (queue == NULL || queue[0] == '\0' || jobId <= 0)
		return B_BAD_VALUE;

	if (cupsCancelJob(queue, jobId) != 1)
		return B_ERROR;

	return B_OK;
}


int
CupsBridge::ListJobs(const char* queue, printcups_job* jobs, int maxJobs)
{
	if (jobs == NULL || maxJobs <= 0)
		return -1;

	cups_job_t* cupJobs = NULL;
	const char* name = (queue != NULL && queue[0] != '\0') ? queue : NULL;
	int count = cupsGetJobs2(NULL, &cupJobs, name, 0, 1);
	if (count < 0)
		return -1;

	int written = 0;
	for (int i = 0; i < count && written < maxJobs; i++) {
		jobs[written].id = cupJobs[i].id;
		strlcpy(jobs[written].name,
			cupJobs[i].title != NULL ? cupJobs[i].title : "",
			sizeof(jobs[written].name));
		strlcpy(jobs[written].queue,
			cupJobs[i].dest != NULL ? cupJobs[i].dest : "",
			sizeof(jobs[written].queue));
		jobs[written].state = (int32)cupJobs[i].state;
		jobs[written].size = (int32)cupJobs[i].size;
		jobs[written].impressions = 0;
		written++;
	}

	free(cupJobs);
	return written;
}


status_t
CupsBridge::AddPrinterEverywhere(const char* name, const char* uri)
{
	if (name == NULL || name[0] == '\0' || uri == NULL || uri[0] == '\0')
		return B_BAD_VALUE;

	// lpadmin -p NAME -v URI -m everywhere -E
	// Equivalent IPP CUPS request: CUPS_ADD_PRINTER with everywhere model.
	http_t* http = httpConnect2(cupsServer(), ippPort(), NULL, AF_UNSPEC,
		HTTP_ENCRYPTION_IF_REQUESTED, 1, 30000, NULL);
	if (http == NULL)
		return B_ERROR;

	ipp_t* request = ippNewRequest(CUPS_ADD_PRINTER);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "printer-name",
		NULL, name);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "device-uri",
		NULL, uri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
		"ppd-name", NULL, "everywhere");
	ippAddBoolean(request, IPP_TAG_OPERATION, "printer-is-accepting-jobs",
		1);
	ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_ENUM,
		"printer-state", IPP_PRINTER_IDLE);

	// Adding a printer needs admin auth; rely on cups-files.conf DefaultAuthType.
	ipp_t* response = cupsDoRequest(http, request, "/admin/");
	httpClose(http);

	if (response == NULL)
		return B_ERROR;

	ipp_status_t status = ippGetStatusCode(response);
	ippDelete(response);
	return status == IPP_STATUS_OK ? B_OK : B_ERROR;
}
