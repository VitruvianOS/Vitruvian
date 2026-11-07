/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AddPrinterDialog.h"

#include <Alert.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <StringView.h>
#include <String.h>
#include <string.h>
#include <strings.h>
#include <TextControl.h>
#include <TabView.h>

#include "Messages.h"
#include "PrinterWorker.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Printers"


enum {
	kDeviceModelColumn = 0,
	kDeviceClassColumn,
	kDeviceUriColumn,
};


static void
ClearList(BColumnListView* list)
{
	while (list->CountRows() > 0)
		list->RemoveRow(list->RowAt(0));
}


DeviceRow::DeviceRow(const printcups_device& device)
	:
	BRow(),
	fUri(device.uri),
	fEverywhere(device.everywhere)
{
	BString model(device.make_model);
	if (model.IsEmpty())
		model = device.info;
	if (model.IsEmpty())
		model = device.uri;
	if (device.everywhere)
		model << " " << B_TRANSLATE("[driverless]");

	BString deviceClass(device.device_class);
	if (deviceClass.IsEmpty())
		deviceClass = "local";

	SetField(new BStringField(model.String()), kDeviceModelColumn);
	SetField(new BStringField(deviceClass.String()), kDeviceClassColumn);
	SetField(new BStringField(device.uri), kDeviceUriColumn);
}


PpdRow::PpdRow(const printcups_ppd& ppd)
	:
	BRow(),
	fName(ppd.name)
{
	BString model(ppd.make_model);
	if (model.IsEmpty())
		model = ppd.name;
	SetField(new BStringField(model.String()), 0);
	SetField(new BStringField(ppd.name), 1);
}




static const int32 kMaxShownPpds = 500;


AddPrinterDialog::AddPrinterDialog(const BMessenger& replyTo)
	:
	BWindow(BRect(80, 80, 640, 560), B_TRANSLATE("Add Printer"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE 
		| B_AUTO_UPDATE_SIZE_LIMITS),
	fReplyTo(replyTo),
	fPpdsLoaded(false),
	fBusy(false)
{
	const float scale = be_plain_font->Size() / 12.0f;

	// --- Discover tab ---
	fDeviceList = new BColumnListView("devices",
		B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER, true);
	fDeviceList->SetSelectionMessage(new BMessage(kMsgDeviceSelected));
	fDeviceList->AddColumn(new BStringColumn(B_TRANSLATE("Printer"),
		180 * scale, 100 * scale, 300 * scale, B_TRUNCATE_END),
		kDeviceModelColumn);
	fDeviceList->AddColumn(new BStringColumn(B_TRANSLATE("Type"),
		80 * scale, 50 * scale, 120 * scale, B_TRUNCATE_END),
		kDeviceClassColumn);
	fDeviceList->AddColumn(new BStringColumn(B_TRANSLATE("URI"),
		220 * scale, 120 * scale, 360 * scale, B_TRUNCATE_END),
		kDeviceUriColumn);
	fRefreshButton = new BButton("refresh", B_TRANSLATE("Refresh"),
		new BMessage(kMsgDiscover));

	BView* discoverTab = new BView("discover", B_WILL_DRAW);
	BLayoutBuilder::Group<>(discoverTab, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fDeviceList, 1)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fRefreshButton)
		.End();

	// --- Manual tab ---
	fUriField = new BTextControl("uri", B_TRANSLATE("URI:"), "", NULL);
	BStringView* uriHint = new BStringView("uriHint", B_TRANSLATE(
		"e.g. ipp://192.168.1.36/ipp/print or socket://host:9100"));
	uriHint->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

	BView* manualTab = new BView("manual", B_WILL_DRAW);
	BLayoutBuilder::Grid<>(manualTab)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddTextControl(fUriField, 0, 0)
		.Add(uriHint, 1, 1)
		.AddGlue(0, 2);

	// --- Driver tab ---
	fDriverlessRadio = new BRadioButton("driverless",
		B_TRANSLATE("Driverless (IPP Everywhere)"),
		new BMessage(kMsgToggleMode));
	fChooseDriverRadio = new BRadioButton("chooseDriver",
		B_TRANSLATE("Choose a driver from the list"),
		new BMessage(kMsgToggleMode));
	fDriverlessRadio->SetValue(B_CONTROL_ON);

	fDriverFilter = new BTextControl("driverFilter", B_TRANSLATE("Filter:"),
		"", NULL);
	fDriverFilter->SetModificationMessage(new BMessage(kMsgDriverFilter));

	fPpdList = new BColumnListView("ppds",
		B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER, true);
	fPpdList->AddColumn(new BStringColumn(B_TRANSLATE("Model"),
		260 * scale, 120 * scale, 600 * scale, B_TRUNCATE_END), 0);
	fPpdList->AddColumn(new BStringColumn(B_TRANSLATE("Driver"),
		200 * scale, 100 * scale, 500 * scale, B_TRUNCATE_END), 1);

	BView* driverTab = new BView("driver", B_WILL_DRAW);
	BLayoutBuilder::Group<>(driverTab, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fDriverlessRadio)
		.Add(fChooseDriverRadio)
		.Add(fDriverFilter)
		.Add(fPpdList, 1);

	fTabs = new BTabView("tabs");
	fTabs->AddTab(discoverTab);
	fTabs->TabAt(0)->SetLabel(B_TRANSLATE("Discover"));
	fTabs->AddTab(manualTab);
	fTabs->TabAt(1)->SetLabel(B_TRANSLATE("By address"));
	fTabs->AddTab(driverTab);
	fTabs->TabAt(2)->SetLabel(B_TRANSLATE("Driver"));

	fNameField = new BTextControl("name", B_TRANSLATE("Name:"), NULL, NULL);
	fAddButton = new BButton("add", B_TRANSLATE("Add"),
		new BMessage(kMsgAddConfirm));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fTabs, 1)
		.Add(fNameField)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fAddButton)
			.Add(fCancelButton)
		.End();

	_UpdateButtons();
	CenterOnScreen();

	PrinterWorker::Post(kWorkerOpDiscover, BMessenger(this));
}


AddPrinterDialog::~AddPrinterDialog()
{
	_ClearPpds();
}


void
AddPrinterDialog::_UpdateButtons()
{
	bool choose = fChooseDriverRadio->Value() == B_CONTROL_ON;
	fRefreshButton->SetEnabled(!fBusy);
	fDriverFilter->SetEnabled(choose && !fBusy);
	fAddButton->SetEnabled(!fBusy);
}


void
AddPrinterDialog::_ClearPpds()
{
	for (int32 i = 0; i < fAllPpds.CountItems(); i++)
		delete static_cast<printcups_ppd*>(fAllPpds.ItemAt(i));
	fAllPpds.MakeEmpty();
}


void
AddPrinterDialog::_LoadPpds()
{
	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpListPPDs, BMessenger(this));
}


void
AddPrinterDialog::_FillPpdList()
{
	ClearList(fPpdList);

	BString filter(fDriverFilter->Text());
	filter.Trim();

	int32 shown = 0;
	for (int32 i = 0; i < fAllPpds.CountItems() && shown < kMaxShownPpds;
			i++) {
		const printcups_ppd* ppd
			= static_cast<const printcups_ppd*>(fAllPpds.ItemAt(i));
		if (!filter.IsEmpty()
			&& strcasestr(ppd->make_model, filter.String()) == NULL
			&& strcasestr(ppd->name, filter.String()) == NULL) {
			continue;
		}
		fPpdList->AddRow(new PpdRow(*ppd));
		shown++;
	}
}


BString
AddPrinterDialog::_SelectedModel()
{
	if (fChooseDriverRadio->Value() != B_CONTROL_ON)
		return BString("everywhere");

	PpdRow* row = dynamic_cast<PpdRow*>(fPpdList->CurrentSelection());
	return row != NULL ? row->Name() : BString();
}


void
AddPrinterDialog::MessageReceived(BMessage* message)
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
			fBusy = false;

			if (op == kWorkerOpDiscover) {
				ClearList(fDeviceList);
				const void* data = NULL;
				ssize_t size = 0;
				int32 count = 0;
				message->FindInt32("count", &count);
				if (status == B_OK
					&& message->FindData("devices", B_RAW_TYPE, &data,
						&size) == B_OK) {
					const printcups_device* devices
						= static_cast<const printcups_device*>(data);
					int32 n = size / (int32)sizeof(printcups_device);
					if (n > count)
						n = count;
					for (int32 i = 0; i < n; i++)
						fDeviceList->AddRow(new DeviceRow(devices[i]));
				}
				if (status != B_OK && !error.IsEmpty()) {
					BAlert* alert = new BAlert(B_TRANSLATE("Add Printer"),
						error.String(), B_TRANSLATE("OK"), NULL, NULL,
						B_WIDTH_AS_USUAL, B_INFO_ALERT);
					alert->Go(NULL);
				}
			} else if (op == kWorkerOpListPPDs) {
				_ClearPpds();
				fPpdsLoaded = false;

				const void* data = NULL;
				ssize_t size = 0;
				int32 count = 0;
				message->FindInt32("count", &count);
				if (status == B_OK
					&& message->FindData("ppds", B_RAW_TYPE, &data,
						&size) == B_OK) {
					const printcups_ppd* ppds
						= static_cast<const printcups_ppd*>(data);
					int32 n = size / (int32)sizeof(printcups_ppd);
					if (n > count)
						n = count;
					for (int32 i = 0; i < n; i++) {
						// The driverless entry has its own radio button.
						if (strcasecmp(ppds[i].name, "everywhere") == 0)
							continue;
						printcups_ppd* copy = new printcups_ppd(ppds[i]);
						if (copy->make_model[0] == '\0') {
							strlcpy(copy->make_model, copy->name,
								sizeof(copy->make_model));
						}
						fAllPpds.AddItem(copy);
					}
					fPpdsLoaded = true;
				} else if (status != B_OK) {
					BString text(error);
					if (text.IsEmpty())
						text = B_TRANSLATE("Could not load the driver list.");
					BAlert* alert = new BAlert(B_TRANSLATE("Add Printer"),
						text.String(), B_TRANSLATE("OK"), NULL, NULL,
						B_WIDTH_AS_USUAL, B_STOP_ALERT);
					alert->Go(NULL);
				}
				_FillPpdList();
			} else if (op == kWorkerOpAddPrinter) {
				if (status == B_OK) {
					fReplyTo.SendMessage(new BMessage(kMsgAddPrinterClosed));
					Quit();
					return;
				} else {
					BString messageText(error);
					if (messageText.IsEmpty())
						messageText = B_TRANSLATE("Could not add the printer.");
					if (status == B_PERMISSION_DENIED) {
						messageText << "\n\n" << B_TRANSLATE(
							"Administrative printer changes need membership "
							"in the lpadmin group.");
					}
					BAlert* alert = new BAlert(B_TRANSLATE("Add Printer"),
						messageText.String(), B_TRANSLATE("OK"), NULL, NULL,
						B_WIDTH_AS_USUAL, B_STOP_ALERT);
					alert->Go(NULL);
				}
			}
			_UpdateButtons();
			break;
		}

		case kMsgToggleMode:
			// Fetch the (long) driver list only once somebody wants it.
			if (fChooseDriverRadio->Value() == B_CONTROL_ON && !fPpdsLoaded
				&& !fBusy) {
				_LoadPpds();
			}
			_UpdateButtons();
			break;

		case kMsgDriverFilter:
			_FillPpdList();
			break;

		case kMsgDiscover:
			fBusy = true;
			_UpdateButtons();
			PrinterWorker::Post(kWorkerOpDiscover, BMessenger(this));
			break;

		case kMsgDeviceSelected:
		{
			DeviceRow* row = dynamic_cast<DeviceRow*>(
				fDeviceList->CurrentSelection());
			if (row != NULL) {
				// The URI field is the single source of truth for Add; the
				// list just fills it in.
				fUriField->SetText(row->Uri().String());
				BString name(row->Uri());
				int32 start = name.FindFirst("://");
				if (start >= 0)
					name.Remove(0, start + 3);
				int32 slash = name.FindFirst('/');
				if (slash >= 0)
					name.Remove(slash, name.Length() - slash);
				name.ReplaceAll(".", "_");
				name.ReplaceAll(" ", "_");
				name.ReplaceAll(":", "_");
				if (!name.IsEmpty())
					fNameField->SetText(name.String());
				if (row->Everywhere())
					fDriverlessRadio->SetValue(B_CONTROL_ON);
			}
			break;
		}

		case kMsgAddConfirm:
		{
			BString name(fNameField->Text());
			BString uri(fUriField->Text());
			name.Trim();
			uri.Trim();
			if (uri.IsEmpty()) {
				BAlert* alert = new BAlert(B_TRANSLATE("Add Printer"),
					B_TRANSLATE("Select a discovered printer or enter a "
						"printer URI."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_INFO_ALERT);
				alert->Go(NULL);
				return;
			}
			if (name.IsEmpty())
				name = uri;

			BString model = _SelectedModel();
			if (model.IsEmpty()) {
				BAlert* alert = new BAlert(B_TRANSLATE("Add Printer"),
					B_TRANSLATE("Select a driver from the list, or choose "
						"driverless."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_INFO_ALERT);
				alert->Go(NULL);
				return;
			}
			fBusy = true;
			_UpdateButtons();
			PrinterWorker::Post(kWorkerOpAddPrinter, BMessenger(this),
				name, uri, model);
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
