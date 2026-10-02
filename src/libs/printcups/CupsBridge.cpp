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

#include <sys/socket.h>
#include <fcntl.h>

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <StorageDefs.h>

#include <cups/adminutil.h>
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


struct DeviceCollect {
	printcups_device*	devices;
	int					max;
	int					count;
};


static void
DeviceCollectCallback(const char* deviceClass, const char* deviceID,
	const char* deviceInfo, const char* makeAndModel, const char* deviceURI,
	const char* deviceLocation, void* userData)
{
	DeviceCollect* ctx = static_cast<DeviceCollect*>(userData);
	if (ctx == NULL || ctx->devices == NULL || deviceURI == NULL
		|| deviceURI[0] == '\0' || ctx->count >= ctx->max)
		return;

	printcups_device& d = ctx->devices[ctx->count];
	memset(&d, 0, sizeof(d));
	strlcpy(d.uri, deviceURI, sizeof(d.uri));
	if (makeAndModel != NULL)
		strlcpy(d.make_model, makeAndModel, sizeof(d.make_model));
	if (deviceInfo != NULL)
		strlcpy(d.info, deviceInfo, sizeof(d.info));
	if (deviceLocation != NULL)
		strlcpy(d.location, deviceLocation, sizeof(d.location));
	if (deviceClass != NULL)
		strlcpy(d.device_class, deviceClass, sizeof(d.device_class));

	// IPP Everywhere: ipp/ipps URIs that advertise application/pdf without
	// a PPD-backed make/model, or explicit ipp-everywhere in the device id.
	if (deviceClass != NULL && strcasecmp(deviceClass, "network") == 0
		&& (strncasecmp(deviceURI, "ipp://", 6) == 0
			|| strncasecmp(deviceURI, "ipps://", 7) == 0))
		d.everywhere = true;
	if (deviceID != NULL && strcasestr(deviceID, "ipp-everywhere") != NULL)
		d.everywhere = true;

	ctx->count++;
}


// Probe a device URI for IPP Everywhere (document-format includes PDF and
// no PPD is required). Used when the user picks a discovered device.
static bool
ProbeEverywhere(const char* uri)
{
	if (uri == NULL || uri[0] == '\0')
		return false;
	if (strncasecmp(uri, "ipp://", 6) != 0
		&& strncasecmp(uri, "ipps://", 7) != 0)
		return false;

	// Parse ipp(s)://host[:port]/resource
	const char* hostStart = strstr(uri, "//");
	if (hostStart == NULL)
		return false;
	hostStart += 2;
	const char* path = strchr(hostStart, '/');
	char host[256];
	size_t hostLen = path != NULL
		? (size_t)(path - hostStart) : strlen(hostStart);
	if (hostLen == 0 || hostLen >= sizeof(host))
		return false;
	memcpy(host, hostStart, hostLen);
	host[hostLen] = '\0';

	char resource[256] = "/";
	if (path != NULL && path[0] != '\0')
		strlcpy(resource, path, sizeof(resource));

	bool isSecure = strncasecmp(uri, "ipps://", 7) == 0;
	http_encryption_t enc = isSecure ? HTTP_ENCRYPTION_REQUIRED
		: HTTP_ENCRYPTION_IF_REQUESTED;
	http_t* http = httpConnect2(host, 631, NULL, AF_UNSPEC,
		enc, 1, 60000, NULL);
	if (http == NULL)
		return false;

	ipp_t* request = ippNewRequest(IPP_OP_GET_PRINTER_ATTRIBUTES);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
		NULL, uri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser());

	ipp_t* response = cupsDoRequest(http, request, resource);
	bool everywhere = false;
	if (response != NULL) {
		ipp_attribute_t* attr = ippFindAttribute(response,
			"document-format-supported", IPP_TAG_KEYWORD);
		if (attr != NULL) {
			for (int i = 0; i < ippGetCount(attr); i++) {
				const char* fmt = ippGetString(attr, i, NULL);
				if (fmt != NULL && strcmp(fmt, "application/pdf") == 0) {
					everywhere = true;
					break;
				}
			}
		}
		// Explicit IPP Everywhere marker when present.
		ipp_attribute_t* ie = ippFindAttribute(response, "ipp-everywhere",
			IPP_TAG_BOOLEAN);
		if (ie != NULL && ippGetBoolean(ie, 0))
			everywhere = true;
		ippDelete(response);
	}
	httpClose(http);
	return everywhere;
}


static ipp_t*
NewPrinterRequest(ipp_op_t op, const char* name)
{
	char printerUri[HTTP_MAX_URI];
	httpAssembleURIf(HTTP_URI_CODING_ALL, printerUri, sizeof(printerUri),
		"ipp", NULL, "localhost", 0, "/printers/%s", name);

	ipp_t* request = ippNewRequest(op);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
		NULL, printerUri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser());
	return request;
}


static ipp_t*
NewJobRequest(ipp_op_t op, const char* queue, int32 jobId)
{
	char printerUri[HTTP_MAX_URI];
	char jobUri[HTTP_MAX_URI];
	httpAssembleURIf(HTTP_URI_CODING_ALL, printerUri, sizeof(printerUri),
		"ipp", NULL, "localhost", 0, "/printers/%s", queue);
	httpAssembleURIf(HTTP_URI_CODING_ALL, jobUri, sizeof(jobUri),
		"ipp", NULL, "localhost", 0, "/jobs/%d", jobId);

	ipp_t* request = ippNewRequest(op);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
		NULL, printerUri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri",
		NULL, jobUri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser());
	return request;
}


static bool
IsForbidden(ipp_status_t status)
{
	return status == IPP_STATUS_ERROR_FORBIDDEN
		|| status == IPP_STATUS_ERROR_NOT_AUTHORIZED
		|| status == IPP_STATUS_ERROR_NOT_FOUND
		|| status == IPP_STATUS_ERROR_REQUEST_ENTITY;
}


static char sLastError[1024] = "";


// ----- CupsBridge -----

CupsBridge::CupsBridge()
{
}


CupsBridge::~CupsBridge()
{
}


void
CupsBridge::_SetError(const char* text)
{
	if (text == NULL)
		text = "";
	fLastError = text;
	strlcpy(sLastError, text, sizeof(sLastError));
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

	const char* location = cupsGetOption("printer-location", dest->num_options,
		dest->options);
	if (location != NULL)
		strlcpy(info->location, location, sizeof(info->location));

	const char* accepting = cupsGetOption("printer-is-accepting-jobs",
		dest->num_options, dest->options);
	info->accepting = accepting == NULL
		|| strcasecmp(accepting, "true") == 0;

	const char* state = cupsGetOption("printer-state", dest->num_options,
		dest->options);
	if (state != NULL) {
		if (strcasecmp(state, "idle") == 0)
			info->state = 3;
		else if (strcasecmp(state, "processing") == 0)
			info->state = 4;
		else if (strcasecmp(state, "stopped") == 0)
			info->state = 5;
	}

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

	// Driverless queues are the IPP Everywhere model or ipp(s) device URIs.
	const char* everywhere = cupsGetOption("printer-is-remote",
		dest->num_options, dest->options);
	if (everywhere != NULL && strcasecmp(everywhere, "true") == 0)
		info->is_everywhere = true;
	else if (info->uri[0] != '\0'
		&& (strncasecmp(info->uri, "ipp://", 6) == 0
			|| strncasecmp(info->uri, "ipps://", 7) == 0))
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
	if (count < 0) {
		_SetError(cupsLastErrorString());
		return B_ERROR;
	}

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
	if (dest == NULL) {
		_SetError(cupsLastErrorString());
		return B_ERROR;
	}

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
		_SetError("Printer not found");
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
		_SetError(cupsLastErrorString());
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

	if (cupsCancelJob(queue, jobId) != 1) {
		_SetError(cupsLastErrorString());
		return B_ERROR;
	}

	return B_OK;
}


int
CupsBridge::ListJobs(const char* queue, printcups_job* jobs, int maxJobs)
{
	if (jobs == NULL || maxJobs <= 0)
		return -1;

	cups_job_t* cupJobs = NULL;
	const char* name = (queue != NULL && queue[0] != '\0') ? queue : NULL;
	// which=0: every job, including completed ones.
	int count = cupsGetJobs2(NULL, &cupJobs, name, 0, 1);
	if (count < 0) {
		_SetError(cupsLastErrorString());
		return -1;
	}

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


static status_t
AdminRequest(ipp_t* request, BString* errorOut)
{
	// cupsd answers admin operations only to members of its @SYSTEM
	// group (lpadmin); everyone else gets IPP_STATUS_ERROR_FORBIDDEN.
	ipp_t* response = cupsDoRequest(CUPS_HTTP_DEFAULT, request, "/admin/");
	if (response == NULL) {
		const char* text = cupsLastErrorString();
		if (errorOut != NULL)
			*errorOut = text != NULL ? text : "";
		strlcpy(sLastError, text != NULL ? text : "", sizeof(sLastError));
		return B_ERROR;
	}

	ipp_status_t status = ippGetStatusCode(response);
	const char* statusMsg = cupsLastErrorString();
	if (statusMsg == NULL)
		statusMsg = "";
	ippDelete(response);
	if (status > IPP_STATUS_OK_CONFLICTING) {
		if (errorOut != NULL)
			*errorOut = statusMsg;
		strlcpy(sLastError, statusMsg, sizeof(sLastError));
		return IsForbidden(status) ? B_PERMISSION_DENIED : B_ERROR;
	}
	sLastError[0] = '\0';
	return B_OK;
}


status_t
CupsBridge::AddPrinter(const char* name, const char* uri, const char* model)
{
	if (name == NULL || name[0] == '\0' || uri == NULL || uri[0] == '\0')
		return B_BAD_VALUE;

	// Same request as lpadmin -p NAME -v URI -m MODEL -E.
	ipp_t* request = NewPrinterRequest(IPP_OP_CUPS_ADD_MODIFY_PRINTER, name);
	const char* ppd = (model == NULL || model[0] == '\0'
		|| strcasecmp(model, "everywhere") == 0)
		? "everywhere" : model;
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "ppd-name",
		NULL, ppd);
	ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_URI, "device-uri",
		NULL, uri);
	ippAddBoolean(request, IPP_TAG_PRINTER, "printer-is-accepting-jobs", 1);
	ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state",
		IPP_PSTATE_IDLE);
	return AdminRequest(request, &fLastError);
}


status_t
CupsBridge::RemovePrinter(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	return AdminRequest(NewPrinterRequest(IPP_OP_CUPS_DELETE_PRINTER, name),
		&fLastError);
}


status_t
CupsBridge::RenamePrinter(const char* name, const char* description,
	const char* location)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	ipp_t* request = NewPrinterRequest(IPP_OP_CUPS_ADD_MODIFY_PRINTER, name);
	if (description != NULL && description[0] != '\0')
		ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_NAME, "printer-info",
			NULL, description);
	if (location != NULL && location[0] != '\0')
		ippAddString(request, IPP_TAG_PRINTER, IPP_TAG_TEXT,
			"printer-location", NULL, location);
	return AdminRequest(request, &fLastError);
}


status_t
CupsBridge::SetAccepting(const char* name, bool accepting)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	ipp_t* request = NewPrinterRequest(IPP_OP_CUPS_ADD_MODIFY_PRINTER, name);
	ippAddBoolean(request, IPP_TAG_PRINTER, "printer-is-accepting-jobs",
		accepting ? 1 : 0);
	ippAddInteger(request, IPP_TAG_PRINTER, IPP_TAG_ENUM, "printer-state",
		accepting ? IPP_PSTATE_IDLE : IPP_PSTATE_STOPPED);
	return AdminRequest(request, &fLastError);
}


status_t
CupsBridge::SetOptions(const char* name, const printcups_options* options)
{
	if (name == NULL || name[0] == '\0' || options == NULL)
		return B_BAD_VALUE;

	cups_dest_t* dests = NULL;
	int count = cupsGetDests(&dests);
	if (count <= 0) {
		_SetError(cupsLastErrorString());
		return B_ERROR;
	}

	cups_dest_t* dest = cupsGetDest(name, NULL, count, dests);
	if (dest == NULL) {
		cupsFreeDests(count, dests);
		_SetError("Printer not found");
		return B_ERROR;
	}

	// Rebuild the dest option list with the new defaults, then cupsSetDests
	// writes them back the way lpoptions does.
	cups_option_t* newOptions = NULL;
	int newCount = 0;
	for (int i = 0; i < dest->num_options; i++) {
		newCount = cupsAddOption(dest->options[i].name,
			dest->options[i].value, newCount, &newOptions);
	}

	if (options->media[0] != '\0')
		newCount = cupsAddOption("media", CupsMediaName(options->media),
			newCount, &newOptions);
	if (options->sides[0] != '\0')
		newCount = cupsAddOption("sides", options->sides, newCount,
			&newOptions);
	if (options->color_mode[0] != '\0')
		newCount = cupsAddOption("print-color-mode", options->color_mode,
			newCount, &newOptions);
	if (options->quality > 0) {
		const char* quality = "normal";
		if (options->quality == 1)
			quality = "draft";
		else if (options->quality == 3)
			quality = "best";
		newCount = cupsAddOption("cupsPrintQuality", quality, newCount,
			&newOptions);
		newCount = cupsAddOption("media-quality", quality, newCount,
			&newOptions);
	}

	dest->options = newOptions;
	dest->num_options = newCount;
	cupsSetDests(count, dests);
	// cupsSetDests copies what it needs; free our rebuilt list via dest.
	cupsFreeDests(count, dests);
	return B_OK;
}


status_t
CupsBridge::PrintTestPage(const char* name)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	BPath temp;
	if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &temp) != B_OK
		&& find_directory(B_USER_CONFIG_DIRECTORY, &temp) != B_OK) {
		_SetError("No temporary directory");
		return B_ERROR;
	}

	BString path(temp.Path());
	path << "/vos-printers-test-page.pdf";
	BFile file(path.String(),
		B_WRITE_ONLY | B_CREATE_FILE | O_TRUNC);
	if (file.InitCheck() != B_OK) {
		_SetError("Could not create the test page file");
		return B_ERROR;
	}

	// Minimal one-page PDF; every CUPS filter chain understands PDF.
	static const char kTestPage[] =
		"%PDF-1.1\n"
		"1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n"
		"2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n"
		"3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 595 842]/Contents 4 0 R"
		"/Resources<<>>>>endobj\n"
		"4 0 obj<</Length 68>>stream\n"
		"BT /F1 24 Tf 72 720 Td (V\\OS printers test page) Tj ET\n"
		"endstream\nendobj\n"
		"trailer<</Root 1 0 R>>\n%%EOF\n";
	file.Write(kTestPage, sizeof(kTestPage) - 1);
	file.Flush();

	BMessage settings;
	settings.AddString("current_printer", name);
	settings.AddString("job_name", "V\\OS test page");
	int32 jobId = -1;
	status_t status = SubmitPdf(path.String(), &settings, &jobId);
	unlink(path.String());
	if (status != B_OK)
		_SetError(cupsLastErrorString());
	return status;
}


status_t
CupsBridge::HoldJob(const char* queue, int32 jobId)
{
	if (queue == NULL || queue[0] == '\0' || jobId <= 0)
		return B_BAD_VALUE;

	return AdminRequest(NewJobRequest(IPP_OP_HOLD_JOB, queue, jobId),
		&fLastError);
}


status_t
CupsBridge::ReleaseJob(const char* queue, int32 jobId)
{
	if (queue == NULL || queue[0] == '\0' || jobId <= 0)
		return B_BAD_VALUE;

	return AdminRequest(NewJobRequest(IPP_OP_RELEASE_JOB, queue, jobId),
		&fLastError);
}


status_t
CupsBridge::PurgeJobs(const char* queue)
{
	if (queue == NULL || queue[0] == '\0')
		return B_BAD_VALUE;

	// Cancel-Jobs on the printer URI drops every job on that queue.
	char printerUri[HTTP_MAX_URI];
	httpAssembleURIf(HTTP_URI_CODING_ALL, printerUri, sizeof(printerUri),
		"ipp", NULL, "localhost", 0, "/printers/%s", queue);
	ipp_t* request = ippNewRequest(IPP_OP_CANCEL_JOBS);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
		NULL, printerUri);
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
		"requesting-user-name", NULL, cupsUser());
	return AdminRequest(request, &fLastError);
}


const char*
printcups_cups_last_error()
{
	return sLastError;
}


int
CupsBridge::ListDevices(printcups_device* devices, int maxDevices)
{
	if (devices == NULL || maxDevices <= 0)
		return -1;

	DeviceCollect ctx;
	ctx.devices = devices;
	ctx.max = maxDevices;
	ctx.count = 0;

	// cupsGetDevices walks the cupsd backends (DNS-SD, USB, parallel...).
	ipp_status_t status = cupsGetDevices(CUPS_HTTP_DEFAULT, 10, NULL, NULL,
		DeviceCollectCallback, &ctx);
	if (status != IPP_STATUS_OK && ctx.count == 0) {
		_SetError(cupsLastErrorString());
		return -1;
	}

	// Refine IPP Everywhere for network devices that look like IPP.
	for (int i = 0; i < ctx.count; i++) {
		printcups_device& d = devices[i];
		if (d.everywhere && d.uri[0] != '\0'
			&& !ProbeEverywhere(d.uri))
			d.everywhere = false;
		else if (!d.everywhere && strncasecmp(d.uri, "ipp", 3) == 0)
			d.everywhere = ProbeEverywhere(d.uri);
	}
	return ctx.count;
}


int
CupsBridge::ListPPDs(const char* uri, printcups_ppd* ppds, int maxPpds)
{
	if (ppds == NULL || maxPpds <= 0)
		return -1;

	// cupsGetPPDs() left the public headers; CUPS-Get-PPDs is the same data
	// lpinfo -m shows. Empty response means cupsd has no driver list yet.
	ipp_t* request = ippNewRequest(IPP_OP_CUPS_GET_PPDS);
	if (uri != NULL && uri[0] != '\0') {
		char match[512];
		snprintf(match, sizeof(match), "ppd-device-id:%s", uri);
		ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_TEXT,
			"ppd-make-and-model", NULL, match);
	}

	ipp_t* response = cupsDoRequest(CUPS_HTTP_DEFAULT, request,
		"/admin/ppds/");
	if (response == NULL) {
		_SetError(cupsLastErrorString());
		return -1;
	}

	int written = 0;
	for (ipp_attribute_t* attr = ippFirstAttribute(response); attr != NULL;
		attr = ippNextAttribute(response)) {
		if (ippGetGroupTag(attr) != IPP_TAG_TEXT
			&& ippGetGroupTag(attr) != IPP_TAG_NAME)
			continue;
		const char* name = ippGetName(attr);
		if (name == NULL || strcmp(name, "ppd-name") != 0)
			continue;
		if (ippGetCount(attr) <= 0)
			continue;
		const char* value = ippGetString(attr, 0, NULL);
		if (value == NULL || written >= maxPpds)
			continue;
		strlcpy(ppds[written].name, value, sizeof(ppds[written].name));
		ppds[written].make_model[0] = '\0';
		written++;
	}

	// Second pass: pair each ppd-name with the make/model after it.
	{
		int idx = 0;
		const char* currentName = NULL;
		for (ipp_attribute_t* attr = ippFirstAttribute(response); attr != NULL;
			attr = ippNextAttribute(response)) {
			if (ippGetGroupTag(attr) != IPP_TAG_TEXT
				&& ippGetGroupTag(attr) != IPP_TAG_NAME)
				continue;
			const char* name = ippGetName(attr);
			if (name == NULL)
				continue;
			if (strcmp(name, "ppd-name") == 0 && ippGetCount(attr) > 0) {
				const char* value = ippGetString(attr, 0, NULL);
				if (value != NULL)
					currentName = value;
			} else if (strcmp(name, "ppd-make-and-model") == 0
				&& currentName != NULL && ippGetCount(attr) > 0) {
				const char* make = ippGetString(attr, 0, NULL);
				if (make != NULL) {
					for (int i = 0; i < written; i++) {
						if (strcmp(ppds[i].name, currentName) == 0) {
							strlcpy(ppds[i].make_model, make,
								sizeof(ppds[i].make_model));
							break;
						}
					}
				}
				(void)idx;
			}
		}
	}

	ippDelete(response);
	return written;
}


int
CupsBridge::ListChoices(const char* queue, const char* option,
	printcups_option_choice* choices, int maxChoices)
{
	if (queue == NULL || option == NULL || choices == NULL || maxChoices <= 0)
		return -1;

	cups_dest_t* dests = NULL;
	int count = cupsGetDests(&dests);
	if (count <= 0) {
		_SetError(cupsLastErrorString());
		return -1;
	}

	cups_dest_t* dest = cupsGetDest(queue, NULL, count, dests);
	if (dest == NULL) {
		cupsFreeDests(count, dests);
		_SetError("Printer not found");
		return -1;
	}

	int written = 0;

	if (strcmp(option, "media") == 0) {
		cups_dinfo_t* dinfo = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, dest);
		if (dinfo != NULL) {
			int mediaCount = cupsGetDestMediaCount(CUPS_HTTP_DEFAULT, dest,
				dinfo, CUPS_MEDIA_FLAGS_DEFAULT);
			for (int i = 0; i < mediaCount && written < maxChoices; i++) {
				cups_size_t size;
				memset(&size, 0, sizeof(size));
				if (cupsGetDestMediaByIndex(CUPS_HTTP_DEFAULT, dest, dinfo, i,
						CUPS_MEDIA_FLAGS_DEFAULT, &size) <= 0)
					continue;
				if (size.media[0] == '\0')
					continue;
				strlcpy(choices[written].value, size.media,
					sizeof(choices[written].value));
				MediaToHaikuName(size.media, choices[written].text,
					sizeof(choices[written].text));
				written++;
			}
			cupsFreeDestInfo(dinfo);
		}
	} else {
		char key[128];
		snprintf(key, sizeof(key), "%s-supported", option);
		const char* supported = cupsGetOption(key, dest->num_options,
			dest->options);
		if (supported == NULL && strcmp(option, "sides") == 0)
			supported = "one-sided two-sided-long-edge";
		if (supported == NULL && strcmp(option, "print-color-mode") == 0)
			supported = "color monochrome";
		if (supported != NULL) {
			char copy[512];
			strlcpy(copy, supported, sizeof(copy));
			char* save = NULL;
			for (char* token = strtok_r(copy, " \t", &save); token != NULL
				&& written < maxChoices;
				token = strtok_r(NULL, " \t", &save)) {
				strlcpy(choices[written].value, token,
					sizeof(choices[written].value));
				if (strcmp(token, "one-sided") == 0)
					strlcpy(choices[written].text, "One-sided",
						sizeof(choices[written].text));
				else if (strcmp(token, "two-sided-long-edge") == 0)
					strlcpy(choices[written].text, "Two-sided",
						sizeof(choices[written].text));
				else if (strcmp(token, "two-sided-short-edge") == 0)
					strlcpy(choices[written].text,
						"Two-sided (short edge)",
						sizeof(choices[written].text));
				else if (strcmp(token, "color") == 0)
					strlcpy(choices[written].text, "Colour",
						sizeof(choices[written].text));
				else if (strcmp(token, "monochrome") == 0)
					strlcpy(choices[written].text, "Greyscale",
						sizeof(choices[written].text));
				else if (strcmp(token, "auto") == 0)
					strlcpy(choices[written].text, "Automatic",
						sizeof(choices[written].text));
				else
					strlcpy(choices[written].text, token,
						sizeof(choices[written].text));
				written++;
			}
		}
	}

	cupsFreeDests(count, dests);
	return written;
}
