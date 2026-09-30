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
#include <Menu.h>
#include <Message.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SeparatorItem.h>
#include <String.h>
#include <StringItem.h>
#include <TextControl.h>
#include <View.h>

#include <stdio.h>

#include "Messages.h"
#include "printcups.h"
#include "pr_server.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Printers"


static const int32 kMaxQueues = 64;
static const int32 kMaxJobs = 128;


PrintersWindow::PrintersWindow()
	:
	BWindow(BRect(80, 80, 520, 480), "Printers",
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE),
	fQueueList(NULL),
	fJobList(NULL),
	fAddNameField(NULL),
	fAddUriField(NULL),
	fDefaultButton(NULL),
	fCancelButton(NULL),
	fAddButton(NULL),
	fRemoveButton(NULL)
{
	BView* top = new BView(Bounds(), "top", B_FOLLOW_ALL, 0);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	AddChild(top);

	fQueueList = new BListView(BRect(0, 0, 200, 200), "queues",
		B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL);
	BScrollView* queueScroll = new BScrollView("queueScroll", fQueueList,
		B_FOLLOW_ALL, 0, false, true);
	fQueueList->SetSelectionMessage(new BMessage(kMsgPrinterSelected));
	fQueueList->SetTarget(this);

	fJobList = new BListView(BRect(0, 0, 260, 200), "jobs",
		B_SINGLE_SELECTION_LIST, B_FOLLOW_ALL);
	BScrollView* jobScroll = new BScrollView("jobScroll", fJobList,
		B_FOLLOW_ALL, 0, false, true);
	fJobList->SetSelectionMessage(new BMessage(kMsgJobSelected));
	fJobList->SetTarget(this);

	fDefaultButton = new BButton("default", B_TRANSLATE("Set default"),
		new BMessage(kMsgMakeDefaultPrinter));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel job"),
		new BMessage(kMsgCancelJob));
	fRemoveButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemovePrinter));

	fAddNameField = new BTextControl("addName", B_TRANSLATE("Name:"), NULL,
		new BMessage(kMsgAddPrinter));
	fAddUriField = new BTextControl("addUri", B_TRANSLATE("URI:"), NULL,
		new BMessage(kMsgAddPrinter));
	fAddUriField->SetText("ipp://printer.local/ipp/print");
	fAddButton = new BButton("add", B_TRANSLATE("Add (IPP Everywhere)"),
		new BMessage(kMsgAddPrinter));

	BBox* queueBox = new BBox("queueBox", B_FOLLOW_ALL, B_WILL_DRAW);
	queueBox->SetLabel(B_TRANSLATE("Queues"));
	queueBox->AddChild(queueScroll);

	BBox* jobBox = new BBox("jobBox", B_FOLLOW_ALL, B_WILL_DRAW);
	jobBox->SetLabel(B_TRANSLATE("Jobs"));
	jobBox->AddChild(jobScroll);

	BBox* addBox = new BBox("addBox", B_FOLLOW_ALL, B_WILL_DRAW);
	addBox->SetLabel(B_TRANSLATE("Add printer (driverless IPP)"));
	addBox->AddChild(fAddNameField);
	addBox->AddChild(fAddUriField);
	addBox->AddChild(fAddButton);

	top->AddChild(queueBox);
	top->AddChild(jobBox);
	top->AddChild(addBox);
	top->AddChild(fDefaultButton);
	top->AddChild(fCancelButton);
	top->AddChild(fRemoveButton);

	// Simple absolute layout, matching the older preflet placement.
	BRect bounds = Bounds();
	queueBox->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
	jobBox->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
	addBox->SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM);

	queueBox->MoveTo(10, 10);
	queueBox->ResizeTo(bounds.Width() - 20, 160);
	jobBox->MoveTo(10, 180);
	jobBox->ResizeTo(bounds.Width() - 20, 160);
	addBox->MoveTo(10, 350);
	addBox->ResizeTo(bounds.Width() - 20, 110);
	fAddNameField->MoveTo(10, 24);
	fAddNameField->ResizeTo(200, fAddNameField->Frame().Height());
	fAddUriField->MoveTo(220, 24);
	fAddUriField->ResizeTo(bounds.Width() - 250, fAddUriField->Frame().Height());
	fAddButton->MoveTo(10, 56);
	fDefaultButton->MoveTo(10, bounds.bottom - 40);
	fCancelButton->MoveTo(130, bounds.bottom - 40);
	fRemoveButton->MoveTo(250, bounds.bottom - 40);

	queueScroll->SetResizingMode(B_FOLLOW_ALL);
	jobScroll->SetResizingMode(B_FOLLOW_ALL);

	_UpdateQueues();
	_UpdateJobs();
}


void
PrintersWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPrinterSelected:
			_UpdateJobs();
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
			// InterfaceDefs run_add_printer_panel()
			_UpdateQueues();
			break;

		case B_PRINTER_CHANGED:
			_UpdateQueues();
			_UpdateJobs();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
PrintersWindow::QuitRequested()
{
	return BWindow::QuitRequested();
}


BStringItem*
PrintersWindow::_SelectedQueue() const
{
	return dynamic_cast<BStringItem*>(fQueueList->ItemAt(
		fQueueList->CurrentSelection()));
}


BString
PrintersWindow::_SelectedJobName() const
{
	BStringItem* item = dynamic_cast<BStringItem*>(fJobList->ItemAt(
		fJobList->CurrentSelection()));
	if (item == NULL)
		return BString();

	// Job list items: "id state name"
	const char* text = item->Text();
	int32 id = 0;
	char name[256];
	name[0] = '\0';
	if (sscanf(text, "%d %*s %255[^\n]", &id, name) >= 1)
		return BString(name);
	return BString(text);
}


int32
PrintersWindow::_SelectedJobId() const
{
	BStringItem* item = dynamic_cast<BStringItem*>(fJobList->ItemAt(
		fJobList->CurrentSelection()));
	if (item == NULL)
		return -1;

	int32 id = -1;
	if (sscanf(item->Text(), "%d", &id) != 1)
		return -1;
	return id;
}


void
PrintersWindow::_UpdateQueues()
{
	fQueueList->MakeEmpty();

	printcups_queue_info queues[kMaxQueues];
	int count = printcups_list_queues(queues, kMaxQueues);
	if (count < 0)
		count = 0;

	for (int32 i = 0; i < count; i++) {
		BString label(queues[i].name);
		if (queues[i].is_default)
			label << "  [default]";
		if (queues[i].make_model[0] != '\0')
			label << "  (" << queues[i].make_model << ")";
		fQueueList->AddItem(new BStringItem(label.String()));
	}

	if (count == 0)
		fQueueList->AddItem(new BStringItem(
			B_TRANSLATE("No printers configured")));
}


void
PrintersWindow::_UpdateJobs()
{
	fJobList->MakeEmpty();

	BStringItem* queueItem = _SelectedQueue();
	const char* queue = NULL;
	if (queueItem != NULL && queueItem->Text()[0] != '\0'
		&& strstr(queueItem->Text(), "No printers") == NULL) {
		// Strip " [default] (...)" suffix.
		BString name(queueItem->Text());
		int32 sp = name.FindFirst("  [");
		if (sp >= 0)
			name.Truncate(sp);
		int32 paren = name.FindFirst("  (");
		if (paren >= 0 && (sp < 0 || paren < sp))
			name.Truncate(paren);
		queue = name.String();
	}

	printcups_job jobs[kMaxJobs];
	int count = printcups_list_jobs(queue, jobs, kMaxJobs);
	if (count < 0)
		count = 0;

	for (int32 i = 0; i < count; i++) {
		const char* state = "unknown";
		switch (jobs[i].state) {
			case 0: state = "pending"; break;
			case 1: state = "held"; break;
			case 2: state = "processing"; break;
			case 3: state = "stopped"; break;
			case 4: state = "canceled"; break;
			case 5: state = "aborted"; break;
			case 6: state = "completed"; break;
		}
		BString label;
		label << jobs[i].id << " " << state << " " << jobs[i].name;
		fJobList->AddItem(new BStringItem(label.String()));
	}

	if (count == 0)
		fJobList->AddItem(new BStringItem(B_TRANSLATE("No jobs")));
}


void
PrintersWindow::_SetDefault()
{
	BStringItem* item = _SelectedQueue();
	if (item == NULL)
		return;

	BString name(item->Text());
	int32 sp = name.FindFirst("  [");
	if (sp >= 0)
		name.Truncate(sp);

	if (printcups_set_default(name.String()) != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Printers"),
			B_TRANSLATE("Could not set the default printer."),
			B_TRANSLATE("OK"));
		alert->Go();
		return;
	}

	_UpdateQueues();
	_UpdateJobs();
}


void
PrintersWindow::_CancelJob()
{
	int32 id = _SelectedJobId();
	if (id < 0)
		return;

	BStringItem* queueItem = _SelectedQueue();
	const char* queue = NULL;
	BString queueName;
	if (queueItem != NULL) {
		queueName = queueItem->Text();
		int32 sp = queueName.FindFirst("  [");
		if (sp >= 0)
			queueName.Truncate(sp);
		queue = queueName.String();
	}

	if (queue == NULL || queue[0] == '\0')
		return;

	if (printcups_cancel_job(queue, id) != B_OK) {
		BAlert* alert = new BAlert(B_TRANSLATE("Printers"),
			B_TRANSLATE("Could not cancel the job."),
			B_TRANSLATE("OK"));
		alert->Go();
		return;
	}

	_UpdateJobs();
}


void
PrintersWindow::_AddPrinter()
{
	const char* name = fAddNameField->Text();
	const char* uri = fAddUriField->Text();
	if (name == NULL || name[0] == '\0' || uri == NULL || uri[0] == '\0') {
		BAlert* alert = new BAlert(B_TRANSLATE("Printers"),
			B_TRANSLATE("Enter a printer name and URI."),
			B_TRANSLATE("OK"));
		alert->Go();
		return;
	}

	status_t status = printcups_add_printer_everywhere(name, uri);
	if (status != B_OK) {
		// Fall back to lpadmin -m everywhere (needs local admin).
		char cmd[1024];
		snprintf(cmd, sizeof(cmd),
			"lpadmin -p %s -v %s -m everywhere -E 2>/dev/null", name, uri);
		if (system(cmd) != 0) {
			BAlert* alert = new BAlert(B_TRANSLATE("Printers"),
				B_TRANSLATE("Could not add the printer. Driverless IPP "
					"needs a reachable IPP Everywhere device."),
				B_TRANSLATE("OK"));
			alert->Go();
			return;
		}
	}

	_UpdateQueues();
	_UpdateJobs();
}


void
PrintersWindow::_RemovePrinter()
{
	BStringItem* item = _SelectedQueue();
	if (item == NULL)
		return;

	BString name(item->Text());
	int32 sp = name.FindFirst("  [");
	if (sp >= 0)
		name.Truncate(sp);

	char cmd[512];
	snprintf(cmd, sizeof(cmd), "lpadmin -x %s 2>/dev/null", name.String());
	if (system(cmd) != 0) {
		BAlert* alert = new BAlert(B_TRANSLATE("Printers"),
			B_TRANSLATE("Could not remove the printer."),
			B_TRANSLATE("OK"));
		alert->Go();
		return;
	}

	_UpdateQueues();
	_UpdateJobs();
}
