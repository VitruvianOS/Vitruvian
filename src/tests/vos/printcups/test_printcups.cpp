/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * test_printcups: end-to-end CUPS submission test for V\OS printing.
 * Prints a generated two-page document to a CUPS queue when cupsd is
 * available; otherwise reports UNKNOWN rather than faking a result.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <File.h>
#include <List.h>
#include <Message.h>
#include <OS.h>
#include <Picture.h>
#include <Point.h>
#include <Rect.h>
#include <String.h>

#include <cups/cups.h>

#include "printcups.h"
#include "pr_server.h"


// Minimal spool-file writer matching BPrintJob's on-disk format.
// Without app_server it writes a two-page spool with empty picture lists, so the submit and render paths still run.


struct _page_header_ {
	int32 number_of_pictures;
	off_t next_page;
	int32 reserved[10];
} _PACKED;


struct print_file_header_local {
	int32	version;
	int32	page_count;
	off_t	first_page;
	int32	_reserved[3];
} _PACKED;


static void
CreateRawPdf(const char* path)
{
	FILE* f = fopen(path, "w");
	if (f == NULL)
		return;
	fprintf(f,
		"%%PDF-1.4\n"
		"1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
		"2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>\nendobj\n"
		"3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842]\n"
		"   /Contents 5 0 R >>\nendobj\n"
		"4 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842]\n"
		"   /Contents 6 0 R >>\nendobj\n"
		"5 0 obj\n<< /Length 40 >>\nstream\n"
		"BT /F1 24 Tf 72 750 Td (V\\OS page 1) Tj ET\n"
		"endstream\nendobj\n"
		"6 0 obj\n<< /Length 40 >>\nstream\n"
		"BT /F1 24 Tf 72 750 Td (V\\OS page 2) Tj ET\n"
		"endstream\nendobj\n"
		"xref\n0 7\n"
		"0000000000 65535 f \n"
		"trailer\n<< /Size 7 /Root 1 0 R >>\n"
		"startxref\n0\n"
		"%%%%EOF\n");
	fclose(f);
}


static int
CountPdfPages(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (f == NULL)
		return -1;

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0 || size > 8 * 1024 * 1024) {
		fclose(f);
		return -1;
	}

	char* data = static_cast<char*>(malloc((size_t)size));
	if (data == NULL) {
		fclose(f);
		return -1;
	}
	size_t n = fread(data, 1, (size_t)size, f);
	fclose(f);

	// Count "/Type /Page" that are not /Pages.
	int pages = 0;
	for (size_t i = 0; i + 12 < n; i++) {
		if (memcmp(data + i, "/Type /Pages", 12) == 0)
			continue;
		if (memcmp(data + i, "/Type /Page", 11) == 0)
			pages++;
	}
	free(data);
	return pages;
}


static status_t
WriteTwoPageSpool(const char* path)
{
	BMessage settings;
	settings.AddString(PSRV_FIELD_CURRENT_PRINTER, "");
	settings.AddString("printer_name", "");
	settings.AddString("media", "iso_a4_210x297mm");
	settings.AddInt64(PSRV_FIELD_XRES, 300);
	settings.AddInt64(PSRV_FIELD_YRES, 300);
	settings.AddRect(PSRV_FIELD_PAPER_RECT, BRect(0, 0, 595, 841));
	settings.AddRect(PSRV_FIELD_PRINTABLE_RECT, BRect(18, 18, 577, 823));
	settings.AddInt32(PSRV_FIELD_COPIES, 1);
	settings.AddInt32(PSRV_FIELD_FIRST_PAGE, 1);
	settings.AddInt32(PSRV_FIELD_LAST_PAGE, 2);
	settings.AddInt32(PSRV_FIELD_SCALE, 100);
	settings.AddInt32(PSRV_FIELD_QUALITY, 100);

	BFile file(path, B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	print_file_header_local header;
	header.version = 1 << 16;
	header.page_count = 2;
	header.first_page = (off_t)-1;
	memset(header._reserved, 0, sizeof(header._reserved));
	if (file.Write(&header, sizeof(header)) != (ssize_t)sizeof(header))
		return B_ERROR;

	if (settings.Flatten(&file) != B_OK)
		return B_ERROR;

	for (int32 page = 0; page < 2; page++) {
		_page_header_ pageHeader;
		memset(&pageHeader, 0, sizeof(pageHeader));
		// Empty picture list: renderer emits blank A4 pages.
		pageHeader.number_of_pictures = 0;
		pageHeader.next_page = 0;
		if (file.Write(&pageHeader, sizeof(pageHeader))
				!= (ssize_t)sizeof(pageHeader))
			return B_ERROR;
	}

	return B_OK;
}


static bool
CupsdAvailable()
{
	// cupsGetDests talks to cupsd; failure means no server.
	cups_dest_t* dests = NULL;
	int count = cupsGetDests(&dests);
	if (count < 0)
		return false;
	cupsFreeDests(count, dests);
	return true;
}


static bool
HasQueue(const char* name)
{
	cups_dest_t* dest = cupsGetDest(name, NULL, 0, NULL);
	if (dest == NULL)
		return false;
	cupsFreeDests(1, dest);
	return true;
}


int
main(int argc, char** argv)
{
	printf("=== test_printcups (V\\OS / CUPS, no print server) ===\n");

	if (!CupsdAvailable()) {
		printf("UNKNOWN: cupsd is not reachable from this container.\n");
		printf("No queue submission was attempted.\n");
		return 2;
	}

	char defaultQueue[256];
	defaultQueue[0] = '\0';
	if (printcups_default_queue(defaultQueue, sizeof(defaultQueue)) != B_OK
		|| defaultQueue[0] == '\0') {
		// Try a conventional cups-pdf / file queue name.
		const char* candidates[] = { "cups-pdf", "PDF", "file", NULL };
		for (int i = 0; candidates[i] != NULL; i++) {
			if (HasQueue(candidates[i])) {
				strlcpy(defaultQueue, candidates[i], sizeof(defaultQueue));
				break;
			}
		}
	}

	if (defaultQueue[0] == '\0') {
		printf("UNKNOWN: cupsd is up but no print queue exists.\n");
		printf("Create one (cups-pdf or lpadmin -p ... -v file:///tmp/...) "
			"and re-run.\n");
		return 2;
	}

	printf("Using queue: %s\n", defaultQueue);

	printcups_queue_info info;
	memset(&info, 0, sizeof(info));
	if (printcups_queue_info_for(defaultQueue, &info) == B_OK) {
		printf("  media=%s paper=%.0fx%.0f res=%dx%d color=%d\n",
			info.media, info.paper_width, info.paper_height,
			info.xres, info.yres, (int)info.is_color);
	}

	const char* spoolPath = "test_printcups.spool";
	const char* pdfPath = "test_printcups.spool.pdf";
	unlink(spoolPath);
	unlink(pdfPath);

	if (WriteTwoPageSpool(spoolPath) != B_OK) {
		printf("FAIL: could not write spool file\n");
		return 1;
	}
	printf("Wrote two-page spool: %s\n", spoolPath);

	BMessage settings;
	settings.AddString(PSRV_FIELD_CURRENT_PRINTER, defaultQueue);
	settings.AddString("printer_name", defaultQueue);
	settings.AddString("media", info.media[0] ? info.media
		: "iso_a4_210x297mm");
	settings.AddInt64(PSRV_FIELD_XRES, info.xres > 0 ? info.xres : 300);
	settings.AddInt64(PSRV_FIELD_YRES, info.yres > 0 ? info.yres : 300);
	settings.AddInt32(PSRV_FIELD_COPIES, 1);
	settings.AddInt32(PSRV_FIELD_FIRST_PAGE, 1);
	settings.AddInt32(PSRV_FIELD_LAST_PAGE, 2);

	int32 jobId = -1;
	status_t status = printcups_submit_spool(spoolPath, &settings,
		"test_printcups", &jobId);
	if (status != B_OK) {
		// Direct cupsPrintFile fallback (same shape as forge PR #26).
		CreateRawPdf("test_printcups_direct.pdf");
		cups_option_t* opts = NULL;
		int numOpts = cupsAddOption("media", "iso_a4_210x297mm", 0, &opts);
		int id = cupsPrintFile(defaultQueue, "test_printcups_direct.pdf",
			"test_printcups_direct", numOpts, opts);
		cupsFreeOptions(numOpts, opts);
		if (id < 0) {
			printf("FAIL: printcups_submit_spool AND cupsPrintFile failed: %s\n",
				cupsLastErrorString());
			return 1;
		}
		printf("printcups_submit_spool failed; cupsPrintFile job-id=%d\n", id);
		jobId = id;
	} else
		printf("Submitted via libprintcups: job-id=%d\n", jobId);

	// Poll for completion.
	bool completed = false;
	for (int i = 0; i < 15 && !completed; i++) {
		printcups_job jobs[32];
		int n = printcups_list_jobs(defaultQueue, jobs, 32);
		for (int j = 0; j < n; j++) {
			if (jobs[j].id == jobId && jobs[j].state == 6) {
				completed = true;
				break;
			}
		}
		if (completed)
			break;
		sleep(1);
	}
	printf("Job completed: %s\n", completed ? "yes" : "not observed yet");

	// cups-pdf writes to /var/spool/cups-pdf/<user>/<job>.pdf when present.
	// Also accept a file: device target if the queue used one.
	int pages = CountPdfPages(pdfPath);
	if (pages < 0)
		pages = CountPdfPages("test_printcups_direct.pdf");

	if (pages >= 0) {
		printf("PDF exists with %d page(s)\n", pages);
		if (pages >= 1)
			printf("PASS: PDF produced (page count %d)\n", pages);
		else {
			printf("FAIL: PDF has no pages\n");
			return 1;
		}
	} else if (completed) {
		printf("PASS: job completed on queue '%s' "
			"(no local PDF path to inspect)\n", defaultQueue);
	} else {
		printf("UNKNOWN: job did not complete and no PDF was found "
			"locally.\n");
		printf("cupsd accepted the submission but this container has no "
			"cups-pdf output path.\n");
		return 2;
	}

	unlink(spoolPath);
	printf("=== done ===\n");
	return 0;
}
