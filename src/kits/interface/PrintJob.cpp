/*
 * Copyright 2001-2009, Haiku.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		I.R. Adema
 *		Stefano Ceccherini (burton666@libero.it)
 *		Michael Pfeiffer
 *		julun <host.haiku@gmx.de>
 *
 * V\OS: BPrintJob talks to CUPS through private libprintcups, dlopen'd on first use.
 */


#include <PrintJob.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Debug.h>
#include <Entry.h>
#include <File.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <OS.h>
#include <Path.h>
#include <Region.h>
#include <Roster.h>
#include <SystemCatalog.h>
#include <View.h>

#include <AutoDeleter.h>
#include <image.h>
#include <pr_server.h>
#include <printcups.h>
#include <ViewPrivate.h>

using BPrivate::gSystemCatalog;

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrintJob"

#undef B_TRANSLATE
#define B_TRANSLATE(str) \
	gSystemCatalog->GetString(B_TRANSLATE_MARK(str), "PrintJob")


/*!	Summary of spool file:

		|-----------------------------------|
		|         print_file_header         |
		|-----------------------------------|
		|    BMessage print_job_settings    |
		|-----------------------------------|
		|                                   |
		| ********** (first page) ********* |
		| *                               * |
		| *         _page_header_         * |
		| * ----------------------------- * |
		| * |---------------------------| * |
		| * |       BPoint where        | * |
		| * |       BRect bounds        | * |
		| * |       BPicture pic        | * |
		| * |---------------------------| * |
		| * |---------------------------| * |
		| * |       BPoint where        | * |
		| * |       BRect bounds        | * |
		| * |       BPicture pic        | * |
		| * |---------------------------| * |
		| ********************************* |
		|                                   |
		| ********* (second page) ********* |
		| *                               * |
		| *         _page_header_         * |
		| * ----------------------------- * |
		| * |---------------------------| * |
		| * |       BPoint where        | * |
		| * |       BRect bounds        | * |
		| * |       BPicture pic        | * |
		| * |---------------------------| * |
		| ********************************* |
		|-----------------------------------|

	BeOS R5 print_file_header.version is 1 << 16
	BeOS R5 print_file_header.first_page is -1

	each page can consist of a collection of picture structures
	remaining pages start at _page_header_.next_page of previous _page_header_
*/


struct _page_header_ {
	int32 number_of_pictures;
	off_t next_page;
	int32 reserved[10];
} _PACKED;


static void
ShowError(const char* message)
{
	BAlert* alert = new BAlert(B_TRANSLATE("Error"), message, B_TRANSLATE("OK"));
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


// #pragma mark - printcups loader


namespace {


struct PrintCupsApi {
	status_t	(*PageSetup)(BMessage* settings, bool* canceled);
	status_t	(*JobSetup)(BMessage* settings, bool* canceled);
	status_t	(*DefaultSettings)(BMessage* settings);
	status_t	(*DefaultQueue)(char* name, size_t size);
	status_t	(*SubmitSpool)(const char* spoolPath, BMessage* settings,
					const char* jobName, int32* jobId);
	int32		(*PrinterType)(const char* queue);
	status_t	(*QueueInfoFor)(const char* name, void* info);
};


PrintCupsApi gPrintCupsApi;
image_id gPrintCupsImage = -2;	// -2 untried, -1 failed, >=0 loaded


status_t
_PrintCupsSymbol(const char* name, void** symbol)
{
	*symbol = NULL;
	if (gPrintCupsImage < 0)
		return B_ERROR;

	return get_image_symbol(gPrintCupsImage, name, B_SYMBOL_TYPE_TEXT,
		symbol) == B_OK ? B_OK : B_ERROR;
}


status_t
LoadPrintCups()
{
	if (gPrintCupsImage >= 0)
		return B_OK;
	if (gPrintCupsImage == -1)
		return B_ERROR;

	static const char* kPaths[] = {
		"/lib/libprintcups.so",
		"/system/lib/libprintcups.so",
		"libprintcups.so",
		NULL
	};

	for (int32 i = 0; kPaths[i] != NULL; i++) {
		gPrintCupsImage = load_add_on(kPaths[i]);
		if (gPrintCupsImage >= 0)
			break;
	}

	if (gPrintCupsImage < 0) {
		gPrintCupsImage = -1;
		return B_ERROR;
	}

	void* symbol = NULL;
	if (_PrintCupsSymbol("printcups_page_setup", &symbol) != B_OK
		|| _PrintCupsSymbol("printcups_job_setup", &symbol) != B_OK) {
		unload_add_on(gPrintCupsImage);
		gPrintCupsImage = -1;
		return B_ERROR;
	}

	gPrintCupsApi.PageSetup = reinterpret_cast<status_t (*)(BMessage*, bool*)>(
		symbol);
	if (_PrintCupsSymbol("printcups_job_setup", &symbol) == B_OK)
		gPrintCupsApi.JobSetup =
			reinterpret_cast<status_t (*)(BMessage*, bool*)>(symbol);
	if (_PrintCupsSymbol("printcups_default_settings", &symbol) == B_OK)
		gPrintCupsApi.DefaultSettings =
			reinterpret_cast<status_t (*)(BMessage*)>(symbol);
	if (_PrintCupsSymbol("printcups_default_queue", &symbol) == B_OK)
		gPrintCupsApi.DefaultQueue =
			reinterpret_cast<status_t (*)(char*, size_t)>(symbol);
	if (_PrintCupsSymbol("printcups_submit_spool", &symbol) == B_OK)
		gPrintCupsApi.SubmitSpool = reinterpret_cast<status_t (*)(const char*,
			BMessage*, const char*, int32*)>(symbol);
	if (_PrintCupsSymbol("printcups_printer_type", &symbol) == B_OK)
		gPrintCupsApi.PrinterType =
			reinterpret_cast<int32 (*)(const char*)>(symbol);

	if (gPrintCupsApi.PageSetup == NULL || gPrintCupsApi.JobSetup == NULL
		|| gPrintCupsApi.DefaultSettings == NULL
		|| gPrintCupsApi.DefaultQueue == NULL
		|| gPrintCupsApi.SubmitSpool == NULL) {
		unload_add_on(gPrintCupsImage);
		gPrintCupsImage = -1;
		memset(&gPrintCupsApi, 0, sizeof(gPrintCupsApi));
		return B_ERROR;
	}

	return B_OK;
}


}	// anonymous namespace


// #pragma mark -- BPrintJob


BPrintJob::BPrintJob(const char* jobName)
	:
	fPrintJobName(NULL),
	fSpoolFile(NULL),
	fError(B_NO_INIT),
	fSetupMessage(NULL),
	fDefaultSetupMessage(NULL),
	fAbort(0),
	fCurrentPageHeader(NULL)
{
	memset(&fSpoolFileHeader, 0, sizeof(print_file_header));

	if (jobName != NULL && jobName[0])
		fPrintJobName = strdup(jobName);

	fCurrentPageHeader = new _page_header_;
	if (fCurrentPageHeader != NULL)
		memset(fCurrentPageHeader, 0, sizeof(_page_header_));
}


BPrintJob::~BPrintJob()
{
	CancelJob();

	free(fPrintJobName);
	delete fSetupMessage;
	delete fDefaultSetupMessage;
	delete fCurrentPageHeader;
}


status_t
BPrintJob::ConfigPage()
{
	if (LoadPrintCups() != B_OK) {
		ShowError(B_TRANSLATE("Printing is not available (libprintcups)."));
		return B_ERROR;
	}

	if (fSetupMessage == NULL)
		fSetupMessage = new BMessage;

	bool canceled = false;
	status_t status = gPrintCupsApi.PageSetup(fSetupMessage, &canceled);
	if (status != B_OK && status != B_CANCELED) {
		ShowError(B_TRANSLATE("Could not open the page setup."));
		return status;
	}
	if (status == B_CANCELED || canceled)
		return B_CANCELED;

	_HandlePageSetup(fSetupMessage);
	return B_OK;
}


status_t
BPrintJob::ConfigJob()
{
	if (LoadPrintCups() != B_OK) {
		ShowError(B_TRANSLATE("Printing is not available (libprintcups)."));
		return B_ERROR;
	}

	if (fSetupMessage == NULL)
		fSetupMessage = new BMessage;

	if (!fSetupMessage->HasString(PSRV_FIELD_CURRENT_PRINTER)) {
		char printer[256];
		printer[0] = '\0';
		if (gPrintCupsApi.DefaultQueue(printer, sizeof(printer)) != B_OK
			|| printer[0] == '\0') {
			ShowError(B_TRANSLATE("No printer is configured."));
			return B_ERROR;
		}
		fSetupMessage->AddString(PSRV_FIELD_CURRENT_PRINTER, printer);
		fSetupMessage->AddString("printer_name", printer);
	}

	bool canceled = false;
	status_t status = gPrintCupsApi.JobSetup(fSetupMessage, &canceled);
	if (status != B_OK && status != B_CANCELED) {
		ShowError(B_TRANSLATE("Could not open the print setup."));
		return status;
	}
	if (status == B_CANCELED || canceled)
		return B_CANCELED;

	if (!_HandlePrintSetup(fSetupMessage))
		return B_ERROR;

	fError = B_OK;
	return B_OK;
}


void
BPrintJob::BeginJob()
{
	fError = B_ERROR;

	// can not start a new job until it has been commited or cancelled
	if (fSpoolFile != NULL || fCurrentPageHeader == NULL)
		return;

	if (fSetupMessage == NULL) {
		ShowError(B_TRANSLATE("Print settings are required."));
		return;
	}

	// create spool file
	BPath path;
	status_t status = find_directory(B_USER_PRINTERS_DIRECTORY, &path);
	if (status != B_OK)
		return;

	char* printer = _GetCurrentPrinterName();
	if (printer == NULL)
		return;
	MemoryDeleter _(printer);

	// No print server creates the per-printer spool folder on V\OS.
	path.Append(printer);
	if (path.InitCheck() != B_OK
		|| create_directory(path.Path(), 0755) != B_OK) {
		ShowError(B_TRANSLATE("Could not create the print spool folder."));
		return;
	}

	char mangledName[B_FILE_NAME_LENGTH];
	_GetMangledName(mangledName, B_FILE_NAME_LENGTH);

	path.Append(mangledName);
	if (path.InitCheck() != B_OK)
		return;

	strlcpy(fSpoolFileName, path.Path(), sizeof(fSpoolFileName));
	fSpoolFile = new BFile(fSpoolFileName, B_READ_WRITE | B_CREATE_FILE);

	if (fSpoolFile->InitCheck() != B_OK) {
		ShowError(B_TRANSLATE("Could not create the print spool file."));
		CancelJob();
		return;
	}

	// add print_file_header
	// page_count is updated in CommitJob()
	fSpoolFileHeader.version = 1 << 16;
	fSpoolFileHeader.page_count = 0;
	fSpoolFileHeader.first_page = (off_t)-1;

	if (fSpoolFile->Write(&fSpoolFileHeader, sizeof(print_file_header))
			!= sizeof(print_file_header)) {
		CancelJob();
		return;
	}

	// add printer settings message
	if (!fSetupMessage->HasString(PSRV_FIELD_CURRENT_PRINTER))
		fSetupMessage->AddString(PSRV_FIELD_CURRENT_PRINTER, printer);
	if (!fSetupMessage->HasString("printer_name"))
		fSetupMessage->AddString("printer_name", printer);

	_AddSetupSpec();
	_NewPage();

	// state variables
	fAbort = 0;
	fError = B_OK;
}


void
BPrintJob::CommitJob()
{
	if (fSpoolFile == NULL)
		return;

	if (fSpoolFileHeader.page_count == 0) {
		ShowError(B_TRANSLATE("No pages to print!"));
		CancelJob();
		return;
	}

	// update spool file
	_EndLastPage();

	// write spool file header
	fSpoolFile->Seek(0, SEEK_SET);
	fSpoolFile->Write(&fSpoolFileHeader, sizeof(print_file_header));

	// set file attributes
	app_info appInfo;
	if (be_app != NULL)
		be_app->GetAppInfo(&appInfo);
	else
		memset(&appInfo, 0, sizeof(appInfo));

	const char* printerName = "";
	fSetupMessage->FindString(PSRV_FIELD_CURRENT_PRINTER, &printerName);

	BNodeInfo info(fSpoolFile);
	info.SetType(PSRV_SPOOL_FILETYPE);

	fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_PAGECOUNT, B_INT32_TYPE, 0,
		&fSpoolFileHeader.page_count, sizeof(int32));
	if (fPrintJobName != NULL) {
		fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_DESCRIPTION, B_STRING_TYPE, 0,
			fPrintJobName, strlen(fPrintJobName) + 1);
	}
	fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_PRINTER, B_STRING_TYPE, 0,
		printerName, strlen(printerName) + 1);
	fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_STATUS, B_STRING_TYPE, 0,
		PSRV_JOB_STATUS_WAITING, strlen(PSRV_JOB_STATUS_WAITING) + 1);
	if (appInfo.signature[0] != '\0') {
		fSpoolFile->WriteAttr(PSRV_SPOOL_ATTR_MIMETYPE, B_STRING_TYPE, 0,
			appInfo.signature, strlen(appInfo.signature) + 1);
	}

	delete fSpoolFile;
	fSpoolFile = NULL;

	if (LoadPrintCups() != B_OK) {
		ShowError(B_TRANSLATE("Printing is not available (libprintcups)."));
		fError = B_ERROR;
		return;
	}

	int32 jobId = -1;
	status_t status = gPrintCupsApi.SubmitSpool(fSpoolFileName, fSetupMessage,
		fPrintJobName, &jobId);
	// Nothing on V\OS reads the spool file after this.
	BEntry(fSpoolFileName).Remove();
	if (status != B_OK) {
		ShowError(B_TRANSLATE("Could not submit the print job."));
		fError = B_ERROR;
		return;
	}

	fError = B_OK;
}


void
BPrintJob::CancelJob()
{
	if (fSpoolFile == NULL)
		return;

	fAbort = 1;
	BEntry(fSpoolFileName).Remove();
	delete fSpoolFile;
	fSpoolFile = NULL;
}


void
BPrintJob::SpoolPage()
{
	if (fSpoolFile == NULL)
		return;

	if (fCurrentPageHeader->number_of_pictures == 0)
		return;

	fSpoolFileHeader.page_count++;
	fSpoolFile->Seek(0, SEEK_END);
	if (fCurrentPageHeaderOffset) {
		// update last written page_header
		fCurrentPageHeader->next_page = fSpoolFile->Position();
		fSpoolFile->Seek(fCurrentPageHeaderOffset, SEEK_SET);
		fSpoolFile->Write(fCurrentPageHeader, sizeof(_page_header_));
		fSpoolFile->Seek(fCurrentPageHeader->next_page, SEEK_SET);
	}

	_NewPage();
}


bool
BPrintJob::CanContinue()
{
	// Check if our local error storage is still B_OK
	return fError == B_OK && !fAbort;
}


void
BPrintJob::DrawView(BView* view, BRect rect, BPoint where)
{
	if (fSpoolFile == NULL)
		return;

	if (view == NULL)
		return;

	if (view->LockLooper()) {
		BPicture picture;
		_RecurseView(view, B_ORIGIN - rect.LeftTop(), &picture, rect);
		_AddPicture(picture, rect, where);
		view->UnlockLooper();
	}
}


BMessage*
BPrintJob::Settings()
{
	if (fSetupMessage == NULL)
		return NULL;

	return new BMessage(*fSetupMessage);
}


void
BPrintJob::SetSettings(BMessage* message)
{
	if (message != NULL)
		_HandlePrintSetup(message);

	delete fSetupMessage;
	fSetupMessage = message;
}


bool
BPrintJob::IsSettingsMessageValid(BMessage* message) const
{
	char* printerName = _GetCurrentPrinterName();
	if (printerName == NULL)
		return false;

	const char* name = NULL;
	// The passed message is valid if it contains the right printer name.
	bool valid = message != NULL
		&& message->FindString("printer_name", &name) == B_OK
		&& strcmp(printerName, name) == 0;

	free(printerName);
	return valid;
}


// Either SetSettings() or ConfigPage() has to be called prior
// to any of the getters otherwise they return undefined values.
BRect
BPrintJob::PaperRect()
{
	if (fDefaultSetupMessage == NULL)
		_LoadDefaultSettings();

	return fPaperSize;
}


BRect
BPrintJob::PrintableRect()
{
	if (fDefaultSetupMessage == NULL)
		_LoadDefaultSettings();

	return fUsableSize;
}


void
BPrintJob::GetResolution(int32* xdpi, int32* ydpi)
{
	if (fDefaultSetupMessage == NULL)
		_LoadDefaultSettings();

	if (xdpi != NULL)
		*xdpi = fXResolution;

	if (ydpi != NULL)
		*ydpi = fYResolution;
}


int32
BPrintJob::FirstPage()
{
	return fFirstPage;
}


int32
BPrintJob::LastPage()
{
	return fLastPage;
}


int32
BPrintJob::PrinterType(void*) const
{
	if (LoadPrintCups() != B_OK)
		return B_COLOR_PRINTER;

	char printer[256];
	printer[0] = '\0';
	if (fSetupMessage != NULL) {
		const char* name = NULL;
		if (fSetupMessage->FindString(PSRV_FIELD_CURRENT_PRINTER, &name) == B_OK
			&& name != NULL)
			strlcpy(printer, name, sizeof(printer));
	}
	if (printer[0] == '\0' && gPrintCupsApi.DefaultQueue != NULL)
		gPrintCupsApi.DefaultQueue(printer, sizeof(printer));

	return gPrintCupsApi.PrinterType(printer);
}


// #pragma mark - private


void
BPrintJob::_RecurseView(BView* view, BPoint origin, BPicture* picture,
	BRect rect)
{
	ASSERT(picture != NULL);

	BRegion region;
	region.Set(BRect(rect.left, rect.top, rect.right, rect.bottom));
	view->fState->print_rect = rect;

	view->AppendToPicture(picture);
	view->PushState();
	view->SetOrigin(origin);
	view->ConstrainClippingRegion(&region);

	if (view->ViewColor() != B_TRANSPARENT_COLOR) {
		rgb_color highColor = view->HighColor();
		view->SetHighColor(view->ViewColor());
		view->FillRect(rect);
		view->SetHighColor(highColor);
	}

	if ((view->Flags() & B_WILL_DRAW) != 0) {
		view->fIsPrinting = true;
		view->Draw(rect);
		view->fIsPrinting = false;
	}

	view->PopState();
	view->EndPicture();

	BView* child = view->ChildAt(0);
	while (child != NULL) {
		if (!child->IsHidden()) {
			BPoint leftTop(view->Bounds().LeftTop() + child->Frame().LeftTop());
			BRect printRect(rect.OffsetToCopy(rect.LeftTop() - leftTop)
				& child->Bounds());
			if (printRect.IsValid())
				_RecurseView(child, origin + leftTop, picture, printRect);
		}
		child = child->NextSibling();
	}

	if ((view->Flags() & B_DRAW_ON_CHILDREN) != 0) {
		view->AppendToPicture(picture);
		view->PushState();
		view->SetOrigin(origin);
		view->ConstrainClippingRegion(&region);
		view->fIsPrinting = true;
		view->DrawAfterChildren(rect);
		view->fIsPrinting = false;
		view->PopState();
		view->EndPicture();
	}
}


void
BPrintJob::_GetMangledName(char* buffer, size_t bufferSize) const
{
	snprintf(buffer, bufferSize, "%s@%" B_PRId64, fPrintJobName,
		system_time() / 1000);
}


void
BPrintJob::_HandlePageSetup(BMessage* setup)
{
	setup->FindRect(PSRV_FIELD_PRINTABLE_RECT, &fUsableSize);
	setup->FindRect(PSRV_FIELD_PAPER_RECT, &fPaperSize);

	// libprintcups stores resolution as int64; older code used int64 too.
	int64 valueInt64;
	if (setup->FindInt64(PSRV_FIELD_XRES, &valueInt64) == B_OK)
		fXResolution = (short)valueInt64;

	if (setup->FindInt64(PSRV_FIELD_YRES, &valueInt64) == B_OK)
		fYResolution = (short)valueInt64;
}


bool
BPrintJob::_HandlePrintSetup(BMessage* message)
{
	_HandlePageSetup(message);

	bool valid = true;
	if (message->FindInt32(PSRV_FIELD_FIRST_PAGE, &fFirstPage) != B_OK)
		valid = false;

	if (message->FindInt32(PSRV_FIELD_LAST_PAGE, &fLastPage) != B_OK)
		valid = false;

	return valid;
}


void
BPrintJob::_NewPage()
{
	// init, write new page_header
	fCurrentPageHeader->next_page = 0;
	fCurrentPageHeader->number_of_pictures = 0;
	fCurrentPageHeaderOffset = fSpoolFile->Position();
	fSpoolFile->Write(fCurrentPageHeader, sizeof(_page_header_));
}


void
BPrintJob::_EndLastPage()
{
	if (!fSpoolFile)
		return;

	if (fCurrentPageHeader->number_of_pictures == 0)
		return;

	fSpoolFileHeader.page_count++;
	fSpoolFile->Seek(0, SEEK_END);
	if (fCurrentPageHeaderOffset) {
		fCurrentPageHeader->next_page = 0;
		fSpoolFile->Seek(fCurrentPageHeaderOffset, SEEK_SET);
		fSpoolFile->Write(fCurrentPageHeader, sizeof(_page_header_));
		fSpoolFile->Seek(0, SEEK_END);
	}
}


void
BPrintJob::_AddSetupSpec()
{
	fSetupMessage->Flatten(fSpoolFile);
}


void
BPrintJob::_AddPicture(BPicture& picture, BRect& rect, BPoint& where)
{
	ASSERT(fSpoolFile != NULL);

	fCurrentPageHeader->number_of_pictures++;
	fSpoolFile->Write(&where, sizeof(BPoint));
	fSpoolFile->Write(&rect, sizeof(BRect));
	picture.Flatten(fSpoolFile);
}


/*!	Returns a copy of the applications default printer name or NULL if it
	could not be obtained. Caller is responsible to free the string using
	free().
*/
char*
BPrintJob::_GetCurrentPrinterName() const
{
	if (fSetupMessage != NULL) {
		const char* name = NULL;
		if (fSetupMessage->FindString(PSRV_FIELD_CURRENT_PRINTER, &name) == B_OK
			&& name != NULL && name[0] != '\0')
			return strdup(name);
	}

	if (LoadPrintCups() != B_OK)
		return NULL;

	char printer[256];
	printer[0] = '\0';
	if (gPrintCupsApi.DefaultQueue(printer, sizeof(printer)) != B_OK
		|| printer[0] == '\0')
		return NULL;

	return strdup(printer);
}


void
BPrintJob::_LoadDefaultSettings()
{
	if (LoadPrintCups() != B_OK)
		return;

	BMessage* reply = new BMessage;
	if (gPrintCupsApi.DefaultSettings(reply) != B_OK) {
		delete reply;
		return;
	}

	// Only override our settings if we don't have any settings yet
	if (fSetupMessage == NULL)
		_HandlePrintSetup(reply);

	delete fDefaultSetupMessage;
	fDefaultSetupMessage = reply;
}


void BPrintJob::_ReservedPrintJob1() {}
void BPrintJob::_ReservedPrintJob2() {}
void BPrintJob::_ReservedPrintJob3() {}
void BPrintJob::_ReservedPrintJob4() {}
