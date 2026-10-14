/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Printers and scanners preflet: CUPS queues and SANE devices in one list.
 * cupsd and SANE errors are shown in the window.
 */

#include "PrintersWindow.h"

#include <ctype.h>
#include <string.h>

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <StringView.h>

#include "AddPrinterDialog.h"
#include "Messages.h"
#include "PrinterOptionsDialog.h"
#include "PrinterWorker.h"
#include "libscan.h"
#include "printcups.h"
// See PrintersApp.cpp: the tracker kit's pr_server.h shadows the print one.
#include <print/pr_server.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Printers"


enum {
	kPrinterColumn = 0,
	kLocationColumn,
	kStatusColumn,
	kFunctionsColumn,
};

enum {
	kJobIdColumn = 0,
	kJobNameColumn,
	kJobStateColumn,
	kJobQueueColumn,
};

enum {
	kScannerNameColumn = 0,
	kScannerModelColumn,
	kScannerTypeColumn,
};


// Snapshot filled from worker results so refreshes rebuild lists in place.
static printcups_queue_info gQueues[64];
static int32 gQueueCount = 0;
static printcups_job gJobs[128];
static int32 gJobCount = 0;
static libscan_device gScanners[32];
static int32 gScannerCount = 0;

static const char* kSettingsFile = "Printers settings";
static const char* kDefaultScannerKey = "default_scanner";


static void
ClearList(BColumnListView* list)
{
	while (list->CountRows() > 0)
		list->RemoveRow(list->RowAt(0));
}


static bool
ContainsIgnoreCase(const char* haystack, const char* needle)
{
	if (haystack == NULL || needle == NULL || needle[0] == '\0')
		return false;
	size_t nlen = strlen(needle);
	for (const char* p = haystack; *p != '\0'; p++) {
		size_t i = 0;
		while (i < nlen && p[i] != '\0'
			&& tolower((unsigned char)p[i])
				== tolower((unsigned char)needle[i]))
			i++;
		if (i == nlen)
			return true;
	}
	return false;
}


PrinterRow::PrinterRow(const printcups_queue_info& info,
		const char* functions)
	:
	BRow(),
	fName(info.name),
	fDefault(info.is_default)
{
	BString name(info.name);
	if (info.make_model[0] != '\0')
		name << " (" << info.make_model << ")";
	if (info.is_default)
		name << " " << B_TRANSLATE("[default]");
	SetField(new BStringField(name.String()), kPrinterColumn);
	SetField(new BStringField(info.location), kLocationColumn);
	SetField(new BStringField(StatusText(info)), kStatusColumn);
	SetField(new BStringField(functions != NULL ? functions : ""),
		kFunctionsColumn);
}


const char*
PrinterRow::StatusText(const printcups_queue_info& info)
{
	if (!info.accepting)
		return B_TRANSLATE("disabled");
	if (info.state == 4)
		return B_TRANSLATE("printing");
	if (info.state == 5)
		return B_TRANSLATE("stopped");
	return B_TRANSLATE("idle");
}


JobRow::JobRow(const printcups_job& job)
	:
	BRow(),
	fId(job.id),
	fQueue(job.queue)
{
	BString id;
	id << job.id;
	SetField(new BStringField(id.String()), kJobIdColumn);
	SetField(new BStringField(job.name), kJobNameColumn);
	SetField(new BStringField(StateText(job.state)), kJobStateColumn);
	SetField(new BStringField(job.queue), kJobQueueColumn);
}


const char*
JobRow::StateText(int32 state)
{
	switch (state) {
		case 0: return B_TRANSLATE("pending");
		case 1: return B_TRANSLATE("held");
		case 2: return B_TRANSLATE("printing");
		case 3: return B_TRANSLATE("stopped");
		case 4: return B_TRANSLATE("canceled");
		case 5: return B_TRANSLATE("aborted");
		case 6: return B_TRANSLATE("completed");
		default: return B_TRANSLATE("unknown");
	}
}


PrintersWindow::PrintersWindow()
	:
	BWindow(BRect(60, 60, 760, 640),
		B_TRANSLATE("Printers and scanners"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE),
	fBusy(false)
{
	_SetupLists();

	fScannerDefaultMenu = new BPopUpMenu("defaultScanner");
	fScannerDefaultField = new BMenuField("defaultScannerField",
		B_TRANSLATE("Default scanner:"), fScannerDefaultMenu);
	fScanButton = new BButton("scan", B_TRANSLATE("Scan..."),
		new BMessage(kMsgScanScanner));

	fAddButton = new BButton("add", B_TRANSLATE("Add Printer..."),
		new BMessage(kMsgAddPrinter));
	fRemoveButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemovePrinter));
	fDefaultButton = new BButton("default", B_TRANSLATE("Set Default"),
		new BMessage(kMsgMakeDefaultPrinter));
	fRenameButton = new BButton("rename", B_TRANSLATE("Rename..."),
		new BMessage(kMsgRenamePrinter));
	fEnableButton = new BButton("enable", B_TRANSLATE("Enable"),
		new BMessage(kMsgEnablePrinter));
	fDisableButton = new BButton("disable", B_TRANSLATE("Disable"),
		new BMessage(kMsgDisablePrinter));
	fTestPageButton = new BButton("test", B_TRANSLATE("Test Page"),
		new BMessage(kMsgPrintTestPage));
	fOptionsButton = new BButton("options", B_TRANSLATE("Options..."),
		new BMessage(kMsgPrinterOptions));
	fRefreshButton = new BButton("refresh", B_TRANSLATE("Refresh"),
		new BMessage(kMsgRefresh));

	fCancelButton = new BButton("canceljob", B_TRANSLATE("Cancel Job"),
		new BMessage(kMsgCancelJob));
	fHoldButton = new BButton("holdjob", B_TRANSLATE("Hold"),
		new BMessage(kMsgHoldJob));
	fReleaseButton = new BButton("releasejob", B_TRANSLATE("Release"),
		new BMessage(kMsgReleaseJob));
	fPurgeButton = new BButton("purgejobs", B_TRANSLATE("Purge All Jobs"),
		new BMessage(kMsgPurgeJobs));

	fStatusView = new BStringView("status", "");

	BBox* printerBox = new BBox("printerBox");
	printerBox->SetLabel(B_TRANSLATE("Printers"));
	BLayoutBuilder::Group<>(printerBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fPrinterList, 2)
		.AddGroup(B_HORIZONTAL)
			.Add(fAddButton)
			.Add(fRemoveButton)
			.Add(fDefaultButton)
			.Add(fRenameButton)
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fEnableButton)
			.Add(fDisableButton)
			.Add(fTestPageButton)
			.Add(fOptionsButton)
			.AddGlue()
			.Add(fRefreshButton)
		.End();

	BBox* scannerBox = new BBox("scannerBox");
	scannerBox->SetLabel(B_TRANSLATE("Scanners"));
	BLayoutBuilder::Group<>(scannerBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fScannerList, 1)
		.AddGroup(B_HORIZONTAL)
			.Add(fScannerDefaultField)
			.AddGlue()
			.Add(fScanButton)
		.End();

	BBox* jobBox = new BBox("jobBox");
	jobBox->SetLabel(B_TRANSLATE("Jobs"));
	BLayoutBuilder::Group<>(jobBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fJobList, 2)
		.AddGroup(B_HORIZONTAL)
			.Add(fCancelButton)
			.Add(fHoldButton)
			.Add(fReleaseButton)
			.AddGlue()
			.Add(fPurgeButton)
		.End();

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(printerBox, 2)
		.Add(scannerBox, 1)
		.Add(jobBox, 2)
		.Add(fStatusView);

	_UpdateButtons();
	CenterOnScreen();

	_LoadDefaultScanner();
	// cupsd and SANE can be slow; load off the window thread.
	_LoadQueues();
	_LoadScanners();
}


void
PrintersWindow::_SetupLists()
{
	const float scale = be_plain_font->Size() / 12.0f;

	fPrinterList = new BColumnListView("printers",
		B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER, false);
	fPrinterList->SetSelectionMessage(new BMessage(kMsgPrinterSelected));
	fPrinterList->AddColumn(new BStringColumn(B_TRANSLATE("Printer"),
	fPrinterList->AddColumn(new BStringColumn(B_TRANSLATE("Printer"),
		200 * scale, 120 * scale, 400 * scale, B_TRUNCATE_END), kPrinterColumn);
	fPrinterList->AddColumn(new BStringColumn(B_TRANSLATE("Location"),
		140 * scale, 80 * scale, 260 * scale, B_TRUNCATE_END), kLocationColumn);
	fPrinterList->AddColumn(new BStringColumn(B_TRANSLATE("Status"),
		100 * scale, 60 * scale, 160 * scale, B_TRUNCATE_END), kStatusColumn);
	fPrinterList->AddColumn(new BStringColumn(B_TRANSLATE("Functions"),
		130 * scale, 80 * scale, 220 * scale, B_TRUNCATE_END), kFunctionsColumn);
	fPrinterList->SetSortingEnabled(true);

	fScannerList = new BListView("scanners");
	fScannerList->SetSelectionMessage(new BMessage(kMsgScannerSelected));

	fJobList = new BColumnListView("jobs",
		B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER, false);
	fJobList->SetSelectionMessage(new BMessage(kMsgJobSelected));
	fJobList->AddColumn(new BIntegerColumn(B_TRANSLATE("ID"),
		50 * scale, 40 * scale, 80 * scale, B_ALIGN_LEFT), kJobIdColumn);
	fJobList->AddColumn(new BStringColumn(B_TRANSLATE("Job"),
		180 * scale, 100 * scale, 320 * scale, B_TRUNCATE_END), kJobNameColumn);
	fJobList->AddColumn(new BStringColumn(B_TRANSLATE("State"),
		90 * scale, 60 * scale, 140 * scale, B_TRUNCATE_END), kJobStateColumn);
	fJobList->AddColumn(new BStringColumn(B_TRANSLATE("Queue"),
		140 * scale, 80 * scale, 240 * scale, B_TRUNCATE_END), kJobQueueColumn);
	fJobList->SetSortingEnabled(true);
}


void
PrintersWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgWorkerResult:
		{
			int32 op = 0;
			int32 status = B_OK;
			BString error;
			message->FindInt32("op", &op);
			message->FindInt32("status", &status);
			message->FindString("error", &error);

			switch (op) {
				case kWorkerOpListQueues:
				{
					fBusy = false;
					const void* data = NULL;
					ssize_t size = 0;
					int32 count = 0;
					message->FindInt32("count", &count);
					gQueueCount = 0;
					if (status == B_OK
						&& message->FindData("queues", B_RAW_TYPE, &data,
							&size) == B_OK) {
						int32 n = size / (int32)sizeof(printcups_queue_info);
						if (n > 64)
							n = 64;
						if (n > count)
							n = count;
						memcpy(gQueues, data,
							n * sizeof(printcups_queue_info));
						gQueueCount = n;
					}
					if (status == B_OK) {
						_UpdateQueues();
						_SetStatus(B_TRANSLATE("Ready"));
					} else
						_SetError(error, status);
					_UpdateButtons();
					break;
				}

				case kWorkerOpListScanners:
				{
					fBusy = false;
					const void* data = NULL;
					ssize_t size = 0;
					int32 count = 0;
					message->FindInt32("count", &count);
					gScannerCount = 0;
					if (status == B_OK
						&& message->FindData("scanners", B_RAW_TYPE, &data,
							&size) == B_OK) {
						int32 n = size / (int32)sizeof(libscan_device);
						if (n > 32)
							n = 32;
						if (n > count)
							n = count;
						memcpy(gScanners, data,
							n * sizeof(libscan_device));
						gScannerCount = n;
					}
					_UpdateScanners();
					if (status != B_OK && !error.IsEmpty())
						_SetStatus(error.String());
					_UpdateButtons();
					break;
				}

				case kWorkerOpListJobs:
				{
					fBusy = false;
					const void* data = NULL;
					ssize_t size = 0;
					int32 count = 0;
					message->FindInt32("count", &count);
					gJobCount = 0;
					if (status == B_OK
						&& message->FindData("jobs", B_RAW_TYPE, &data,
							&size) == B_OK) {
						int32 n = size / (int32)sizeof(printcups_job);
						if (n > 128)
							n = 128;
						if (n > count)
							n = count;
						memcpy(gJobs, data,
							n * sizeof(printcups_job));
						gJobCount = n;
					}
					if (status == B_OK)
						_UpdateJobs();
					else if (!error.IsEmpty())
						_SetStatus(error.String());
					_UpdateButtons();
					break;
				}

				case kWorkerOpSetDefault:
				case kWorkerOpRemovePrinter:
				case kWorkerOpRenamePrinter:
				case kWorkerOpSetAccepting:
				case kWorkerOpSetOptions:
				case kWorkerOpTestPage:
				case kWorkerOpCancelJob:
				case kWorkerOpHoldJob:
				case kWorkerOpReleaseJob:
				case kWorkerOpPurgeJobs:
				case kWorkerOpAddPrinter:
				{
					fBusy = false;
					if (status == B_OK) {
						_SetStatus(B_TRANSLATE("Done"));
						_LoadQueues();
						_LoadJobs();
					} else
						_SetError(error, status);
					_UpdateButtons();
					break;
				}

				default:
					fBusy = false;
					_UpdateButtons();
					break;
			}
			break;
		}

		case kMsgPrinterSelected:
			_UpdateJobs();
			_UpdateButtons();
			break;

		case kMsgJobSelected:
			_UpdateButtons();
			break;

		case kMsgScannerSelected:
			_UpdateButtons();
			break;

		case kMsgScanScanner:
			_DoScan();
			break;

		case kMsgDefaultScanner:
		{
			const char* name = NULL;
			if (message->FindString("name", &name) == B_OK && name != NULL)
				_SaveDefaultScanner(name);
			break;
		}

		case kMsgMakeDefaultPrinter:
			_DoDefault();
			break;

		case kMsgCancelJob:
			_DoCancelJob();
			break;

		case kMsgHoldJob:
			_DoHoldJob();
			break;

		case kMsgReleaseJob:
			_DoReleaseJob();
			break;

		case kMsgPurgeJobs:
			_DoPurgeJobs();
			break;

		case kMsgAddPrinter:
			_DoAddPrinter();
			break;

		case kMsgRemovePrinter:
			_DoRemove();
			break;

		case kMsgRenamePrinter:
			_DoRename();
			break;

		case kMsgEnablePrinter:
			_DoSetAccepting(true);
			break;

		case kMsgDisablePrinter:
			_DoSetAccepting(false);
			break;

		case kMsgPrintTestPage:
			_DoTestPage();
			break;

		case kMsgPrinterOptions:
			_DoOptions();
			break;

		case kMsgRefresh:
			_LoadQueues();
			_LoadScanners();
			break;

		case PRINTERS_ADD_PRINTER:
		case B_PRINTER_CHANGED:
			_LoadQueues();
			_LoadJobs();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


PrinterRow*
PrintersWindow::_SelectedPrinter() const
{
	return dynamic_cast<PrinterRow*>(fPrinterList->CurrentSelection());
}


BString
PrintersWindow::_SelectedQueue() const
{
	PrinterRow* row = _SelectedPrinter();
	return row != NULL ? row->Name() : BString();
}


const char*
PrintersWindow::_FunctionsForQueue(const char* /*queue*/,
	const char* makeModel) const
{
	for (int32 i = 0; i < gScannerCount; i++) {
		if (makeModel != NULL && makeModel[0] != '\0'
			&& ContainsIgnoreCase(gScanners[i].model, makeModel))
			return B_TRANSLATE("printer + scanner");
		if (ContainsIgnoreCase(makeModel, gScanners[i].model))
			return B_TRANSLATE("printer + scanner");
	}
	return B_TRANSLATE("printer");
}


void
PrintersWindow::_UpdateQueues()
{
	BString selected = _SelectedQueue();
	ClearList(fPrinterList);

	for (int32 i = 0; i < gQueueCount; i++) {
		fPrinterList->AddRow(new PrinterRow(gQueues[i],
			_FunctionsForQueue(gQueues[i].name, gQueues[i].make_model)));
		if (selected == gQueues[i].name)
			fPrinterList->SetFocusRow(i, true);
	}

	if (gQueueCount <= 0) {
		BRow* row = new BRow();
		row->SetField(new BStringField(
			B_TRANSLATE("No printers configured")), kPrinterColumn);
		row->SetField(new BStringField(""), kLocationColumn);
		row->SetField(new BStringField(""), kStatusColumn);
		row->SetField(new BStringField(""), kFunctionsColumn);
		fPrinterList->AddRow(row);
	}
	_UpdateButtons();
}


void
PrintersWindow::_UpdateScanners()
{
	fScannerDefaultMenu->RemoveItems(0, fScannerDefaultMenu->CountItems(),
		true);

	if (gScannerCount <= 0) {
		fScannerList->MakeEmpty();
		fScannerList->AddItem(new BStringItem(
			B_TRANSLATE("(none found)")));
		fScannerDefaultField->SetEnabled(false);
		return;
	}

	fScannerList->MakeEmpty();
	for (int32 i = 0; i < gScannerCount; i++) {
		BString line(gScanners[i].model);
		if (gScanners[i].type[0] != '\0')
			line << " (" << gScanners[i].type << ")";
		if (gScanners[i].is_network)
			line << " " << B_TRANSLATE("[network]");
		fScannerList->AddItem(new BStringItem(line.String()));

		BMessage* select = new BMessage(kMsgDefaultScanner);
		select->AddString("name", gScanners[i].name);
		BString label(gScanners[i].model);
		if (label.IsEmpty())
			label = gScanners[i].name;
		fScannerDefaultMenu->AddItem(new BMenuItem(label.String(), select));
	}

	fScannerDefaultField->SetEnabled(true);
	_LoadDefaultScanner();
}


void
PrintersWindow::_UpdateJobs()
{
	ClearList(fJobList);

	for (int32 i = 0; i < gJobCount; i++)
		fJobList->AddRow(new JobRow(gJobs[i]));

	if (gJobCount <= 0) {
		BRow* row = new BRow();
		row->SetField(new BStringField(B_TRANSLATE("No jobs")), kJobIdColumn);
		row->SetField(new BStringField(""), kJobNameColumn);
		row->SetField(new BStringField(""), kJobStateColumn);
		row->SetField(new BStringField(""), kJobQueueColumn);
		fJobList->AddRow(row);
	}
}


void
PrintersWindow::_UpdateButtons()
{
	bool hasQueue = !_SelectedQueue().IsEmpty();
	PrinterRow* row = _SelectedPrinter();
	bool isDefault = row != NULL && row->IsDefault();

	fAddButton->SetEnabled(!fBusy);
	fRemoveButton->SetEnabled(hasQueue && !fBusy);
	fDefaultButton->SetEnabled(hasQueue && !isDefault && !fBusy);
	fRenameButton->SetEnabled(hasQueue && !fBusy);
	fEnableButton->SetEnabled(hasQueue && !fBusy);
	fDisableButton->SetEnabled(hasQueue && !fBusy);
	fTestPageButton->SetEnabled(hasQueue && !fBusy);
	fOptionsButton->SetEnabled(hasQueue && !fBusy);
	fRefreshButton->SetEnabled(!fBusy);
	fScannerDefaultField->SetEnabled(gScannerCount > 0 && !fBusy);
	fScanButton->SetEnabled(fScannerList->CurrentSelection() >= 0
		&& !fBusy);

	JobRow* job = dynamic_cast<JobRow*>(fJobList->CurrentSelection());
	bool hasJob = job != NULL;
	fCancelButton->SetEnabled(hasJob && !fBusy);
	fHoldButton->SetEnabled(hasJob && !fBusy);
	fReleaseButton->SetEnabled(hasJob && !fBusy);
	fPurgeButton->SetEnabled(hasQueue && !fBusy);
}


void
PrintersWindow::_SetStatus(const char* text)
{
	fStatusView->SetText(text != NULL ? text : "");
}


void
PrintersWindow::_SetError(const BString& text, status_t status)
{
	BString message(text);
	if (message.IsEmpty())
		message = B_TRANSLATE("The request failed.");
	if (status == B_PERMISSION_DENIED) {
		message << "\n\n" << B_TRANSLATE("Administrative printer changes "
			"need membership in the lpadmin group.");
	}
	_SetStatus(message.String());

	BAlert* alert = new BAlert(B_TRANSLATE("Printers"), message.String(),
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go(NULL);
}


void
PrintersWindow::_LoadDefaultScanner()
{
	BMessage settings;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
		&& path.Append(kSettingsFile) == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			settings.Unflatten(&file);
	}

	const char* saved = settings.GetString(kDefaultScannerKey, "");
	for (int32 i = 0; i < fScannerDefaultMenu->CountItems(); i++) {
		BMenuItem* item = fScannerDefaultMenu->ItemAt(i);
		const char* name = NULL;
		if (item->Message() != NULL
			&& item->Message()->FindString("name", &name) == B_OK
			&& name != NULL && saved[0] != '\0'
			&& strcmp(name, saved) == 0) {
			item->SetMarked(true);
			break;
		}
	}
}


void
PrintersWindow::_SaveDefaultScanner(const char* name)
{
	if (name == NULL)
		return;

	BMessage settings('Prnt');
	settings.AddString(kDefaultScannerKey, name);

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK
		|| path.Append(kSettingsFile) != B_OK)
		return;
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);

	_SetStatus(B_TRANSLATE("Default scanner saved"));
}


void
PrintersWindow::_DoScan()
{
	int32 index = fScannerList->CurrentSelection();
	if (index < 0 || index >= gScannerCount)
		return;

	// argv style launch so PrintOMatic opens on this device.
	BMessage launch(B_ARGV_RECEIVED);
	launch.AddString("argv", "PrintOMatic");
	launch.AddString("argv", gScanners[index].name);
	status_t status = be_roster->Launch(
		"application/x-vnd.vos-PrintOMatic", &launch);
	if (status != B_OK && status != B_ALREADY_RUNNING)
		_SetStatus(B_TRANSLATE("Could not start PrintOMatic"));
}


void
PrintersWindow::_LoadQueues()
{
	fBusy = true;
	_UpdateButtons();
	_SetStatus(B_TRANSLATE("Loading printers..."));
	PrinterWorker::Post(kWorkerOpListQueues, BMessenger(this));
}


void
PrintersWindow::_LoadScanners()
{
	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpListScanners, BMessenger(this));
}


void
PrintersWindow::_LoadJobs()
{
	fBusy = true;
	PrinterWorker::Post(kWorkerOpListJobs, BMessenger(this), _SelectedQueue());
}


void
PrintersWindow::_DoDefault()
{
	BString queue = _SelectedQueue();
	if (queue.IsEmpty())
		return;
	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpSetDefault, BMessenger(this), queue);
}


void
PrintersWindow::_DoRemove()
{
	BString queue = _SelectedQueue();
	if (queue.IsEmpty())
		return;

	BString text(B_TRANSLATE("Remove the printer \"%name%\"?"));
	text.ReplaceFirst("%name%", queue);
	BAlert* alert = new BAlert(B_TRANSLATE("Printers"), text.String(),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Remove"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	if (alert->Go() != 1)
		return;

	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpRemovePrinter, BMessenger(this), queue);
}


void
PrintersWindow::_DoRename()
{
	BString queue = _SelectedQueue();
	if (queue.IsEmpty())
		return;

	printcups_queue_info info;
	memset(&info, 0, sizeof(info));
	for (int32 i = 0; i < gQueueCount; i++) {
		if (queue == gQueues[i].name) {
			info = gQueues[i];
			break;
		}
	}

	PrinterOptionsDialog* dialog = new PrinterOptionsDialog(BMessenger(this),
		queue, info, true);
	dialog->Show();
}


void
PrintersWindow::_DoSetAccepting(bool accepting)
{
	BString queue = _SelectedQueue();
	if (queue.IsEmpty())
		return;
	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpSetAccepting, BMessenger(this), queue,
		BString(), BString(), 0, accepting);
}


void
PrintersWindow::_DoTestPage()
{
	BString queue = _SelectedQueue();
	if (queue.IsEmpty())
		return;
	fBusy = true;
	_UpdateButtons();
	_SetStatus(B_TRANSLATE("Sending a test page..."));
	PrinterWorker::Post(kWorkerOpTestPage, BMessenger(this), queue);
}


void
PrintersWindow::_DoOptions()
{
	BString queue = _SelectedQueue();
	if (queue.IsEmpty())
		return;

	printcups_queue_info info;
	memset(&info, 0, sizeof(info));
	for (int32 i = 0; i < gQueueCount; i++) {
		if (queue == gQueues[i].name) {
			info = gQueues[i];
			break;
		}
	}

	PrinterOptionsDialog* dialog = new PrinterOptionsDialog(BMessenger(this),
		queue, info, false);
	dialog->Show();
}


void
PrintersWindow::_DoAddPrinter()
{
	AddPrinterDialog* dialog = new AddPrinterDialog(BMessenger(this));
	dialog->Show();
}


void
PrintersWindow::_DoCancelJob()
{
	JobRow* row = dynamic_cast<JobRow*>(fJobList->CurrentSelection());
	if (row == NULL)
		return;
	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpCancelJob, BMessenger(this), row->Queue(),
		BString(), BString(), row->Id());
}


void
PrintersWindow::_DoHoldJob()
{
	JobRow* row = dynamic_cast<JobRow*>(fJobList->CurrentSelection());
	if (row == NULL)
		return;
	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpHoldJob, BMessenger(this), row->Queue(),
		BString(), BString(), row->Id());
}


void
PrintersWindow::_DoReleaseJob()
{
	JobRow* row = dynamic_cast<JobRow*>(fJobList->CurrentSelection());
	if (row == NULL)
		return;
	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpReleaseJob, BMessenger(this), row->Queue(),
		BString(), BString(), row->Id());
}


void
PrintersWindow::_DoPurgeJobs()
{
	BString queue = _SelectedQueue();
	if (queue.IsEmpty())
		return;

	BString text(B_TRANSLATE("Remove all jobs from \"%name%\"?"));
	text.ReplaceFirst("%name%", queue);
	BAlert* alert = new BAlert(B_TRANSLATE("Printers"), text.String(),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Purge"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	if (alert->Go() != 1)
		return;

	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpPurgeJobs, BMessenger(this), queue);
}
