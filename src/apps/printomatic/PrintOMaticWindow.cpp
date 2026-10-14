/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * PrintOMatic main window: device, options, preview with area
 * selection, multi-page session, print or save.
 */

#include "PrintOMaticWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <BitmapStream.h>
#include <Catalog.h>
#include <Box.h>
#include <Button.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <List.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Notification.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <Rect.h>
#include <Roster.h>
#include <StringList.h>
#include <StringView.h>
#include <TextControl.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <View.h>

#include <PrintJob.h>

#include "printcups.h"
#include "print/pr_server.h"
#include "PrintCupsRender.h"
#include "ScanView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrintOMatic"


static const char* kSettingsFileName = "PrintOMatic";
static const float kPreviewDpi = 75.0f;


// Hidden view that draws a page bitmap fitted into its bounds; used
// for BPrintJob::DrawView and for PDF picture capture.
class PageDrawView : public BView {
public:
					PageDrawView()
						:
						BView(BRect(0, 0, 99, 99), "pageDraw",
							B_FOLLOW_NONE, 0),
						fBitmap(NULL)
					{
					}

			void	SetBitmap(BBitmap* bitmap) { fBitmap = bitmap; }

	virtual	void	Draw(BRect updateRect)
					{
						(void)updateRect;
						SetLowColor(255, 255, 255);
						FillRect(Bounds(), B_SOLID_LOW);
						if (fBitmap == NULL
							|| fBitmap->InitCheck() != B_OK)
							return;

						BRect src = fBitmap->Bounds();
						BRect dst = Bounds();
						float scale = dst.Width() / src.Width();
						if (dst.Height() / src.Height() < scale)
							scale = dst.Height() / src.Height();
						float width = src.Width() * scale;
						float height = src.Height() * scale;
						BRect target(0, 0, width, height);
						target.OffsetBy((dst.Width() - width) / 2, 0);
						DrawBitmap(fBitmap, src, target);
					}

			BBitmap*	fBitmap;
};


static void
ProgressCallback(void* cookie, float done, int32 /*page*/)
{
	PrintOMaticWindow* window = static_cast<PrintOMaticWindow*>(cookie);
	BMessage message(kMsgScanProgress);
	message.AddFloat("done", done);
	window->PostMessage(&message);
}


static int32
ScanThreadEntry(void* data)
{
	PrintOMaticWindow* window = static_cast<PrintOMaticWindow*>(data);
	window->_RunScan(false);
	return 0;
}


static int32
PreviewThreadEntry(void* data)
{
	PrintOMaticWindow* window = static_cast<PrintOMaticWindow*>(data);
	window->_RunScan(true);
	return 0;
}


static int32
DeviceLoadEntry(void* data)
{
	PrintOMaticWindow* window = static_cast<PrintOMaticWindow*>(data);
	libscan_device devices[32];
	int32 count = libscan_list_devices(devices, 32);

	BMessage message(kMsgDevicesLoaded);
	if (count >= 0) {
		message.AddData("devices", B_RAW_TYPE, devices,
			count * (int32)sizeof(libscan_device));
		message.AddInt32("count", count);
	} else
		message.AddInt32("count", -1);
	window->PostMessage(&message);
	return 0;
}


static int32
PrinterLoadEntry(void* data)
{
	PrintOMaticWindow* window = static_cast<PrintOMaticWindow*>(data);
	printcups_queue_info queues[16];
	int32 count = printcups_list_queues(queues, 16);

	BMessage message(kMsgPrintersLoaded);
	if (count >= 0) {
		for (int32 i = 0; i < count; i++)
			message.AddString("queue", queues[i].name);
	} else
		message.AddInt32("count", -1);
	window->PostMessage(&message);
	return 0;
}


static status_t
WriteBitmapFile(BBitmap* bitmap, const char* path, const char* mimeType,
	uint32 translatorType)
{
	BBitmapStream stream(bitmap);
	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return B_ERROR;

	BTranslatorRoster* roster = BTranslatorRoster::Default();
	if (roster == NULL)
		return B_ERROR;
	if (roster->Translate(&stream, NULL, NULL, &file, translatorType)
			< B_OK)
		return B_ERROR;

	BNodeInfo nodeInfo(&file);
	if (nodeInfo.InitCheck() == B_OK)
		nodeInfo.SetType(mimeType);
	return B_OK;
}


PrintOMaticWindow::PrintOMaticWindow(const BRect& frame, const char* scanner)
	:
	BWindow(frame, "PrintOMatic", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fDeviceCount(0),
	fBusy(false),
	fBatchActive(false),
	fScanThread(-1)
{
	fDeviceName[0] = '\0';
	if (scanner != NULL)
		strlcpy(fDeviceName, scanner, sizeof(fDeviceName));

	fDeviceMenu = new BPopUpMenu("device");
	fDeviceField = new BMenuField("deviceField",
		B_TRANSLATE("Scanner:"), fDeviceMenu);
	fRefreshButton = new BButton("refresh", B_TRANSLATE("Refresh"),
		new BMessage(kMsgRefresh));

	fSourceMenu = new BPopUpMenu("source");
	fSourceField = new BMenuField("sourceField",
		B_TRANSLATE("Source:"), fSourceMenu);
	fModeMenu = new BPopUpMenu("mode");
	fModeField = new BMenuField("modeField", B_TRANSLATE("Mode:"),
		fModeMenu);
	fResolutionMenu = new BPopUpMenu("resolution");
	fResolutionField = new BMenuField("resolutionField",
		B_TRANSLATE("Resolution:"), fResolutionMenu);

	fPreviewView = new ScanView();

	fPageList = new BListView("pages", B_SINGLE_SELECTION_LIST);
	fDeletePageButton = new BButton("deletePage",
		B_TRANSLATE("Delete"), new BMessage(kMsgDeletePage));
	fMoveUpButton = new BButton("moveUp", B_TRANSLATE("Move Up"),
		new BMessage(kMsgMoveUp));
	fMoveDownButton = new BButton("moveDown", B_TRANSLATE("Move Down"),
		new BMessage(kMsgMoveDown));

	fPrintRadio = new BRadioButton("printRadio",
		B_TRANSLATE("Copy to printer"), new BMessage(kMsgOptionChanged));
	fSaveRadio = new BRadioButton("saveRadio",
		B_TRANSLATE("Scan to file"), new BMessage(kMsgOptionChanged));
	fPrinterMenu = new BPopUpMenu("printer");
	fPrinterField = new BMenuField("printerField",
		B_TRANSLATE("Printer:"), fPrinterMenu);
	fCopiesControl = new BTextControl("copies", B_TRANSLATE("Copies:"),
		"1", NULL);
	fFormatMenu = new BPopUpMenu("format");
	BMessage* pdfItem = new BMessage(kMsgOptionChanged);
	pdfItem->AddString("format", "PDF");
	fFormatMenu->AddItem(new BMenuItem("PDF", pdfItem));
	BMessage* pngItem = new BMessage(kMsgOptionChanged);
	pngItem->AddString("format", "PNG");
	fFormatMenu->AddItem(new BMenuItem("PNG", pngItem));
	BMessage* jpegItem = new BMessage(kMsgOptionChanged);
	jpegItem->AddString("format", "JPEG");
	fFormatMenu->AddItem(new BMenuItem("JPEG", jpegItem));
	fFormatField = new BMenuField("formatField",
		B_TRANSLATE("Format:"), fFormatMenu);
	fFormatMenu->FindItem("PDF")->SetMarked(true);

	fPreviewButton = new BButton("preview", B_TRANSLATE("Preview"),
		new BMessage(kMsgPreview));
	fScanButton = new BButton("scan", B_TRANSLATE("Scan Page"),
		new BMessage(kMsgScanPage));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancelScan));
	fPrintButton = new BButton("print", B_TRANSLATE("Print Copies"),
		new BMessage(kMsgPrintCopies));
	fSaveButton = new BButton("save", B_TRANSLATE("Save to File"),
		new BMessage(kMsgSaveFile));
	fStatusView = new BStringView("status", B_TRANSLATE("Starting..."));

	BBox* pagesBox = new BBox("pagesBox");
	pagesBox->SetLabel(B_TRANSLATE("Pages"));
	BLayoutBuilder::Group<>(pagesBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fPageList)
		.AddGroup(B_HORIZONTAL)
			.Add(fDeletePageButton)
			.Add(fMoveUpButton)
			.Add(fMoveDownButton)
		.End();

	BBox* destinationBox = new BBox("destinationBox");
	destinationBox->SetLabel(B_TRANSLATE("Destination"));
	BLayoutBuilder::Group<>(destinationBox, B_VERTICAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fPrintRadio)
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_DEFAULT_SPACING, 0, 0, 0)
			.Add(fPrinterField)
			.Add(fCopiesControl)
		.End()
		.Add(fSaveRadio)
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_DEFAULT_SPACING, 0, 0, 0)
			.Add(fFormatField)
		.End();

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(fDeviceField)
			.Add(fRefreshButton)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fSourceField)
			.Add(fModeField)
			.Add(fResolutionField)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fPreviewView, 3)
			.Add(pagesBox, 1)
		.End()
		.Add(destinationBox)
		.AddGroup(B_HORIZONTAL)
			.Add(fPreviewButton)
			.Add(fScanButton)
			.Add(fCancelButton)
			.AddGlue()
			.Add(fPrintButton)
			.Add(fSaveButton)
		.End()
		.Add(fStatusView);

	// Hidden helper for print/PDF page rendering.
	fPageDrawView = new PageDrawView();
	AddChild(fPageDrawView);
	fPrintRadio->SetValue(B_CONTROL_ON);
	fCancelButton->SetEnabled(false);

	_LoadSettings();
	_UpdateButtons();

	thread_id thread = spawn_thread(DeviceLoadEntry, "printomatic:devices",
		B_NORMAL_PRIORITY, this);
	if (thread >= B_OK)
		resume_thread(thread);
	thread = spawn_thread(PrinterLoadEntry, "printomatic:printers",
		B_NORMAL_PRIORITY, this);
	if (thread >= B_OK)
		resume_thread(thread);

	CenterOnScreen();
}


bool
PrintOMaticWindow::QuitRequested()
{
	if (fBusy)
		_CancelScan();
	_SaveSettings();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
PrintOMaticWindow::SelectScanner(const char* name)
{
	if (name == NULL)
		return;
	strlcpy(fDeviceName, name, sizeof(fDeviceName));
	for (int32 i = 0; i < fDeviceCount; i++) {
		if (strcmp(fDevices[i].name, name) == 0) {
			BMenuItem* item = fDeviceMenu->ItemAt(i);
			if (item != NULL)
				item->SetMarked(true);
			_SelectDevice(i);
			return;
		}
	}
	// The device may appear after the next refresh.
	_SetStatus(B_TRANSLATE("Device will be selected after refresh"));
}


void
PrintOMaticWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDevicesLoaded:
		{
			int32 count = 0;
			message->FindInt32("count", &count);
			if (count < 0) {
				_SetStatus(B_TRANSLATE("SANE is not available"));
				break;
			}
			const void* data = NULL;
			ssize_t size = 0;
			fDeviceCount = 0;
			if (count > 0 && message->FindData("devices", B_RAW_TYPE,
					&data, &size) == B_OK) {
				fDeviceCount = count;
				if (size > (ssize_t)sizeof(fDevices))
					size = sizeof(fDevices);
				memcpy(fDevices, data, size);
				fDeviceCount = size / (int32)sizeof(libscan_device);
			}

			fDeviceMenu->RemoveItems(0, fDeviceMenu->CountItems(),
				true);
			if (fDeviceCount == 0) {
				fDeviceMenu->AddItem(new BMenuItem(
					B_TRANSLATE("(no scanners found)"), NULL));
				_SetStatus(B_TRANSLATE("No scanners found"));
			} else {
				for (int32 i = 0; i < fDeviceCount; i++) {
					BString label(fDevices[i].model);
					if (fDevices[i].type[0] != '\0')
						label << " (" << fDevices[i].type << ")";
					if (fDevices[i].is_network)
						label << " " << B_TRANSLATE("[network]");
					BMessage* select
						= new BMessage(kMsgDeviceSelected);
					select->AddInt32("index", i);
					fDeviceMenu->AddItem(
						new BMenuItem(label.String(), select));
				}
				_SetStatus(B_TRANSLATE("Ready"));
			}
			fDeviceMenu->ItemAt(0)->SetMarked(true);

			if (fDeviceName[0] == '\0' && fDeviceCount > 0)
				strlcpy(fDeviceName, fDevices[0].name,
					sizeof(fDeviceName));
			for (int32 i = 0; i < fDeviceCount; i++) {
				if (strcmp(fDevices[i].name, fDeviceName) == 0) {
					fDeviceMenu->ItemAt(i)->SetMarked(true);
					_SelectDevice(i);
					break;
				}
			}
			_UpdateButtons();
			break;
		}

		case kMsgPrintersLoaded:
		{
			_UpdatePrinterMenu(message);
			break;
		}

		case kMsgDeviceSelected:
		{
			int32 index = -1;
			if (message->FindInt32("index", &index) == B_OK)
				_SelectDevice(index);
			break;
		}

		case kMsgRefresh:
			_SetStatus(B_TRANSLATE("Rescanning..."));
			{
				thread_id thread = spawn_thread(DeviceLoadEntry,
					"printomatic:devices", B_NORMAL_PRIORITY,
					this);
				if (thread >= B_OK)
					resume_thread(thread);
				thread = spawn_thread(PrinterLoadEntry,
					"printomatic:printers", B_NORMAL_PRIORITY,
					this);
				if (thread >= B_OK)
					resume_thread(thread);
			}
			break;

		case kMsgOptionChanged:
			_SaveSettings();
			break;

		case kMsgPreview:
			_StartPreview();
			break;

		case kMsgScanPage:
			_StartScan();
			break;

		case kMsgCancelScan:
			_CancelScan();
			break;

		case kMsgScanProgress:
		{
			float done = 0;
			message->FindFloat("done", &done);
			BString text;
			text << B_TRANSLATE("Scanning... ")
				<< (int32)(done * 100) << "%";
			_SetStatus(text.String());
			break;
		}

		case kMsgScanDone:
		{
			fBusy = false;
			BBitmap* bitmap = NULL;
			bool preview = false;
			message->FindPointer("bitmap", (void**)&bitmap);
			message->FindBool("preview", &preview);
			_ScanDone(bitmap, preview);
			_UpdateButtons();
			break;
		}

		case kMsgScanFailed:
		{
			fBusy = false;
			const char* error = "scan failed";
			message->FindString("error", &error);
			_SetStatus(error);
			_UpdateButtons();
			break;
		}

		case kMsgDeletePage:
			_RemoveSelectedPages();
			break;

		case kMsgMoveUp:
			_MoveSelectedPage(true);
			break;

		case kMsgMoveDown:
			_MoveSelectedPage(false);
			break;

		case kMsgPrintCopies:
			_DoPrint();
			break;

		case kMsgSaveFile:
			_DoSave();
			break;

		case B_SAVE_REQUESTED:
		{
			entry_ref ref;
			const char* name = NULL;
			if (message->FindRef("directory", &ref) != B_OK
				|| message->FindString("name", &name) != B_OK)
				break;

			BPath path(&ref);
			path.Append(name);

			status_t status = B_ERROR;
			const char* format = fFormatMenu->FindMarked()->Label();
			if (strcmp(format, "PDF") == 0) {
				BList& pages = fPages;
				// PDF assembles every scanned page.
				BList spoolPages;
				float paperWidth = 595;
				float paperHeight = 842;
				for (int32 i = 0; i < pages.CountItems(); i++) {
					BBitmap* bitmap = static_cast<BBitmap*>(
						pages.ItemAt(i));
					SpooledPage* page = printcups_new_page();
					fPageDrawView->ResizeTo(paperWidth,
						paperHeight);
					fPageDrawView->SetBitmap(bitmap);
					BPicture* picture = new BPicture;
					fPageDrawView->BeginPicture(picture);
					fPageDrawView->Draw(fPageDrawView->Bounds());
					picture = fPageDrawView->EndPicture();
					printcups_add_picture(page, picture,
						BRect(0, 0, paperWidth - 1,
							paperHeight - 1),
						BPoint(0, 0));
					spoolPages.AddItem(page);
				}
				status = printcups_render_pages_pdf(path.Path(),
					&spoolPages, paperWidth, paperHeight, 1.0f,
					"PrintOMatic");
				for (int32 i = 0; i < spoolPages.CountItems();
					i++)
					printcups_free_page(
						static_cast<SpooledPage*>(
							spoolPages.ItemAt(i)));
			} else if (fPages.CountItems() > 0) {
				BBitmap* bitmap
					= static_cast<BBitmap*>(fPages.ItemAt(0));
				if (strcmp(format, "PNG") == 0)
					status = WriteBitmapFile(bitmap, path.Path(),
						"image/png", B_PNG_FORMAT);
				else
					status = WriteBitmapFile(bitmap, path.Path(),
						"image/jpeg", B_JPEG_FORMAT);
			}

			if (status == B_OK) {
				_SetStatus(B_TRANSLATE("Saved"));
				_Notify(B_TRANSLATE("PrintOMatic"),
					B_TRANSLATE("Scan saved"));
			} else {
				_SetStatus(B_TRANSLATE("Could not save the file"));
				_Notify(B_TRANSLATE("PrintOMatic"),
					B_TRANSLATE("Could not save the file"));
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
PrintOMaticWindow::_LoadDevices()
{
	thread_id thread = spawn_thread(DeviceLoadEntry,
		"printomatic:devices", B_NORMAL_PRIORITY, this);
	if (thread >= B_OK)
		resume_thread(thread);
}


void
PrintOMaticWindow::_SelectDevice(int32 index)
{
	if (index < 0 || index >= fDeviceCount)
		return;

	if (fBusy)
		_CancelScan();
	if (fBatchActive) {
		libscan_batch_stop();
		fBatchActive = false;
	}

	strlcpy(fDeviceName, fDevices[index].name, sizeof(fDeviceName));
	_LoadDeviceOptions();
	_LoadSettings();
	_UpdateButtons();
}


void
PrintOMaticWindow::_LoadDeviceOptions()
{
	fDeviceOptions.MakeEmpty();
	if (fDeviceName[0] == '\0')
		return;

	if (libscan_get_options(fDeviceName, &fDeviceOptions) != B_OK) {
		_SetStatus(libscan_last_error());
		return;
	}
	_UpdateOptionMenus();
}


void
PrintOMaticWindow::_UpdateOptionMenus()
{
	fModeMenu->RemoveItems(0, fModeMenu->CountItems(), true);
	const char* currentMode = fDeviceOptions.GetString("mode", "Color");
	BStringList modes;
	fDeviceOptions.FindStrings("mode-choices", &modes);
	if (modes.CountStrings() == 0) {
		modes.Add("Color");
		modes.Add("Gray");
		modes.Add("Lineart");
	}
	for (int32 i = 0; i < modes.CountStrings(); i++) {
		BMessage* pick = new BMessage(kMsgOptionChanged);
		pick->AddString("mode", modes.StringAt(i));
		fModeMenu->AddItem(new BMenuItem(modes.StringAt(i).String(),
			pick));
		if (modes.StringAt(i) == currentMode)
			fModeMenu->ItemAt(i)->SetMarked(true);
	}
	if (fModeMenu->FindMarked() == NULL)
		fModeMenu->ItemAt(0)->SetMarked(true);

	fSourceMenu->RemoveItems(0, fSourceMenu->CountItems(), true);
	const char* currentSource
		= fDeviceOptions.GetString("source-name", "");
	BStringList sources;
	fDeviceOptions.FindStrings("source-choices", &sources);
	if (sources.CountStrings() == 0)
		sources.Add("Flatbed");
	int32 sourceIndex = fDeviceOptions.GetInt32("source", 0);
	for (int32 i = 0; i < sources.CountStrings(); i++) {
		BMessage* pick = new BMessage(kMsgOptionChanged);
		pick->AddString("source-name", sources.StringAt(i));
		// Map the word back to the flatbed/ADF/duplex index.
		int32 kind = 0;
		if (sources.StringAt(i).IFindFirst("duplex") >= 0)
			kind = 2;
		else if (sources.StringAt(i).IFindFirst("adf") >= 0)
			kind = 1;
		pick->AddInt32("source", kind);
		fSourceMenu->AddItem(new BMenuItem(sources.StringAt(i).String(),
			pick));
		if (sources.StringAt(i) == currentSource
			|| (currentSource[0] == '\0' && i == sourceIndex))
			fSourceMenu->ItemAt(i)->SetMarked(true);
	}
	if (fSourceMenu->FindMarked() == NULL)
		fSourceMenu->ItemAt(0)->SetMarked(true);

	fResolutionMenu->RemoveItems(0, fResolutionMenu->CountItems(), true);
	int32 currentDpi = fDeviceOptions.GetInt32("resolution", 300);
	BList choices;
	int32 choiceCount = 0;
	int32 value = 0;
	while (fDeviceOptions.FindInt32("resolution-choices", choiceCount,
			&value) == B_OK) {
		choices.AddItem((void*)(intptr_t)value);
		choiceCount++;
	}
	if (choices.CountItems() == 0) {
		choices.AddItem((void*)(intptr_t)75);
		choices.AddItem((void*)(intptr_t)150);
		choices.AddItem((void*)(intptr_t)300);
		choices.AddItem((void*)(intptr_t)600);
	}
	for (int32 i = 0; i < choices.CountItems(); i++) {
		int32 dpi = (int32)(intptr_t)choices.ItemAt(i);
		BString label;
		label << dpi << " " << B_TRANSLATE("dpi");
		BMessage* pick = new BMessage(kMsgOptionChanged);
		pick->AddInt32("resolution", dpi);
		fResolutionMenu->AddItem(new BMenuItem(label.String(), pick));
		if (dpi == currentDpi)
			fResolutionMenu->ItemAt(i)->SetMarked(true);
	}
	if (fResolutionMenu->FindMarked() == NULL) {
		int32 nearest = 0;
		int32 best = 0x7fffffff;
		for (int32 i = 0; i < choices.CountItems(); i++) {
			int32 dpi = (int32)(intptr_t)choices.ItemAt(i);
			int32 delta = dpi > currentDpi ? dpi - currentDpi
				: currentDpi - dpi;
			if (delta < best) {
				best = delta;
				nearest = i;
			}
		}
		fResolutionMenu->ItemAt(nearest)->SetMarked(true);
	}
}


void
PrintOMaticWindow::_UpdatePrinterMenu(const BMessage* message)
{
	fPrinterMenu->RemoveItems(0, fPrinterMenu->CountItems(), true);

	const char* saved = fCurrentDeviceSettings.GetString("printer", "");
	BString queue;
	bool found = false;
	for (int32 i = 0; message != NULL
			&& message->FindString("queue", i, &queue) == B_OK; i++) {
		BMessage* pick = new BMessage(kMsgOptionChanged);
		pick->AddString("printer", queue);
		fPrinterMenu->AddItem(new BMenuItem(queue.String(), pick));
		if (strcmp(queue.String(), saved) == 0) {
			fPrinterMenu->ItemAt(i)->SetMarked(true);
			found = true;
		}
	}

	if (fPrinterMenu->CountItems() == 0) {
		BMenuItem* none = new BMenuItem(B_TRANSLATE("(no printers)"),
			NULL);
		none->SetEnabled(false);
		fPrinterMenu->AddItem(none);
	} else if (!found)
		fPrinterMenu->ItemAt(0)->SetMarked(true);
	_UpdateButtons();
}


void
PrintOMaticWindow::_UpdateButtons()
{
	bool hasDevice = fDeviceName[0] != '\0';
	bool hasPages = fPages.CountItems() > 0;
	bool hasPrinters = fPrinterMenu->CountItems() > 0
		&& fPrinterMenu->ItemAt(0)->IsEnabled();

	fDeviceField->SetEnabled(!fBusy && hasDevice);
	fRefreshButton->SetEnabled(!fBusy);
	fSourceField->SetEnabled(!fBusy && hasDevice);
	fModeField->SetEnabled(!fBusy && hasDevice);
	fResolutionField->SetEnabled(!fBusy && hasDevice);

	fPreviewButton->SetEnabled(!fBusy && hasDevice);
	fScanButton->SetEnabled(!fBusy && hasDevice);
	fCancelButton->SetEnabled(fBusy);
	fPrintButton->SetEnabled(!fBusy && hasPages && hasPrinters
		&& fPrintRadio->Value() == B_CONTROL_ON);
	fSaveButton->SetEnabled(!fBusy && hasPages
		&& fSaveRadio->Value() == B_CONTROL_ON);

	fDeletePageButton->SetEnabled(hasPages);
	fMoveUpButton->SetEnabled(hasPages);
	fMoveDownButton->SetEnabled(hasPages);

	fPrinterField->SetEnabled(fPrintRadio->Value() == B_CONTROL_ON
		&& !fBusy);
	fCopiesControl->SetEnabled(fPrintRadio->Value() == B_CONTROL_ON
		&& !fBusy);
	fFormatField->SetEnabled(fSaveRadio->Value() == B_CONTROL_ON
		&& !fBusy);
}


void
PrintOMaticWindow::_SetStatus(const char* text)
{
	fStatusView->SetText(text);
}


void
PrintOMaticWindow::_AppendPage(BBitmap* bitmap)
{
	int32 number = fPages.CountItems() + 1;
	BString label;
	label << B_TRANSLATE("Page ") << number;
	fPages.AddItem(bitmap);
	fPageList->AddItem(new BStringItem(label.String()));
	fPageList->Select(fPageList->CountItems() - 1);
}


void
PrintOMaticWindow::_RemoveSelectedPages()
{
	int32 index = fPageList->CurrentSelection();
	while (index >= 0) {
		delete static_cast<BBitmap*>(fPages.ItemAt(index));
		fPages.RemoveItem(index);
		fPageList->RemoveItem(index);
		index = fPageList->CurrentSelection();
	}
	_UpdateButtons();
}


void
PrintOMaticWindow::_MoveSelectedPage(bool up)
{
	int32 index = fPageList->CurrentSelection();
	int32 target = up ? index - 1 : index + 1;
	if (index < 0 || target < 0 || target >= fPages.CountItems())
		return;

	fPages.SwapItems(index, target);
	BStringItem* item
		= static_cast<BStringItem*>(fPageList->ItemAt(index));
	fPageList->SwapItems(index, target);
	fPageList->Select(target);
	(void)item;
}


libscan_options
PrintOMaticWindow::_BuildOptions(bool preview)
{
	libscan_options opts;
	memset(&opts, 0, sizeof(opts));
	opts.resolution_dpi = 75;
	opts.format = LIBSCAN_RGB24;

	BMenuItem* resolution = fResolutionMenu->FindMarked();
	if (resolution != NULL && resolution->Message() != NULL)
		resolution->Message()->FindInt32("resolution",
			&opts.resolution_dpi);
	if (preview && opts.resolution_dpi > 150)
		opts.resolution_dpi = 75;

	BMenuItem* mode = fModeMenu->FindMarked();
	const char* modeWord = "Color";
	if (mode != NULL)
		modeWord = mode->Label();
	strlcpy(opts.mode, modeWord, sizeof(opts.mode));
	if (strcasestr(modeWord, "gray") != NULL
		|| strcasestr(modeWord, "grey") != NULL)
		opts.format = LIBSCAN_GRAY8;
	else if (strcasestr(modeWord, "lineart") != NULL
		|| strcasestr(modeWord, "black") != NULL
		|| strcasestr(modeWord, "binary") != NULL)
		opts.format = LIBSCAN_BINARY;

	BMenuItem* source = fSourceMenu->FindMarked();
	if (source != NULL && source->Message() != NULL) {
		source->Message()->FindInt32("source", &opts.source);
		const char* name = NULL;
		if (source->Message()->FindString("source-name", &name)
				== B_OK)
			strlcpy(opts.source_name, name,
				sizeof(opts.source_name));
	}

	// The preview is scanned at 75 dpi over the whole page; the area
	// selection maps through that scale into inches.
	float pageWidth = fDeviceOptions.GetFloat("max-width", 8.5f);
	float pageHeight = fDeviceOptions.GetFloat("max-height", 11.0f);
	if (pageWidth <= 0)
		pageWidth = 8.5f;
	if (pageHeight <= 0)
		pageHeight = 11.0f;
	opts.tl_x = 0;
	opts.tl_y = 0;
	opts.br_x = pageWidth;
	opts.br_y = pageHeight;

	BBitmap* previewBitmap = fPreviewView->Bitmap();
	if (previewBitmap != NULL && fPreviewView->HasSelection()) {
		BRect selection = fPreviewView->Selection();
		BRect bounds = previewBitmap->Bounds();
		if (bounds.Width() > 0 && bounds.Height() > 0) {
			opts.tl_x = selection.left / bounds.Width()
				* pageWidth;
			opts.tl_y = selection.top / bounds.Height()
				* pageHeight;
			opts.br_x = selection.right / bounds.Width()
				* pageWidth;
			opts.br_y = selection.bottom / bounds.Height()
				* pageHeight;
		}
	}

	return opts;
}


void
PrintOMaticWindow::_StartPreview()
{
	if (fBusy || fDeviceName[0] == '\0')
		return;

	fBusy = true;
	_SetStatus(B_TRANSLATE("Scanning preview..."));
	_UpdateButtons();

	fScanThread = spawn_thread(PreviewThreadEntry,
		"printomatic:preview", B_NORMAL_PRIORITY, this);
	if (fScanThread < B_OK) {
		fBusy = false;
		_SetStatus(B_TRANSLATE("Could not start the scan thread"));
		_UpdateButtons();
		return;
	}
	resume_thread(fScanThread);
}


void
PrintOMaticWindow::_StartScan()
{
	if (fBusy || fDeviceName[0] == '\0')
		return;

	fBusy = true;
	_SetStatus(B_TRANSLATE("Scanning..."));
	_UpdateButtons();

	fScanThread = spawn_thread(ScanThreadEntry, "printomatic:scan",
		B_NORMAL_PRIORITY, this);
	if (fScanThread < B_OK) {
		fBusy = false;
		_SetStatus(B_TRANSLATE("Could not start the scan thread"));
		_UpdateButtons();
		return;
	}
	resume_thread(fScanThread);
}


void
PrintOMaticWindow::_CancelScan()
{
	libscan_scan_cancel();
	libscan_preview_cancel();
	if (fBatchActive) {
		libscan_batch_stop();
		fBatchActive = false;
	}
}


// Runs on the worker thread; state is read under the window lock and
// the (slow) SANE calls run without it.
void
PrintOMaticWindow::_RunScan(bool preview)
{
	Lock();
	char device[256];
	strlcpy(device, fDeviceName, sizeof(device));
	libscan_options opts = _BuildOptions(preview);
	bool useBatch = opts.source > 0;
	Unlock();

	BBitmap* bitmap = NULL;
	status_t status;

	if (preview) {
		status = libscan_preview_start(device, &opts, ProgressCallback,
			this);
		if (status == B_OK)
			status = libscan_preview_wait(&bitmap);
	} else if (useBatch) {
		if (!fBatchActive) {
			status = libscan_batch_start(device, &opts);
			if (status == B_OK)
				fBatchActive = true;
		} else
			status = B_OK;
		if (status == B_OK)
			status = libscan_batch_next_page(ProgressCallback,
				this, &bitmap);
	} else {
		status = libscan_scan_page(device, &opts, ProgressCallback,
			this, &bitmap);
	}

	BMessage done(kMsgScanDone);
	if (status == B_OK && bitmap != NULL) {
		done.AddPointer("bitmap", bitmap);
		done.AddBool("preview", preview);
	} else {
		BMessage failed(kMsgScanFailed);
		failed.AddString("error", libscan_last_error());
		PostMessage(&failed);
		return;
	}
	PostMessage(&done);
}


void
PrintOMaticWindow::_ScanDone(BBitmap* bitmap, bool preview)
{
	if (preview) {
		fPreviewView->SetBitmap(bitmap);
		_SetStatus(B_TRANSLATE("Preview ready"));
		return;
	}

	// Convert RGB24 to B_RGB32 once so print, PDF and the
	// Translation Kit all see one pixel format.
	BRect bounds = bitmap->Bounds();
	BBitmap* converted = new BBitmap(bounds, B_BITMAP_NO_SERVER_LINK,
		B_RGB32);
	if (converted->InitCheck() == B_OK) {
		for (int32 y = 0; y <= bounds.IntegerHeight(); y++) {
			const uint8* src = static_cast<const uint8*>(
				bitmap->Bits()) + (size_t)y
					* bitmap->BytesPerRow();
			uint8* dst = static_cast<uint8*>(converted->Bits())
				+ (size_t)y * converted->BytesPerRow();
			for (int32 x = 0; x <= bounds.IntegerWidth(); x++) {
				dst[x * 4 + 0] = src[x * 3 + 2];
				dst[x * 4 + 1] = src[x * 3 + 1];
				dst[x * 4 + 2] = src[x * 3 + 0];
				dst[x * 4 + 3] = 0xff;
			}
		}
		delete bitmap;
		bitmap = converted;
	} else {
		delete converted;
	}

	// Grey8 stays as-is; libscan already expanded line art to RGB32.
	_AppendPage(bitmap);
	BString text;
	text << B_TRANSLATE("Scanned page ") << fPages.CountItems();
	_SetStatus(text.String());
}


void
PrintOMaticWindow::_DoPrint()
{
	if (fPages.CountItems() == 0)
		return;

	BMenuItem* printer = fPrinterMenu->FindMarked();
	if (printer == NULL || printer->Message() == NULL) {
		_SetStatus(B_TRANSLATE("No printer queue. Scan to file "
			"still available."));
		return;
	}
	const char* queue = printer->Label();
	int32 copies = atoi(fCopiesControl->TextView()->Text());
	if (copies < 1)
		copies = 1;

	BMessage settings;
	settings.AddString(PSRV_FIELD_CURRENT_PRINTER, queue);
	settings.AddString("printer_name", queue);
	printcups_default_settings(&settings);
	settings.AddInt32(PSRV_FIELD_COPIES, copies);
	settings.AddInt32(PSRV_FIELD_FIRST_PAGE, 1);
	settings.AddInt32(PSRV_FIELD_LAST_PAGE, fPages.CountItems());

	BPrintJob job("PrintOMatic");
	job.SetSettings(new BMessage(settings));

	BRect printable = job.PrintableRect();
	PageDrawView* drawView = static_cast<PageDrawView*>(ChildAt(0));
	drawView->ResizeTo(printable.Width() + 1, printable.Height() + 1);

	job.BeginJob();
	for (int32 i = 0; i < fPages.CountItems(); i++) {
		drawView->SetBitmap(static_cast<BBitmap*>(fPages.ItemAt(i)));
		drawView->Sync();
		job.DrawView(drawView, drawView->Bounds(), BPoint(0, 0));
		job.SpoolPage();
	}
	job.CommitJob();

	BString text;
	text << B_TRANSLATE("Sent ") << fPages.CountItems()
		<< B_TRANSLATE(" page(s) to ") << queue;
	_SetStatus(text.String());
	_Notify(B_TRANSLATE("PrintOMatic"), text.String());
	_SaveSettings();
}


void
PrintOMaticWindow::_DoSave()
{
	if (fPages.CountItems() == 0)
		return;

	BFilePanel* panel = new BFilePanel(B_SAVE_PANEL,
		new BMessenger(this), NULL, 0, false, NULL, NULL);
	panel->Show();
}


void
PrintOMaticWindow::_Notify(const char* title, const char* content)
{
	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetTitle(title);
	notification.SetContent(content);
	notification.Send();
}


void
PrintOMaticWindow::_SaveSettings()
{
	// Remember the choices for the current device.
	if (fDeviceName[0] != '\0') {
		fCurrentDeviceSettings.MakeEmpty();
		BMenuItem* mode = fModeMenu->FindMarked();
		if (mode != NULL)
			fCurrentDeviceSettings.AddString("mode", mode->Label());
		BMenuItem* source = fSourceMenu->FindMarked();
		if (source != NULL && source->Message() != NULL) {
			const char* name = NULL;
			int32 kind = 0;
			source->Message()->FindString("source-name", &name);
			source->Message()->FindInt32("source", &kind);
			if (name != NULL)
				fCurrentDeviceSettings.AddString("source-name",
					name);
			fCurrentDeviceSettings.AddInt32("source", kind);
		}
		BMenuItem* resolution = fResolutionMenu->FindMarked();
		if (resolution != NULL && resolution->Message() != NULL) {
			int32 dpi = 0;
			resolution->Message()->FindInt32("resolution", &dpi);
			fCurrentDeviceSettings.AddInt32("resolution", dpi);
		}
		BMenuItem* printer = fPrinterMenu->FindMarked();
		if (printer != NULL && printer->Message() != NULL) {
			const char* queue = printer->Label();
			fCurrentDeviceSettings.AddString("printer", queue);
		}
		fCurrentDeviceSettings.AddInt32("copies",
			fCopiesControl->Value());
		fCurrentDeviceSettings.AddBool("save-mode",
			fSaveRadio->Value() == B_CONTROL_ON);
		fCurrentDeviceSettings.AddString("format",
			fFormatMenu->FindMarked()->Label());

		fSettings.RemoveName(fDeviceName);
		fSettings.AddMessage(fDeviceName, &fCurrentDeviceSettings);
	}

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK
		|| path.Append(kSettingsFileName) != B_OK)
		return;
	BFile file(path.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		fSettings.Flatten(&file);
}


void
PrintOMaticWindow::_LoadSettings()
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
		&& path.Append(kSettingsFileName) == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			fSettings.Unflatten(&file);
	}

	BMessage* deviceSettings = _DeviceSettings(false);
	if (deviceSettings == NULL)
		return;
	fCurrentDeviceSettings = *deviceSettings;
	delete deviceSettings;

	const char* mode = deviceSettings->GetString("mode", NULL);
	if (mode != NULL) {
		for (int32 i = 0; i < fModeMenu->CountItems(); i++) {
			BMenuItem* item = fModeMenu->ItemAt(i);
			if (strcmp(item->Label(), mode) == 0) {
				item->SetMarked(true);
				break;
			}
		}
	}
	const char* sourceName = deviceSettings->GetString("source-name",
		NULL);
	if (sourceName != NULL) {
		for (int32 i = 0; i < fSourceMenu->CountItems(); i++) {
			BMenuItem* item = fSourceMenu->ItemAt(i);
			if (strcmp(item->Label(), sourceName) == 0) {
				item->SetMarked(true);
				break;
			}
		}
	}
	int32 dpi = deviceSettings->GetInt32("resolution", 0);
	if (dpi > 0) {
		for (int32 i = 0; i < fResolutionMenu->CountItems(); i++) {
			BMenuItem* item = fResolutionMenu->ItemAt(i);
			BMessage* message = item->Message();
			int32 value = 0;
			if (message != NULL
				&& message->FindInt32("resolution", &value)
					== B_OK && value == dpi) {
				item->SetMarked(true);
				break;
			}
		}
	}
	int32 copies = deviceSettings->GetInt32("copies", 1);
	if (copies >= 1 && copies <= 99) {
		BString value;
		value << copies;
		fCopiesControl->SetText(value.String());
	}
	const char* format = deviceSettings->GetString("format", "PDF");
	BMenuItem* formatItem = fFormatMenu->FindItem(format);
	if (formatItem != NULL)
		formatItem->SetMarked(true);
	if (deviceSettings->GetBool("save-mode", false)) {
		fSaveRadio->SetValue(B_CONTROL_ON);
		fPrintRadio->SetValue(B_CONTROL_OFF);
	}
}


BMessage*
PrintOMaticWindow::_DeviceSettings(bool create)
{
	if (fDeviceName[0] == '\0')
		return NULL;

	// The caller owns the returned copy.
	BMessage* settings = new BMessage;
	if (fSettings.FindMessage(fDeviceName, 0, settings) == B_OK)
		return settings;
	delete settings;

	if (!create)
		return NULL;
	fCurrentDeviceSettings.MakeEmpty();
	fSettings.AddMessage(fDeviceName, &fCurrentDeviceSettings);
	settings = new BMessage(fCurrentDeviceSettings);
	return settings;
}
