/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Printers preflet: list CUPS queues, set default, show/cancel jobs,
 * add driverless IPP printers. No PPD handling.
 */

#include "PrintersWindow.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <ScrollView.h>
#include <String.h>
#include <StringItem.h>
#include <TextControl.h>

#include "Messages.h"
#include "printcups.h"
#include "pr_server.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Printers"


static const int32 kMaxQueues = 64;
static const int32 kMaxJobs = 128;


class QueueItem : public BStringItem {
public:
	QueueItem(const printcups_queue_info& info)
		:
		BStringItem(""),
		fName(info.name)
	{
		BString label(info.name);
		if (info.make_model[0] != '\0')
			label << " (" << info.make_model << ")";
		if (info.is_default)
			label << " " << B_TRANSLATE("[default]");
		SetText(label.String());
	}

	const BString& Name() const { return fName; }

private:
	BString fName;
};


class JobItem : public BStringItem {
public:
	JobItem(const printcups_job& job)
		:
		BStringItem(""),
		fId(job.id),
		fQueue(job.queue)
	{
		const char* state;
		switch (job.state) {
			case 0: state = B_TRANSLATE("pending"); break;
			case 1: state = B_TRANSLATE("held"); break;
			case 2: state = B_TRANSLATE("processing"); break;
			case 3: state = B_TRANSLATE("stopped"); break;
			case 4: state = B_TRANSLATE("canceled"); break;
			case 5: state = B_TRANSLATE("aborted"); break;
			case 6: state = B_TRANSLATE("completed"); break;
			default: state = B_TRANSLATE("unknown"); break;
		}
		BString label;
		label << job.id << "  " << job.name << "  (" << state << ")";
		SetText(label.String());
	}

	int32 Id() const { return fId; }
	const BString& Queue() const { return fQueue; }

private:
	int32	fId;
	BString	fQueue;
};


PrintersWindow::PrintersWindow()
	:
	BWindow(BRect(80, 80, 560, 520), B_TRANSLATE_SYSTEM_NAME("Printers"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_QUIT_ON_WINDOW_CLOSE)
{
	fQueueList = new BListView("queues");
	fQueueList->SetSelectionMessage(new BMessage(kMsgPrinterSelected));
	BScrollView* queueScroll = new BScrollView("queueScroll", fQueueList,
		0, false, true);

	fJobList = new BListView("jobs");
	fJobList->SetSelectionMessage(new BMessage(kMsgJobSelected));
	BScrollView* jobScroll = new BScrollView("jobScroll", fJobList,
		0, false, true);

	fDefaultButton = new BButton("default", B_TRANSLATE("Set default"),
		new BMessage(kMsgMakeDefaultPrinter));
	fRemoveButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemovePrinter));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel job"),
		new BMessage(kMsgCancelJob));

	fAddNameField = new BTextControl("addName", B_TRANSLATE("Name:"), NULL,
		NULL);
	fAddUriField = new BTextControl("addUri", B_TRANSLATE("URI:"),
		"ipp://printer.local/ipp/print", NULL);
	fAddButton = new BButton("add", B_TRANSLATE("Add"),
		new BMessage(kMsgAddPrinter));

	BBox* queueBox = new BBox("queueBox");
	queueBox->SetLabel(B_TRANSLATE("Printers"));
	BLayoutBuilder::Group<>(queueBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(queueScroll)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fRemoveButton)
			.Add(fDefaultButton)
		.End();

	BBox* jobBox = new BBox("jobBox");
	jobBox->SetLabel(B_TRANSLATE("Jobs"));
	BLayoutBuilder::Group<>(jobBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(jobScroll)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fCancelButton)
		.End();

	BBox* addBox = new BBox("addBox");
	addBox->SetLabel(B_TRANSLATE("Add a driverless (IPP Everywhere) printer"));
	BLayoutBuilder::Grid<>(addBox)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.AddTextControl(fAddNameField, 0, 0)
		.AddTextControl(fAddUriField, 0, 1)
		.Add(fAddButton, 1, 2);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(queueBox, 2)
		.Add(jobBox, 2)
		.Add(addBox, 0);

	_UpdateQueues();
	_UpdateJobs();
	CenterOnScreen();
}


void
PrintersWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPrinterSelected:
			_UpdateJobs();
			break;

		case kMsgJobSelected:
			_UpdateButtons();
			break;

		case kMsgMakeDefaultPrinter:
			_SetDefault();
			break;

		case kMsgCancelJob:
			_CancelJob();
			break;

		case kMsgAddPrinter:
			_AddPrinter();
			break;

		case kMsgRemovePrinter:
			_RemovePrinter();
			break;

		case PRINTERS_ADD_PRINTER:
		case B_PRINTER_CHANGED:
			_UpdateQueues();
			_UpdateJobs();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


BString
PrintersWindow::_SelectedQueue() const
{
	QueueItem* item = dynamic_cast<QueueItem*>(
		fQueueList->ItemAt(fQueueList->CurrentSelection()));
	return item != NULL ? item->Name() : BString();
}


void
PrintersWindow::_UpdateQueues()
{
	BString selected = _SelectedQueue();
	fQueueList->MakeEmpty();

	printcups_queue_info queues[kMaxQueues];
	int count = printcups_list_queues(queues, kMaxQueues);
	for (int32 i = 0; i < count; i++) {
		fQueueList->AddItem(new QueueItem(queues[i]));
		if (selected == queues[i].name)
			fQueueList->Select(i);
	}

	if (count <= 0) {
		BStringItem* item = new BStringItem(
			B_TRANSLATE("No printers configured"));
		item->SetEnabled(false);
		fQueueList->AddItem(item);
	}
	_UpdateButtons();
}


void
PrintersWindow::_UpdateJobs()
{
	fJobList->MakeEmpty();

	// No selection lists the jobs of every queue.
	BString queue = _SelectedQueue();
	printcups_job jobs[kMaxJobs];
	int count = printcups_list_jobs(queue.IsEmpty() ? NULL : queue.String(),
		jobs, kMaxJobs);
	for (int32 i = 0; i < count; i++)
		fJobList->AddItem(new JobItem(jobs[i]));

	if (count <= 0) {
		BStringItem* item = new BStringItem(B_TRANSLATE("No jobs"));
		item->SetEnabled(false);
		fJobList->AddItem(item);
	}
	_UpdateButtons();
}


void
PrintersWindow::_UpdateButtons()
{
	bool hasQueue = !_SelectedQueue().IsEmpty();
	fDefaultButton->SetEnabled(hasQueue);
	fRemoveButton->SetEnabled(hasQueue);
	fCancelButton->SetEnabled(dynamic_cast<JobItem*>(
		fJobList->ItemAt(fJobList->CurrentSelection())) != NULL);
}


void
PrintersWindow::_ShowError(const char* text, status_t status)
{
	BString message(text);
	if (status == B_PERMISSION_DENIED) {
		message << "\n\n" << B_TRANSLATE("Managing printers requires "
			"membership in the lpadmin group.");
	}
	BAlert* alert = new BAlert(B_TRANSLATE("Printers"), message.String(),
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go(NULL);
}


void
PrintersWindow::_SetDefault()
{
	BString queue = _SelectedQueue();
	if (queue.IsEmpty())
		return;

	status_t status = printcups_set_default(queue.String());
	if (status != B_OK) {
		_ShowError(B_TRANSLATE("Could not set the default printer."), status);
		return;
	}
	_UpdateQueues();
}


void
PrintersWindow::_CancelJob()
{
	JobItem* item = dynamic_cast<JobItem*>(
		fJobList->ItemAt(fJobList->CurrentSelection()));
	if (item == NULL)
		return;

	status_t status = printcups_cancel_job(item->Queue().String(),
		item->Id());
	if (status != B_OK) {
		_ShowError(B_TRANSLATE("Could not cancel the job."), status);
		return;
	}
	_UpdateJobs();
}


void
PrintersWindow::_AddPrinter()
{
	BString name(fAddNameField->Text());
	BString uri(fAddUriField->Text());
	name.Trim();
	uri.Trim();
	if (name.IsEmpty() || uri.IsEmpty()) {
		_ShowError(B_TRANSLATE("Enter a printer name and URI."), B_OK);
		return;
	}

	status_t status = printcups_add_printer_everywhere(name.String(),
		uri.String());
	if (status != B_OK) {
		_ShowError(B_TRANSLATE("Could not add the printer. Driverless IPP "
			"needs a reachable IPP Everywhere device."), status);
		return;
	}

	fAddNameField->SetText("");
	_UpdateQueues();
}


void
PrintersWindow::_RemovePrinter()
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

	status_t status = printcups_remove_printer(queue.String());
	if (status != B_OK) {
		_ShowError(B_TRANSLATE("Could not remove the printer."), status);
		return;
	}
	_UpdateQueues();
	_UpdateJobs();
}
