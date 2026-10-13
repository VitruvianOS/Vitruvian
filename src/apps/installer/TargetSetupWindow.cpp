/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "TargetSetupWindow.h"

#include <stdio.h>
#include <stdlib.h>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceVisitor.h>
#include <DiskSystem.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Partition.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include "InstallerDefs.h"
#include "PartitionMenuItem.h"
#include "PartitionPlanBuilder.h"


#define B_TRANSLATION_CONTEXT "TargetSetup"


static const uint32 kMsgModeChanged = 'iTMC';
static const uint32 kMsgTableChanged = 'iBTC';
static const uint32 kMsgApply = 'iTAP';


// Collects format-candidate partitions for the existing-partition mode.
class ExistingPartitionVisitor : public BDiskDeviceVisitor {
public:
	ExistingPartitionVisitor(BMenu* menu, BString* diskPathOut)
		:
		fMenu(menu),
		fDiskPathOut(diskPathOut)
	{
	}

	virtual bool Visit(BDiskDevice* device)
	{
		if (device == NULL)
			return false;
		BPath path;
		if (device->GetPath(&path) != B_OK)
			return false;
		fCurrentDisk = path.Path();
		return false;
	}

	virtual bool Visit(BPartition* partition, int32 /*level*/)
	{
		if (partition == NULL || partition->ContentSize() <= 0)
			return false;
		if (partition->IsDevice() || partition->IsMounted())
			return false;
		BDiskSystem diskSystem;
		if (partition->GetDiskSystem(&diskSystem) != B_OK
			|| !diskSystem.IsFileSystem()) {
			return false;
		}
		const char* type = partition->Type();
		if (type != NULL) {
			if (strcmp(type, "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f") == 0
				|| strcmp(type, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") == 0
				|| strcasecmp(type, "EFI") == 0)
				return false;
		}
		if (partition->ContentType() == NULL)
			return false;
		BPath path;
		if (partition->GetPath(&path) != B_OK)
			return false;
		BString label;
		BString contentName = partition->ContentName();
		if (!contentName.IsEmpty())
			label.SetToFormat("%s (%s)", contentName.String(),
				path.Path());
		else
			label.SetToFormat(B_TRANSLATE("%s"), path.Path());
		BMessage* message = new BMessage(TARGET_PARTITION);
		message->AddString("devnode", path.Path());
		message->AddString("disk", fCurrentDisk);
		fMenu->AddItem(new BMenuItem(label.String(), message));
		if (fDiskPathOut->IsEmpty())
			*fDiskPathOut = fCurrentDisk;
		return false;
	}

private:
	BMenu*		fMenu;
	BString*	fDiskPathOut;
	BString		fCurrentDisk;
};


static const uint32 kMsgDiskChosen = 'iTDC';


TargetSetupWindow::TargetSetupWindow(const BMessenger& notify)
	:
	BWindow(BRect(0, 0, 480, 520), B_TRANSLATE("Set up target"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_RESIZABLE
			| B_NOT_ZOOMABLE),
	fNotify(notify)
{
	fDiskMenu = new BPopUpMenu("disks");
	fDiskField = new BMenuField("diskField", B_TRANSLATE("Disk:"),
		fDiskMenu);
	fDiskMenu->SetTargetForItems(this);

	fModeMenu = new BPopUpMenu("modes");
	fModeMenu->AddItem(new BMenuItem(
		B_TRANSLATE("Erase disk, create ESP and root"),
		new BMessage(kMsgModeChanged)));
	fModeMenu->AddItem(new BMenuItem(
		B_TRANSLATE("Format an existing partition"),
		new BMessage(kMsgModeChanged)));
	fModeMenu->ItemAt(0)->SetMarked(true);
	fModeField = new BMenuField("modeField", B_TRANSLATE("Action:"),
		fModeMenu);
	fModeMenu->SetTargetForItems(this);

	fFilesystemMenu = new BPopUpMenu("filesystems");
	fFilesystemMenu->AddItem(new BMenuItem("ext4", NULL));
	fFilesystemMenu->AddItem(new BMenuItem("xfs", NULL));
	fFilesystemMenu->AddItem(new BMenuItem("btrfs", NULL));
	fFilesystemMenu->ItemAt(0)->SetMarked(true);
	fFilesystemField = new BMenuField("fsField",
		B_TRANSLATE("Root filesystem:"), fFilesystemMenu);

	fTableMenu = new BPopUpMenu("tables");
	fTableMenu->AddItem(new BMenuItem("GPT", NULL));
	fTableMenu->AddItem(new BMenuItem("MBR", NULL));
	fTableMenu->ItemAt(0)->SetMarked(true);
	fTableField = new BMenuField("tableField",
		B_TRANSLATE("Partition table:"), fTableMenu);
	fTableMenu->SetTargetForItems(this);

	fExistingMenu = new BPopUpMenu("existing");
	fExistingField = new BMenuField("existingField",
		B_TRANSLATE("Partition:"), fExistingMenu);

	fEspSizeField = new BTextControl("espSize",
		B_TRANSLATE("ESP size (MiB):"), "512", NULL);
	fEspSizeField->SetToolTip(B_TRANSLATE("0 removes the ESP; needed "
		"only for UEFI boots"));

	fBiosBootCheck = new BCheckBox("biosBoot",
		B_TRANSLATE("Create BIOS boot partition (1 MiB)"), NULL);
	fBiosBootCheck->SetValue(B_CONTROL_ON);

	fPreview = new BTextView("preview");
	fPreview->MakeEditable(false);
	fPreview->SetExplicitMinSize(BSize(B_SIZE_UNSET, 120));

	fApplyButton = new BButton("apply", B_TRANSLATE("Use this plan"),
		new BMessage(kMsgApply));
	fCloseButton = new BButton("close", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fDiskField)
		.Add(fModeField)
		.Add(fExistingField)
		.Add(fTableField)
		.Add(fEspSizeField)
		.Add(fBiosBootCheck)
		.Add(fFilesystemField)
		.Add(fPreview)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.AddGlue()
			.Add(fCloseButton)
			.Add(fApplyButton)
		.End();

	_ScanDisks();
	_ScanExistingPartitions();
	_UpdateControls();

	CenterOnScreen();
}


void
TargetSetupWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgModeChanged:
		case kMsgTableChanged:
		case kMsgDiskChosen:
			_UpdateControls();
			break;

		case kMsgApply:
			_Apply();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


void
TargetSetupWindow::_ScanDisks()
{
	BMenuItem* item;
	while ((item = fDiskMenu->RemoveItem((int32)0)) != NULL)
		delete item;

	BDiskDeviceRoster roster;
	roster.RewindDevices();
	BDiskDevice device;
	while (roster.GetNextDevice(&device) == B_OK) {
		BPath path;
		if (device.GetPath(&path) != B_OK)
			continue;
		BMessage* message = new BMessage(kMsgDiskChosen);
		message->AddString("disk", path.Path());
		BString label;
		BString contentName = device.ContentName();
		if (!contentName.IsEmpty())
			label.SetToFormat("%s (%s)", contentName.String(),
				path.Path());
		else
			label.SetTo(path.Path());
		fDiskMenu->AddItem(new BMenuItem(label.String(), message));
	}
	if (fDiskMenu->CountItems() > 0)
		fDiskMenu->ItemAt(0)->SetMarked(true);
}


void
TargetSetupWindow::_ScanExistingPartitions()
{
	BMenuItem* item;
	while ((item = fExistingMenu->RemoveItem((int32)0)) != NULL)
		delete item;

	fExistingDiskPath.Truncate(0);
	BDiskDeviceRoster roster;
	roster.RewindDevices();
	BDiskDevice device;
	while (roster.GetNextDevice(&device) == B_OK) {
		ExistingPartitionVisitor visitor(fExistingMenu,
			&fExistingDiskPath);
		device.VisitEachDescendant(&visitor);
	}
	if (fExistingMenu->CountItems() > 0)
		fExistingMenu->ItemAt(0)->SetMarked(true);
}


void
TargetSetupWindow::_UpdateControls()
{
	BMenuItem* marked = fModeMenu->FindMarked();
	bool eraseMode = marked == NULL || fModeMenu->IndexOf(marked) == 0;
	fDiskField->SetEnabled(eraseMode);
	fTableField->SetEnabled(eraseMode);
	fEspSizeField->SetEnabled(eraseMode);
	fExistingField->SetEnabled(!eraseMode);

	BMenuItem* table = fTableMenu->FindMarked();
	bool gpt = table == NULL || fTableMenu->IndexOf(table) == 0;
	fBiosBootCheck->SetEnabled(eraseMode && gpt);
	if (!gpt)
		fBiosBootCheck->SetValue(B_CONTROL_OFF);
}


bool
TargetSetupWindow::_BuildPlan(BString& planOut, BString& errorOut)
{
	BMenuItem* mode = fModeMenu->FindMarked();
	bool eraseMode = mode == NULL || fModeMenu->IndexOf(mode) == 0;

	BMenuItem* fsItem = fFilesystemMenu->FindMarked();
	BString filesystem = fsItem != NULL ? fsItem->Label() : "ext4";

	BPrivate::PartitionPlanBuilder builder;

	if (!eraseMode) {
		BMenuItem* existing = fExistingMenu->FindMarked();
		const char* devnode = NULL;
		if (existing == NULL || existing->Message() == NULL
			|| existing->Message()->FindString("devnode", &devnode)
				!= B_OK) {
			errorOut = B_TRANSLATE("No existing partition chosen.");
			return false;
		}
		const char* disk = NULL;
		existing->Message()->FindString("disk", &disk);
		builder.SetDisk(disk != NULL ? disk : fExistingDiskPath.String());
		builder.AddFormat("f_root", devnode, filesystem.String(),
			"vitruvian-root", "root", NULL);
	} else {
		BMenuItem* diskItem = fDiskMenu->FindMarked();
		const char* disk = NULL;
		if (diskItem == NULL || diskItem->Message() == NULL
			|| diskItem->Message()->FindString("disk", &disk)
				!= B_OK) {
			errorOut = B_TRANSLATE("No disk chosen.");
			return false;
		}
		BMenuItem* tableItem = fTableMenu->FindMarked();
		const char* tableType
			= (tableItem != NULL && fTableMenu->IndexOf(tableItem) == 1)
				? "mbr" : "gpt";

		off_t espSize = strtoll(fEspSizeField->Text(), NULL, 10);
		if (espSize < 0 || espSize > 4096) {
			errorOut = B_TRANSLATE("ESP size must be between 0 and "
				"4096 MiB.");
			return false;
		}
		bool biosBoot = fBiosBootCheck->Value() == B_CONTROL_ON
			&& strcmp(tableType, "gpt") == 0;

		builder.SetDisk(disk);
		builder.AddCreateTable("t0", tableType);
		off_t offset = 1;
		if (biosBoot) {
			builder.AddCreate("p_bios", offset, 1, "bios_boot",
				"none");
			offset += 1;
		}
		if (espSize > 0) {
			builder.AddCreate("p_esp", offset, espSize, "esp",
				"esp");
			builder.AddFormat("f_esp", "p_esp", "fat32", "ESP",
				"esp", NULL);
			BMessage flags;
			flags.AddString("flag", "esp");
			builder.AddSetFlags("sf_esp", "p_esp", flags);
			offset += espSize;
		}
		builder.AddCreate("p_root", offset, -1, "linux", "root");
		builder.AddFormat("f_root", "p_root", filesystem.String(),
			"vitruvian-root", "root", NULL);
	}

	if (!builder.IsValid()) {
		errorOut = B_TRANSLATE("The plan contains an invalid field.");
		return false;
	}
	planOut = builder.ToPlan();
	return true;
}


void
TargetSetupWindow::_Apply()
{
	BString plan;
	BString error;
	if (!_BuildPlan(plan, error)) {
		BAlert* alert = new BAlert(B_TRANSLATE("Cannot build plan"),
			error.String(), B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	fPreview->SetText(plan.String());

	BMessage message(MSG_PLAN_READY);
	message.AddString("plan", plan);
	fNotify.SendMessage(&message);
	PostMessage(B_QUIT_REQUESTED);
}
