/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "PrinterOptionsDialog.h"

#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <String.h>
#include <TextControl.h>

#include "Messages.h"
#include "PrinterWorker.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Printers"


static void
ClearStringList(BList& list)
{
	for (int32 i = 0; i < list.CountItems(); i++)
		delete static_cast<BString*>(list.ItemAt(i));
	list.MakeEmpty();
}


PrinterOptionsDialog::PrinterOptionsDialog(const BMessenger& replyTo,
	const BString& queue, const printcups_queue_info& info, bool renameMode)
	:
	BWindow(BRect(100, 100, 420, renameMode ? 220 : 340),
		renameMode ? B_TRANSLATE("Rename Printer")
			: B_TRANSLATE("Printer Options"),
		B_TITLED_WINDOW, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE),
	fReplyTo(replyTo),
	fQueue(queue),
	fInfo(info),
	fRenameMode(renameMode),
	fBusy(false),
	fDescriptionField(NULL),
	fLocationField(NULL),
	fMediaMenu(NULL),
	fSidesMenu(NULL),
	fColorMenu(NULL),
	fQualityMenu(NULL)
{
	fSaveButton = new BButton("save", B_TRANSLATE("Save"),
		new BMessage(kMsgOptionsSave));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	if (renameMode)
		_SetupRename();
	else
		_SetupOptions();

	if (renameMode) {
		// Save/cancel already in the group from _SetupRename's grid plus glue.
	} else if (fSaveButton->Parent() == NULL) {
	}

	if (fSaveButton->Parent() == NULL) {
		BLayoutBuilder::Group<>(this, B_VERTICAL)
			.SetInsets(B_USE_WINDOW_SPACING)
			.AddGlue()
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(fSaveButton)
				.Add(fCancelButton)
			.End();
	}

	fSaveButton->SetTarget(this);
	fCancelButton->SetTarget(this);
	_UpdateBusy(false);
	CenterOnScreen();

	if (!renameMode) {
		_LoadChoices("media", fMediaMenu, fInfo.media);
		_LoadChoices("sides", fSidesMenu, NULL);
		_LoadChoices("print-color-mode", fColorMenu, NULL);
	}
}


void
PrinterOptionsDialog::_SetupRename()
{
	fDescriptionField = new BTextControl("description",
		B_TRANSLATE("Description:"), fInfo.name, NULL);
	fLocationField = new BTextControl("location",
		B_TRANSLATE("Location:"), fInfo.location, NULL);

	BLayoutBuilder::Grid<>(this)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fDescriptionField, 0, 0, B_ALIGN_LEFT)
		.Add(fLocationField, 0, 1, B_ALIGN_LEFT)
		.AddGlue(0, 2)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 3, 2, 2)
			.AddGlue()
			.Add(fSaveButton)
			.Add(fCancelButton)
		.End();
}


void
PrinterOptionsDialog::_SetupOptions()
{
	fMediaMenu = new BPopUpMenu("media");
	fSidesMenu = new BPopUpMenu("sides");
	fColorMenu = new BPopUpMenu("color");
	fQualityMenu = new BPopUpMenu("quality");

	// Quality is fixed: draft/normal/best mapped in _Save().
	fQualityMenu->AddItem(new BMenuItem(B_TRANSLATE("Draft"),
		new BMessage(kMsgOptionsChoices)));
	fQualityMenu->AddItem(new BMenuItem(B_TRANSLATE("Normal"),
		new BMessage(kMsgOptionsChoices)));
	fQualityMenu->AddItem(new BMenuItem(B_TRANSLATE("Best"),
		new BMessage(kMsgOptionsChoices)));
	fQualityMenu->ItemAt(1)->SetMarked(true);
	fQualityValues.AddItem(new BString("2"));

	BBox* box = new BBox("optionsBox");
	box->SetLabel(fQueue);
	BLayoutBuilder::Grid<>(box)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.AddMenuField(new BMenuField("media", B_TRANSLATE("Paper:"),
			fMediaMenu), 0, 0)
		.AddMenuField(new BMenuField("sides", B_TRANSLATE("Sides:"),
			fSidesMenu), 0, 1)
		.AddMenuField(new BMenuField("color", B_TRANSLATE("Colour:"),
			fColorMenu), 0, 2)
		.AddMenuField(new BMenuField("quality", B_TRANSLATE("Quality:"),
			fQualityMenu), 0, 3);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(box)
		.AddGlue()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fSaveButton)
			.Add(fCancelButton)
		.End();
}


void
PrinterOptionsDialog::_LoadChoices(const char* option, BPopUpMenu* menu,
	const char* current)
{
	if (menu == NULL)
		return;

	BList* values = NULL;
	if (menu == fMediaMenu)
		values = &fMediaValues;
	else if (menu == fSidesMenu)
		values = &fSidesValues;
	else if (menu == fColorMenu)
		values = &fColorValues;

	menu->RemoveItems(0, menu->CountItems(), true);
	if (values != NULL)
		ClearStringList(*values);

	// Always offer "leave as is" first.
	menu->AddItem(new BMenuItem(B_TRANSLATE("Printer default"),
		new BMessage(kMsgOptionsChoices)));
	if (values != NULL)
		values->AddItem(new BString(""));

	printcups_option_choice choices[32];
	int count = printcups_list_choices(fQueue.String(), option, choices, 32);
	int marked = 0;
	for (int i = 0; i < count; i++) {
		BString label(choices[i].text);
		if (label.IsEmpty())
			label = choices[i].value;
		menu->AddItem(new BMenuItem(label.String(),
			new BMessage(kMsgOptionsChoices)));
		if (values != NULL)
			values->AddItem(new BString(choices[i].value));
		if (current != NULL && choices[i].value[0] != '\0'
			&& strcasecmp(choices[i].value, current) == 0)
			marked = menu->CountItems() - 1;
	}

	// Map legacy media names onto cups keys when the printer lists them.
	if (current != NULL && current[0] != '\0' && values != NULL) {
		for (int32 i = 1; i < values->CountItems(); i++) {
			BString* value = static_cast<BString*>(values->ItemAt(i));
			if (value != NULL && strcasecmp(value->String(), current) == 0)
				marked = i;
		}
	}

	if (menu->CountItems() > 0)
		menu->ItemAt(marked)->SetMarked(true);
}


	BString
PrinterOptionsDialog::_MarkedValue(BPopUpMenu* menu, BList& values)
{
	int32 index = menu->FindMarkedIndex();
	if (index < 0 || index >= values.CountItems())
		return BString();
	BString* value = static_cast<BString*>(values.ItemAt(index));
	return value != NULL ? *value : BString();
}


void
PrinterOptionsDialog::_UpdateBusy(bool busy)
{
	fBusy = busy;
	fSaveButton->SetEnabled(!busy);
	fCancelButton->SetEnabled(!busy);
	if (fDescriptionField != NULL)
		fDescriptionField->SetEnabled(!busy);
	if (fLocationField != NULL)
		fLocationField->SetEnabled(!busy);
	if (fMediaMenu != NULL)
		fMediaMenu->SetEnabled(!busy);
	if (fSidesMenu != NULL)
		fSidesMenu->SetEnabled(!busy);
	if (fColorMenu != NULL)
		fColorMenu->SetEnabled(!busy);
	if (fQualityMenu != NULL)
		fQualityMenu->SetEnabled(!busy);
}


void
PrinterOptionsDialog::_Save()
{
	if (fRenameMode) {
		BString description(fDescriptionField->Text());
		BString location(fLocationField->Text());
		description.Trim();
		location.Trim();
		PrinterWorker::Post(kWorkerOpRenamePrinter, fReplyTo, fQueue,
			description, location);
		Quit();
		return;
	}

	printcups_options options;
	memset(&options, 0, sizeof(options));

	BString media = _MarkedValue(fMediaMenu, fMediaValues);
	if (!media.IsEmpty())
		strlcpy(options.media, media.String(), sizeof(options.media));

	BString sides = _MarkedValue(fSidesMenu, fSidesValues);
	if (!sides.IsEmpty())
		strlcpy(options.sides, sides.String(), sizeof(options.sides));

	BString color = _MarkedValue(fColorMenu, fColorValues);
	if (!color.IsEmpty())
		strlcpy(options.color_mode, color.String(),
			sizeof(options.color_mode));

	int32 qualityIndex = fQualityMenu->FindMarkedIndex();
	if (qualityIndex >= 0)
		options.quality = qualityIndex + 1;

	PrinterWorker::Post(kWorkerOpSetOptions, fReplyTo, fQueue, BString(),
		BString(), 0, false, &options);
	Quit();
}


void
PrinterOptionsDialog::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgOptionsSave:
			_Save();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
