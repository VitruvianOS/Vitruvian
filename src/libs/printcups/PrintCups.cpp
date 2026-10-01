/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * PrintCups: private C API for V\OS printing. libbe dlopens
 * libprintcups.so on first BPrintJob use so libbe does not hard-link
 * libcups/cairo. There is no Haiku print_server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <AppDefs.h>
#include <Application.h>
#include <Bitmap.h>
#include <DataIO.h>
#include <File.h>
#include <List.h>
#include <Message.h>
#include <OS.h>
#include <Picture.h>
#include <Point.h>
#include <PrintJob.h>
#include <Rect.h>
#include <String.h>
#include <Window.h>

#include "CupsBridge.h"
#include "PrintCupsRender.h"
#include "SetupPanels.h"
#include "printcups.h"
#include "pr_server.h"


// Spool file layout written by BPrintJob (see PrintJob.cpp):
//   print_file_header
//   flattened BMessage settings
//   per page: _page_header_ then pictures as
//     BPoint where, BRect bounds, flattened BPicture
struct _page_header_ {
	int32 number_of_pictures;
	off_t next_page;
	int32 reserved[10];
} _PACKED;


SpooledPage*
printcups_new_page()
{
	return new SpooledPage;
}


void
printcups_add_picture(SpooledPage* page, BPicture* picture,
	const BRect& bounds, const BPoint& where)
{
	if (page == NULL || picture == NULL)
		return;

	SpooledPicture* pic = new SpooledPicture;
	pic->picture = picture;
	pic->bounds = bounds;
	pic->where = where;
	page->pictures.AddItem(pic);
}


void
printcups_free_page(SpooledPage* page)
{
	if (page == NULL)
		return;

	for (int32 i = 0; i < page->pictures.CountItems(); i++) {
		SpooledPicture* pic =
			static_cast<SpooledPicture*>(page->pictures.ItemAt(i));
		delete pic->picture;
		delete pic;
	}
	delete page;
}


static void
EnsureDefaults(BMessage* settings)
{
	if (settings == NULL)
		return;

	if (!settings->HasInt32(PSRV_FIELD_COPIES))
		settings->AddInt32(PSRV_FIELD_COPIES, 1);
	if (!settings->HasInt32(PSRV_FIELD_FIRST_PAGE))
		settings->AddInt32(PSRV_FIELD_FIRST_PAGE, 1);
	// Through the last page, as Haiku's "All".
	if (!settings->HasInt32(PSRV_FIELD_LAST_PAGE))
		settings->AddInt32(PSRV_FIELD_LAST_PAGE, INT32_MAX);
	if (!settings->HasInt32(PSRV_FIELD_SCALE))
		settings->AddInt32(PSRV_FIELD_SCALE, 100);
	if (!settings->HasInt32(PSRV_FIELD_QUALITY))
		settings->AddInt32(PSRV_FIELD_QUALITY, 100);
	if (!settings->HasInt64(PSRV_FIELD_XRES))
		settings->AddInt64(PSRV_FIELD_XRES, 300);
	if (!settings->HasInt64(PSRV_FIELD_YRES))
		settings->AddInt64(PSRV_FIELD_YRES, 300);
}


static status_t
ReadSpoolFile(const char* spoolPath, BList* pages, BMessage* settings,
	int32* pageCountOut)
{
	if (spoolPath == NULL || pages == NULL || settings == NULL)
		return B_BAD_VALUE;

	BFile file(spoolPath, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	print_file_header header;
	if (file.Read(&header, sizeof(header)) != (ssize_t)sizeof(header))
		return B_ERROR;
	if (header.page_count <= 0)
		return B_ERROR;

	if (settings->Unflatten(&file) != B_OK)
		return B_ERROR;

	EnsureDefaults(settings);

	int32 first = 1;
	int32 last = header.page_count;
	settings->FindInt32(PSRV_FIELD_FIRST_PAGE, &first);
	settings->FindInt32(PSRV_FIELD_LAST_PAGE, &last);
	if (first < 1)
		first = 1;
	if (last > header.page_count)
		last = header.page_count;
	if (last < first)
		return B_ERROR;

	int32 written = 0;
	for (int32 pageIndex = 1; pageIndex <= header.page_count; pageIndex++) {
		_page_header_ pageHeader;
		if (file.Read(&pageHeader, sizeof(pageHeader))
				!= (ssize_t)sizeof(pageHeader))
			break;

		if (pageIndex < first || pageIndex > last) {
			// Skip page contents.
			for (int32 p = 0; p < pageHeader.number_of_pictures; p++) {
				BPoint where;
				BRect bounds;
				if (file.Read(&where, sizeof(where)) != (ssize_t)sizeof(where)
					|| file.Read(&bounds, sizeof(bounds))
						!= (ssize_t)sizeof(bounds))
					return B_ERROR;
				BPicture picture;
				if (picture.Unflatten(&file) != B_OK)
					return B_ERROR;
			}
			continue;
		}

		SpooledPage* page = printcups_new_page();
		for (int32 p = 0; p < pageHeader.number_of_pictures; p++) {
			BPoint where;
			BRect bounds;
			if (file.Read(&where, sizeof(where)) != (ssize_t)sizeof(where)
				|| file.Read(&bounds, sizeof(bounds))
					!= (ssize_t)sizeof(bounds)) {
				printcups_free_page(page);
				return B_ERROR;
			}

			BPicture* picture = new BPicture;
			if (picture->Unflatten(&file) != B_OK) {
				delete picture;
				printcups_free_page(page);
				return B_ERROR;
			}
			printcups_add_picture(page, picture, bounds, where);
		}
		pages->AddItem(page);
		written++;
	}

	if (pageCountOut != NULL)
		*pageCountOut = written;

	return written > 0 ? B_OK : B_ERROR;
}


static status_t
RunModalWindow(BWindow* window, sem_id done)
{
	if (window == NULL || done < 0)
		return B_BAD_VALUE;

	// Wait like BAlert::Go(): a calling window keeps drawing meanwhile,
	// instead of leaving trails when the panel is dragged over it.
	BWindow* caller = dynamic_cast<BWindow*>(
		BLooper::LooperForThread(find_thread(NULL)));

	window->Show();

	status_t status;
	if (caller != NULL) {
		do {
			status = acquire_sem_etc(done, 1, B_RELATIVE_TIMEOUT, 50000);
			if (status == B_TIMED_OUT)
				caller->UpdateIfNeeded();
		} while (status == B_TIMED_OUT || status == B_INTERRUPTED);
	} else {
		do {
			status = acquire_sem(done);
		} while (status == B_INTERRUPTED);
	}

	delete_sem(done);
	return B_OK;
}


extern "C" {


int
printcups_list_queues(printcups_queue_info* queues, int maxQueues)
{
	CupsBridge bridge;
	return (int)bridge.ListQueues(queues, maxQueues);
}


status_t
printcups_queue_info_for(const char* name, printcups_queue_info* info)
{
	CupsBridge bridge;
	return bridge.QueueInfo(name, info);
}


status_t
printcups_default_queue(char* name, size_t size)
{
	CupsBridge bridge;
	return bridge.DefaultQueue(name, size);
}


status_t
printcups_default_settings(BMessage* settings)
{
	if (settings == NULL)
		return B_BAD_VALUE;

	CupsBridge bridge;
	printcups_queue_info info;

	char current[256];
	const char* name = NULL;
	if (settings->FindString(PSRV_FIELD_CURRENT_PRINTER, &name) == B_OK
		&& name != NULL)
		strlcpy(current, name, sizeof(current));
	else
		current[0] = '\0';
	if (current[0] == '\0') {
		if (bridge.DefaultQueue(current, sizeof(current)) != B_OK
			|| current[0] == '\0')
			return B_ERROR;
	}

	if (bridge.QueueInfo(current, &info) != B_OK)
		return B_ERROR;

	settings->RemoveName(PSRV_FIELD_CURRENT_PRINTER);
	settings->AddString(PSRV_FIELD_CURRENT_PRINTER, info.name);
	settings->RemoveName("printer_name");
	settings->AddString("printer_name", info.name);

	BRect paper(0, 0, info.paper_width - 1, info.paper_height - 1);
	BRect printable(info.margin_left, info.margin_top,
		paper.right - info.margin_right, paper.bottom - info.margin_bottom);
	settings->RemoveName(PSRV_FIELD_PAPER_RECT);
	settings->AddRect(PSRV_FIELD_PAPER_RECT, paper);
	settings->RemoveName(PSRV_FIELD_PRINTABLE_RECT);
	settings->AddRect(PSRV_FIELD_PRINTABLE_RECT, printable);

	settings->RemoveName(PSRV_FIELD_XRES);
	settings->AddInt64(PSRV_FIELD_XRES, info.xres);
	settings->RemoveName(PSRV_FIELD_YRES);
	settings->AddInt64(PSRV_FIELD_YRES, info.yres);
	settings->RemoveName("media");
	settings->AddString("media", info.media);

	EnsureDefaults(settings);
	return B_OK;
}


status_t
printcups_page_setup(BMessage* settings, bool* canceled)
{
	if (settings == NULL || canceled == NULL)
		return B_BAD_VALUE;

	*canceled = true;

	if (be_app == NULL)
		return B_ERROR;

	if (printcups_default_settings(settings) != B_OK)
		return B_ERROR;

	sem_id done = create_sem(0, "printcups_page_setup");
	if (done < 0)
		return B_ERROR;

	PageSetupWindow* window = new PageSetupWindow(settings, canceled, done);
	RunModalWindow(window, done);
	return *canceled ? B_CANCELED : B_OK;
}


status_t
printcups_job_setup(BMessage* settings, bool* canceled)
{
	if (settings == NULL || canceled == NULL)
		return B_BAD_VALUE;

	*canceled = true;

	if (be_app == NULL)
		return B_ERROR;

	// Apps size their pages from the printable rect; an empty one makes
	// StyledEdit's pagination loop forever.
	if (!settings->HasString(PSRV_FIELD_CURRENT_PRINTER)
		|| !settings->HasRect(PSRV_FIELD_PAPER_RECT)
		|| !settings->HasRect(PSRV_FIELD_PRINTABLE_RECT)) {
		if (printcups_default_settings(settings) != B_OK)
			return B_ERROR;
	}

	EnsureDefaults(settings);

	sem_id done = create_sem(0, "printcups_job_setup");
	if (done < 0)
		return B_ERROR;

	JobSetupWindow* window = new JobSetupWindow(settings, canceled, done);
	RunModalWindow(window, done);
	return *canceled ? B_CANCELED : B_OK;
}


status_t
printcups_submit_spool(const char* spoolPath, BMessage* settings,
	const char* jobName, int32* jobId)
{
	if (spoolPath == NULL || settings == NULL || jobId == NULL)
		return B_BAD_VALUE;

	*jobId = -1;

	CupsBridge bridge;
	char printer[256];
	const char* name = NULL;
	if (settings->FindString(PSRV_FIELD_CURRENT_PRINTER, &name) == B_OK
		&& name != NULL)
		strlcpy(printer, name, sizeof(printer));
	else
		printer[0] = '\0';
	if (printer[0] == '\0') {
		if (bridge.DefaultQueue(printer, sizeof(printer)) != B_OK
			|| printer[0] == '\0')
			return B_ERROR;
	}

	printcups_queue_info info;
	if (bridge.QueueInfo(printer, &info) != B_OK)
		return B_ERROR;

	BList pages;
	BMessage spoolSettings;
	int32 pageCount = 0;
	status_t status = ReadSpoolFile(spoolPath, &pages, &spoolSettings,
		&pageCount);
	if (status != B_OK)
		return status;

	// Merge caller settings over spool settings (caller wins on conflicts).
	BMessage merged(spoolSettings);
	{
		int32 copies = 1;
		int32 quality = 100;
		int32 first = 1;
		int32 last = pageCount;
		if (settings->FindInt32(PSRV_FIELD_COPIES, &copies) == B_OK) {
			merged.RemoveName(PSRV_FIELD_COPIES);
			merged.AddInt32(PSRV_FIELD_COPIES, copies);
		}
		if (settings->FindInt32(PSRV_FIELD_QUALITY, &quality) == B_OK) {
			merged.RemoveName(PSRV_FIELD_QUALITY);
			merged.AddInt32(PSRV_FIELD_QUALITY, quality);
		}
		if (settings->FindInt32(PSRV_FIELD_FIRST_PAGE, &first) == B_OK) {
			merged.RemoveName(PSRV_FIELD_FIRST_PAGE);
			merged.AddInt32(PSRV_FIELD_FIRST_PAGE, first);
		}
		if (settings->FindInt32(PSRV_FIELD_LAST_PAGE, &last) == B_OK) {
			merged.RemoveName(PSRV_FIELD_LAST_PAGE);
			merged.AddInt32(PSRV_FIELD_LAST_PAGE, last);
		}
		int64 xr = 0;
		int64 yr = 0;
		if (settings->FindInt64(PSRV_FIELD_XRES, &xr) == B_OK) {
			merged.RemoveName(PSRV_FIELD_XRES);
			merged.AddInt64(PSRV_FIELD_XRES, xr);
		}
		if (settings->FindInt64(PSRV_FIELD_YRES, &yr) == B_OK) {
			merged.RemoveName(PSRV_FIELD_YRES);
			merged.AddInt64(PSRV_FIELD_YRES, yr);
		}
		const char* media = NULL;
		if (settings->FindString("media", &media) == B_OK) {
			merged.RemoveName("media");
			merged.AddString("media", media);
		}
		merged.RemoveName(PSRV_FIELD_CURRENT_PRINTER);
		merged.AddString(PSRV_FIELD_CURRENT_PRINTER, info.name);
	}

	// Render to a temporary PDF next to the spool file.
	BString pdfPath(spoolPath);
	pdfPath << ".pdf";

	// Scale 72dpi == 1.0 (points).
	float scale = 1.0f;
	status = printcups_render_pages_pdf(pdfPath.String(), &pages,
		info.paper_width, info.paper_height, scale, jobName);

	// Free page pictures regardless of render result.
	for (int32 i = 0; i < pages.CountItems(); i++)
		printcups_free_page(static_cast<SpooledPage*>(pages.ItemAt(i)));
	pages.MakeEmpty();

	if (status != B_OK)
		return B_ERROR;

	merged.RemoveName("job_name");
	if (jobName != NULL && jobName[0] != '\0')
		merged.AddString("job_name", jobName);

	status = bridge.SubmitPdf(pdfPath.String(), &merged, jobId);

	// cupsPrintFile has sent the file to cupsd by now.
	unlink(pdfPath.String());
	return status;
}


int32
printcups_printer_type(const char* queue)
{
	CupsBridge bridge;
	printcups_queue_info info;
	const char* name = queue;
	if (name == NULL || name[0] == '\0') {
		if (bridge.DefaultQueue(info.name, sizeof(info.name)) != B_OK)
			return BPrintJob::B_COLOR_PRINTER;
		name = info.name;
	}

	if (bridge.QueueInfo(name, &info) != B_OK)
		return BPrintJob::B_COLOR_PRINTER;

	return info.is_color ? BPrintJob::B_COLOR_PRINTER : BPrintJob::B_BW_PRINTER;
}


status_t
printcups_set_default(const char* queue)
{
	CupsBridge bridge;
	return bridge.SetDefault(queue);
}


status_t
printcups_cancel_job(const char* queue, int32 jobId)
{
	CupsBridge bridge;
	return bridge.CancelJob(queue, jobId);
}


int
printcups_list_jobs(const char* queue, printcups_job* jobs, int maxJobs)
{
	CupsBridge bridge;
	return bridge.ListJobs(queue, jobs, maxJobs);
}


status_t
printcups_add_printer_everywhere(const char* name, const char* uri)
{
	CupsBridge bridge;
	return bridge.AddPrinterEverywhere(name, uri);
}


status_t
printcups_remove_printer(const char* name)
{
	CupsBridge bridge;
	return bridge.RemovePrinter(name);
}


}	// extern "C"
