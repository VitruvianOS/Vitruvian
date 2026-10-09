/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * SetupPanels: page-setup and print-setup windows shown in the calling
 * application's own process (no print_server).
 */

#include "SetupPanels.h"

#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <Rect.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CupsBridge.h"
#include "printcups.h"
#include "pr_server.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrintSetupPanels"


class PaperItem : public BMenuItem {
public:
	PaperItem(const char* label, const char* media, BRect paper, BRect printable)
		:
		BMenuItem(label, new BMessage(kMsgPaperSelected)),
		fMedia(media),
		fPaper(paper),
		fPrintable(printable)
	{
	}

	const char*		Media() const { return fMedia.String(); }
	BRect			Paper() const { return fPaper; }
	BRect			_Printable() const { return fPrintable; }

private:
	BString		fMedia;
	BRect		fPaper;
	BRect		fPrintable;
};


static BRect
PaperRectForMedia(const char* media, bool* ok)
{
	printcups_queue_info info;
	CupsBridge bridge;
	if (bridge.DefaultQueue(info.name, sizeof(info.name)) != B_OK
		|| bridge.QueueInfo(info.name, &info) != B_OK) {
		*ok = false;
		return BRect(0, 0, 595, 841);
	}

	*ok = true;
	if (media == NULL)
		media = info.media;

	// Prefer the queue's geometry when media matches.
	if (strcmp(media, info.media) == 0) {
		return BRect(0, 0, info.paper_width - 1, info.paper_height - 1);
	}

	// Fallback table for common media.
	if (strcasecmp(media, "A4") == 0 || strncasecmp(media, "iso_a4", 6) == 0)
		return BRect(0, 0, 595, 841);
	if (strcasecmp(media, "Letter") == 0
		|| strncasecmp(media, "us_letter", 9) == 0)
		return BRect(0, 0, 612, 791);
	if (strcasecmp(media, "Legal") == 0
		|| strncasecmp(media, "us_legal", 8) == 0)
		return BRect(0, 0, 612, 1007);
	if (strcasecmp(media, "A3") == 0 || strncasecmp(media, "iso_a3", 6) == 0)
		return BRect(0, 0, 842, 1190);
	if (strcasecmp(media, "A5") == 0 || strncasecmp(media, "iso_a5", 6) == 0)
		return BRect(0, 0, 420, 595);

	return BRect(0, 0, info.paper_width - 1, info.paper_height - 1);
}


static BRect
PrintableRectFor(const BRect& paper, const char* media)
{
	printcups_queue_info info;
	CupsBridge bridge;
	if (bridge.DefaultQueue(info.name, sizeof(info.name)) != B_OK
		|| bridge.QueueInfo(info.name, &info) != B_OK)
		return BRect(18, 18, paper.right - 18, paper.bottom - 18);

	return BRect(info.margin_left, info.margin_top,
		paper.right - info.margin_right, paper.bottom - info.margin_bottom);
}


static void
FillSettingsFromQueue(BMessage* settings, const printcups_queue_info& info)
{
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

	if (!settings->HasInt32(PSRV_FIELD_COPIES))
		settings->AddInt32(PSRV_FIELD_COPIES, 1);
	if (!settings->HasInt32(PSRV_FIELD_ORIENTATION))
		settings->AddInt32(PSRV_FIELD_ORIENTATION, 0);
	if (!settings->HasInt32(PSRV_FIELD_SCALE))
		settings->AddInt32(PSRV_FIELD_SCALE, 100);
	if (!settings->HasInt32(PSRV_FIELD_QUALITY))
		settings->AddInt32(PSRV_FIELD_QUALITY, 100);
}


// #pragma mark - PreviewView


class PreviewView : public BView {
public:
	PreviewView()
		:
		BView("preview", B_WILL_DRAW | B_FRAME_EVENTS),
		fPaper(0, 0, 595, 841),
		fPrintable(18, 18, 577, 823)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(160, 160));
	}

	void SetPaper(BRect paper, BRect printable)
	{
		fPaper = paper;
		fPrintable = printable;
		Invalidate();
	}

	virtual void FrameResized(float width, float height)
	{
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		BRect bounds = Bounds().InsetByCopy(8, 8);
		if (fPaper.Width() <= 0 || fPaper.Height() <= 0)
			return;

		float scale = bounds.Width() / fPaper.Width();
		if (bounds.Height() / fPaper.Height() < scale)
			scale = bounds.Height() / fPaper.Height();

		BRect page(0, 0, fPaper.Width() * scale, fPaper.Height() * scale);
		page.OffsetTo(bounds.left + (bounds.Width() - page.Width()) / 2,
			bounds.top + (bounds.Height() - page.Height()) / 2);

		SetHighColor(255, 255, 255);
		FillRect(page);
		SetHighColor(0, 0, 0);
		StrokeRect(page);

		BRect printable(page.left + fPrintable.left * scale,
			page.top + fPrintable.top * scale,
			page.left + fPrintable.right * scale,
			page.top + fPrintable.bottom * scale);
		SetHighColor(128, 128, 255);
		StrokeRect(printable);
	}

private:
	BRect	fPaper;
	BRect	fPrintable;
};


// #pragma mark - PageSetupWindow


PageSetupWindow::PageSetupWindow(BMessage* settings, bool* canceled,
	sem_id done)
	:
	BWindow(BRect(0, 0, 100, 100), B_TRANSLATE("Page setup"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fSettings(settings),
	fCanceled(canceled),
	fDone(done),
	fFinished(false),
	fOrientation(0),
	fPaperMenu(NULL),
	fOrientationMenu(NULL),
	fPreview(NULL)
{
	*fCanceled = true;

	const char* media = NULL;
	if (settings != NULL) {
		settings->FindString("media", &media);
		settings->FindInt32(PSRV_FIELD_ORIENTATION, &fOrientation);
	}

	fPaperMenu = new BPopUpMenu("paper");
	static const char* kMediaNames[] = {
		"A4", "Letter", "Legal", "A3", "A5", NULL
	};
	for (int32 i = 0; kMediaNames[i] != NULL; i++) {
		bool ok = false;
		BRect paper = PaperRectForMedia(kMediaNames[i], &ok);
		PaperItem* item = new PaperItem(kMediaNames[i], kMediaNames[i], paper,
			PrintableRectFor(paper, kMediaNames[i]));
		fPaperMenu->AddItem(item);
		if (media != NULL && strcasecmp(media, kMediaNames[i]) == 0)
			item->SetMarked(true);
	}
	if (fPaperMenu->FindMarked() == NULL)
		fPaperMenu->ItemAt(0)->SetMarked(true);

	fOrientationMenu = new BPopUpMenu("orientation");
	fOrientationMenu->AddItem(new BMenuItem(B_TRANSLATE("Portrait"),
		new BMessage(kMsgOrientationChanged)));
	fOrientationMenu->AddItem(new BMenuItem(B_TRANSLATE("Landscape"),
		new BMessage(kMsgOrientationChanged)));
	fOrientationMenu->ItemAt(fOrientation == 1 ? 1 : 0)->SetMarked(true);

	BMenuField* paperField = new BMenuField("paperField",
		B_TRANSLATE("Paper size:"), fPaperMenu);
	BMenuField* orientField = new BMenuField("orientField",
		B_TRANSLATE("Orientation:"), fOrientationMenu);

	fPreview = new PreviewView();
	BBox* box = new BBox("previewBox");
	box->SetLabel(B_TRANSLATE("Preview"));
	BLayoutBuilder::Group<>(box, B_VERTICAL)
		.SetInsets(B_USE_SMALL_SPACING, B_USE_BIG_SPACING,
			B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
		.Add(fPreview);

	BButton* okButton = new BButton("ok", B_TRANSLATE("OK"),
		new BMessage(kMsgPageSetupOk));
	BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgPageSetupCancel));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL)
			.AddGrid()
				.AddMenuField(paperField, 0, 0)
				.AddMenuField(orientField, 0, 1)
				.AddGlue(0, 2)
			.End()
			.Add(box)
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancelButton)
			.Add(okButton)
		.End();

	okButton->MakeDefault(true);
	fPaperMenu->SetTargetForItems(this);
	fOrientationMenu->SetTargetForItems(this);

	_UpdateSettings();
	_UpdatePreview();
	CenterOnScreen();
}


void
PageSetupWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOrientationChanged:
			fOrientation = fOrientationMenu->IndexOf(
				fOrientationMenu->FindMarked()) == 1 ? 1 : 0;
			// fall through
		case kMsgPaperSelected:
			_UpdateSettings();
			_UpdatePreview();
			break;

		case kMsgPageSetupOk:
			_UpdateSettings();
			_Finish(false);
			break;

		case kMsgPageSetupCancel:
			_Finish(true);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
PageSetupWindow::QuitRequested()
{
	// Closing the window or Escape is a cancel.
	_Release(true);
	return true;
}


void
PageSetupWindow::_Finish(bool canceled)
{
	_Release(canceled);
	Quit();
}


void
PageSetupWindow::_Release(bool canceled)
{
	if (fFinished)
		return;

	fFinished = true;
	*fCanceled = canceled;
	if (fDone >= 0) {
		sem_id id = fDone;
		fDone = -1;
		release_sem(id);
	}
}


void
PageSetupWindow::_UpdateSettings()
{
	if (fSettings == NULL)
		return;

	PaperItem* item = dynamic_cast<PaperItem*>(fPaperMenu->FindMarked());
	if (item != NULL) {
		BRect paper = item->Paper();
		if (fOrientation == 1)
			paper = BRect(0, 0, paper.Height(), paper.Width());

		fSettings->RemoveName(PSRV_FIELD_PAPER_RECT);
		fSettings->AddRect(PSRV_FIELD_PAPER_RECT, paper);
		fSettings->RemoveName(PSRV_FIELD_PRINTABLE_RECT);
		fSettings->AddRect(PSRV_FIELD_PRINTABLE_RECT,
			PrintableRectFor(paper, item->Media()));
		fSettings->RemoveName("media");
		fSettings->AddString("media", item->Media());
	}

	fSettings->RemoveName(PSRV_FIELD_ORIENTATION);
	fSettings->AddInt32(PSRV_FIELD_ORIENTATION, fOrientation);
}


void
PageSetupWindow::_UpdatePreview()
{
	BRect paper(0, 0, 595, 841);
	BRect printable(18, 18, 577, 823);
	if (fSettings != NULL) {
		fSettings->FindRect(PSRV_FIELD_PAPER_RECT, &paper);
		fSettings->FindRect(PSRV_FIELD_PRINTABLE_RECT, &printable);
	}
	fPreview->SetPaper(paper, printable);
}


// #pragma mark - JobSetupWindow


JobSetupWindow::JobSetupWindow(BMessage* settings, bool* canceled,
	sem_id done)
	:
	BWindow(BRect(0, 0, 100, 100), B_TRANSLATE("Print"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fSettings(settings),
	fCanceled(canceled),
	fDone(done),
	fFinished(false),
	fCopiesField(NULL),
	fFirstField(NULL),
	fLastField(NULL)
{
	*fCanceled = true;

	const char* printer = NULL;
	int32 copies = 1;
	int32 first = 1;
	int32 last = INT32_MAX;
	if (settings != NULL) {
		settings->FindString(PSRV_FIELD_CURRENT_PRINTER, &printer);
		settings->FindInt32(PSRV_FIELD_COPIES, &copies);
		settings->FindInt32(PSRV_FIELD_FIRST_PAGE, &first);
		settings->FindInt32(PSRV_FIELD_LAST_PAGE, &last);
	}

	BStringView* printerLabel = new BStringView("printerLabel",
		B_TRANSLATE("Printer:"));
	BStringView* printerName = new BStringView("printerName",
		printer != NULL && printer[0] != '\0'
			? printer : B_TRANSLATE("(default)"));

	BString text;
	text << (copies > 0 ? copies : 1);
	fCopiesField = new BTextControl("copies", B_TRANSLATE("Copies:"),
		text.String(), NULL);

	text = "";
	text << (first > 0 ? first : 1);
	fFirstField = new BTextControl("first", B_TRANSLATE("First page:"),
		text.String(), NULL);

	// Empty means through the last page.
	text = "";
	if (last > 0 && last < INT32_MAX)
		text << last;
	fLastField = new BTextControl("last", B_TRANSLATE("Last page:"),
		text.String(), NULL);
	fLastField->SetToolTip(B_TRANSLATE("Leave empty to print to the end."));

	BButton* okButton = new BButton("ok", B_TRANSLATE("Print"),
		new BMessage(kMsgJobSetupOk));
	BButton* cancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(kMsgJobSetupCancel));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGrid()
			.Add(printerLabel, 0, 0)
			.Add(printerName, 1, 0)
			.AddTextControl(fCopiesField, 0, 1)
			.AddTextControl(fFirstField, 0, 2)
			.AddTextControl(fLastField, 0, 3)
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancelButton)
			.Add(okButton)
		.End();

	okButton->MakeDefault(true);
	CenterOnScreen();
}


void
JobSetupWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgJobSetupOk:
			_UpdateSettings();
			_Finish(false);
			break;

		case kMsgJobSetupCancel:
			_Finish(true);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
JobSetupWindow::QuitRequested()
{
	// Closing the window or Escape is a cancel.
	_Release(true);
	return true;
}


void
JobSetupWindow::_Finish(bool canceled)
{
	_Release(canceled);
	Quit();
}


void
JobSetupWindow::_Release(bool canceled)
{
	if (fFinished)
		return;

	fFinished = true;
	*fCanceled = canceled;
	if (fDone >= 0) {
		sem_id id = fDone;
		fDone = -1;
		release_sem(id);
	}
}


void
JobSetupWindow::_UpdateSettings()
{
	if (fSettings == NULL)
		return;

	int32 copies = atoi(fCopiesField->Text());
	int32 first = atoi(fFirstField->Text());
	int32 last = fLastField->Text()[0] != '\0'
		? atoi(fLastField->Text()) : INT32_MAX;
	if (copies < 1)
		copies = 1;
	if (first < 1)
		first = 1;
	if (last < first)
		last = first;

	fSettings->RemoveName(PSRV_FIELD_COPIES);
	fSettings->AddInt32(PSRV_FIELD_COPIES, copies);
	fSettings->RemoveName(PSRV_FIELD_FIRST_PAGE);
	fSettings->AddInt32(PSRV_FIELD_FIRST_PAGE, first);
	fSettings->RemoveName(PSRV_FIELD_LAST_PAGE);
	fSettings->AddInt32(PSRV_FIELD_LAST_PAGE, last);
}
