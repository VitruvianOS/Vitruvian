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
#include <SeparatorItem.h>
#include <String.h>
#include <TextControl.h>

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


AddPrinterDialog::AddPrinterDialog(const BMessenger& replyTo)
	:
	BWindow(BRect(80, 80, 560, 480), B_TRANSLATE("Add Printer"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fReplyTo(replyTo),
	fBusy(false)
{
	fDiscoverRadio = new BRadioButton("discover",
		B_TRANSLATE("Discover network and USB printers"),
		new BMessage(kMsgToggleMode));
	fManualRadio = new BRadioButton("manual",
		B_TRANSLATE("Add by address"),
		new BMessage(kMsgToggleMode));
	fDiscoverRadio->SetValue(B_CONTROL_ON);

	fDeviceList = new BColumnListView("devices",
		B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER, false);
	fDeviceList->SetSelectionMessage(new BMessage(kMsgDeviceSelected));
	fDeviceList->AddColumn(new BStringColumn(B_TRANSLATE("Printer"),
		180, 100, 300, B_TRUNCATE_END), kDeviceModelColumn);
	fDeviceList->AddColumn(new BStringColumn(B_TRANSLATE("Type"),
		80, 50, 120, B_TRUNCATE_END), kDeviceClassColumn);
	fDeviceList->AddColumn(new BStringColumn(B_TRANSLATE("URI"),
		220, 120, 360, B_TRUNCATE_END), kDeviceUriColumn);

	fUriField = new BTextControl("uri", B_TRANSLATE("URI:"),
		"ipp://printer.local/ipp/print", NULL);
	fNameField = new BTextControl("name", B_TRANSLATE("Name:"), NULL, NULL);

	fModelMenu = new BPopUpMenu("model");
	fModelMenu->AddItem(new BMenuItem(B_TRANSLATE("Driverless (IPP Everywhere)"),
		new BMessage(kMsgPpdsLoaded)));
	// Index 0 is always "everywhere"; PPD entries follow.
	fPpdNames.AddItem(new BString("everywhere"));

	fRefreshButton = new BButton("refresh", B_TRANSLATE("Refresh"),
		new BMessage(kMsgDiscover));
	fAddButton = new BButton("add", B_TRANSLATE("Add"),
		new BMessage(kMsgAddConfirm));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	BBox* discoverBox = new BBox("discoverBox");
	discoverBox->SetLabel(B_TRANSLATE("Discovered printers"));
	BLayoutBuilder::Group<>(discoverBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fDeviceList, 1)
		.Add(fRefreshButton);

	BBox* manualBox = new BBox("manualBox");
	manualBox->SetLabel(B_TRANSLATE("Manual address"));
	BLayoutBuilder::Grid<>(manualBox)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.AddTextControl(fUriField, 0, 0, B_ALIGN_LEFT)
		.AddTextControl(fNameField, 0, 1, B_ALIGN_LEFT);

	BBox* modelBox = new BBox("modelBox");
	modelBox->SetLabel(B_TRANSLATE("Model / driver"));
	BLayoutBuilder::Group<>(modelBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(new BMenuField("modelField", B_TRANSLATE("Driver:"), fModelMenu));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fDiscoverRadio)
		.Add(discoverBox, 1)
		.Add(fManualRadio)
		.Add(manualBox)
		.Add(modelBox)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fAddButton)
			.Add(fCancelButton)
		.End();

	fManualRadio->SetValue(B_CONTROL_OFF);
	fModelMenu->ItemAt(0)->SetMarked(true);
	_UpdateButtons();
	CenterOnScreen();

	PrinterWorker::Post(kWorkerOpDiscover, BMessenger(this));
}


void
AddPrinterDialog::_UpdateButtons()
{
	bool discover = fDiscoverRadio->Value() == B_CONTROL_ON;
	fUriField->SetEnabled(!discover && !fBusy);
	fNameField->SetEnabled(!fBusy);
	fModelMenu->SetEnabled(!fBusy);
	fRefreshButton->SetEnabled(discover && !fBusy);
	fAddButton->SetEnabled(!fBusy);
}


void
AddPrinterDialog::_LoadPpds(const BString& uri)
{
	fBusy = true;
	_UpdateButtons();
	PrinterWorker::Post(kWorkerOpListPPDs, BMessenger(this), uri);
}


BString
AddPrinterDialog::_SelectedModel()
{
	int32 index = fModelMenu->FindMarkedIndex();
	if (index < 0 || index >= fPpdNames.CountItems())
		return BString("everywhere");
	BString* name = static_cast<BString*>(fPpdNames.ItemAt(index));
	return name != NULL ? *name : BString("everywhere");
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
				// Keep item 0 (everywhere) and drop previous PPD entries.
				while (fModelMenu->CountItems() > 1) {
					int32 index = fModelMenu->CountItems() - 1;
					delete fModelMenu->RemoveItem(index);
				}
				while (fPpdNames.CountItems() > 1) {
					int32 index = fPpdNames.CountItems() - 1;
					delete static_cast<BString*>(fPpdNames.RemoveItem(index));
				}

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
						BString label(ppds[i].make_model);
						if (label.IsEmpty())
							label = ppds[i].name;
						// Skip the built-in driverless entry name.
						if (strcasecmp(ppds[i].name, "everywhere") == 0)
							continue;
						fModelMenu->AddItem(new BMenuItem(label.String(),
							new BMessage(kMsgPpdsLoaded)));
						fPpdNames.AddItem(new BString(ppds[i].name));
					}
				}
				fModelMenu->SetTargetForItems(this);
				fModelMenu->ItemAt(0)->SetMarked(true);
			} else if (op == kWorkerOpAddPrinter) {
				if (status == B_OK) {
					fReplyTo.SendMessage(new BMessage(kMsgAddPrinterClosed));
					Quit();
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
			_UpdateButtons();
			break;

		case kMsgDiscover:
			PrinterWorker::Post(kWorkerOpDiscover, BMessenger(this));
			break;

		case kMsgDeviceSelected:
		{
			DeviceRow* row = dynamic_cast<DeviceRow*>(
				fDeviceList->CurrentSelection());
			if (row != NULL) {
				fUriField->SetText(row->Uri().String());
				BString name(row->Uri());
				int32 start = name.FindFirst("://");
				if (start >= 0)
					name.Remove(0, start + 3);
				int32 slash = name.FindFirst('/');
				if (slash >= 0)
					name.Remove(slash, name.Length() - slash);
				name.ReplaceFirst(".", "_");
				name.ReplaceFirst(" ", "_");
				if (!name.IsEmpty())
					fNameField->SetText(name.String());
				if (row->Everywhere() && fModelMenu->CountItems() > 0)
					fModelMenu->ItemAt(0)->SetMarked(true);
				_LoadPpds(row->Uri());
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
					B_TRANSLATE("Enter a printer URI."),
					B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
					B_INFO_ALERT);
				alert->Go(NULL);
				return;
			}
			if (name.IsEmpty())
				name = uri;

			BString model = _SelectedModel();
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
