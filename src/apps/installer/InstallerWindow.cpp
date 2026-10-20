/*
 * Copyright 2009-2011, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2005, Jérôme DUVAL
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "InstallerWindow.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceVisitor.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <GridView.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <List.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Partition.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <SeparatorView.h>
#include <RosterPrivate.h>
#include <Screen.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <TextView.h>

#include <Keymap.h>
#include <RootExpansion.h>
#include <RootMedia.h>

#include "InstallerDefs.h"
#include "PartitionMenuItem.h"
#include "tracker_private.h"
#include "TargetSetupWindow.h"
#include "WorkerThread.h"


#define B_TRANSLATION_CONTEXT "InstallerWindow"


static const char* kDriveSetupSignature = "application/x-vnd.Haiku-DriveSetup";

static const uint32 kMsgBegin           = 'iBGN';
static const uint32 kMsgInit            = 'iINI';
static const uint32 kMsgQuit            = 'iQTA';
static const uint32 kMsgAdvancedToggled = 'iADV';
static const uint32 kMsgPasswordChanged = 'iPWC';
static const uint32 kMsgInPlaceSelected = 'iINP';
static const uint32 kMsgBootChoice      = 'iBCH';
static const uint32 kMsgSwapChoice      = 'iSWC';
static const uint32 kMsgSwapSize        = 'iSWS';

// Real partition IDs are >= 0; -2 tags the in-place pseudo-item.
static const int32 kInPlacePartitionId = -2;


static const char* kEspTypeGuid = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";
static const char* kBiosBootTypeGuid = "21686148-6449-6e6f-744e-656564454649";
static const char* kSwapTypeGuid = "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f";


// What the bootloader and swap menus need to know about one disk.
struct BootDiskInfo {
	BootDiskInfo()
		:
		known(false),
		gpt(false),
		mbr(false),
		removable(false),
		biosBoot(false),
		planEsp(false),
		planBios(false),
		planSwap(false),
		planSwapMiB(0)
	{
	}

	bool	known;
	BString	path;
	bool	gpt;
	bool	mbr;
	bool	removable;
	bool	biosBoot;
	BMessage esps;		// "node"
	BMessage swaps;		// "node", int64 "mib"
	// Partitions the pending plan creates on this disk.
	bool	planEsp;
	bool	planBios;
	bool	planSwap;
	int64	planSwapMiB;
};


static bool
type_is(const char* type, const char* guid, const char* alias)
{
	return type != NULL && (strcasecmp(type, guid) == 0
		|| (alias != NULL && strcasecmp(type, alias) == 0));
}


// Sorts a disk's partitions into ESPs, BIOS boot and swap.
class BootDiskVisitor : public BDiskDeviceVisitor {
public:
	BootDiskVisitor(BootDiskInfo& info)
		:
		fInfo(info)
	{
	}

	virtual bool Visit(BDiskDevice* /*device*/)
	{
		return false;
	}

	virtual bool Visit(BPartition* partition, int32 /*level*/)
	{
		if (partition == NULL)
			return false;
		const char* type = partition->Type();
		BPath path;
		if (partition->GetPath(&path) != B_OK)
			return false;

		if (type_is(type, kEspTypeGuid, "EFI") || type_is(type, "0xef", NULL)) {
			if (partition->ContentSize() > 0)
				fInfo.esps.AddString("node", path.Path());
		} else if (type_is(type, kBiosBootTypeGuid, NULL)) {
			fInfo.biosBoot = true;
		} else {
			const char* content = partition->ContentType();
			if (type_is(type, kSwapTypeGuid, "0x82")
				|| (content != NULL && strcasestr(content, "swap") != NULL)) {
				fInfo.swaps.AddString("node", path.Path());
				fInfo.swaps.AddInt64("mib",
					partition->Size() / (1024 * 1024));
			}
		}
		return false;
	}

private:
	BootDiskInfo&	fInfo;
};


static void
collect_disk_info(BDiskDevice& device, BootDiskInfo& info)
{
	BPath path;
	if (device.GetPath(&path) == B_OK)
		info.path = path.Path();
	const char* table = device.ContentType();
	if (table != NULL) {
		if (strcasecmp(table, "gpt") == 0)
			info.gpt = true;
		else if (strcasecmp(table, "intel") == 0
			|| strcasecmp(table, "dos") == 0
			|| strcasecmp(table, "msdos") == 0
			|| strcasecmp(table, "mbr") == 0) {
			info.mbr = true;
		}
	}
	info.removable = device.IsRemovableMedia();
	info.known = true;
	BootDiskVisitor visitor(info);
	device.VisitEachDescendant(&visitor);
}


// Folds the pending plan into what the disk will look like after it runs.
static void
apply_plan_to_disk(const BString& plan, BootDiskInfo& info)
{
	BString planText(plan);
	planText << "\n";
	bool wipes = false;
	BString tableType;
	int64 lastSize = 0;
	int32 offset = 0;
	while (offset < planText.Length()) {
		int32 end = planText.FindFirst('\n', offset);
		if (end < 0)
			end = planText.Length();
		BString line;
		planText.CopyInto(line, offset, end - offset);
		offset = end + 1;
		line.Trim();

		if (line == "[op]") {
			lastSize = 0;
		} else if (line == "kind=create_table") {
			wipes = true;
		} else if (line.StartsWith("table_type=")) {
			tableType = line.String() + 11;
		} else if (line.StartsWith("size_mib=")) {
			lastSize = strtoll(line.String() + 9, NULL, 10);
		} else if (line == "gpt_type=esp") {
			info.planEsp = true;
		} else if (line == "gpt_type=bios_boot") {
			info.planBios = true;
		} else if (line == "gpt_type=linux_swap") {
			info.planSwap = true;
			info.planSwapMiB = lastSize;
		}
	}

	if (wipes) {
		info.biosBoot = false;
		info.esps.MakeEmpty();
		info.swaps.MakeEmpty();
		info.gpt = tableType != "mbr";
		info.mbr = tableType == "mbr";
	}
}


static bool
find_disk(const char* diskPath, BootDiskInfo& info)
{
	BDiskDeviceRoster roster;
	roster.RewindDevices();
	BDiskDevice device;
	while (roster.GetNextDevice(&device) == B_OK) {
		BPath path;
		if (device.GetPath(&path) == B_OK
			&& strcmp(path.Path(), diskPath) == 0) {
			collect_disk_info(device, info);
			return true;
		}
	}
	return false;
}


static BString
mib_label(int64 mib)
{
	BString text;
	if (mib >= 1024)
		text.SetToFormat("%.1f GiB", mib / 1024.0);
	else
		text.SetToFormat("%" B_PRId64 " MiB", mib);
	return text;
}


// Live ISO's / is overlay/squashfs; only a writable disk root qualifies.
static bool
_in_place_available()
{
	return access("/etc/vos/live", F_OK) == 0 && root_is_persistent_rw();
}


static void
random_hostname_suffix(char* out, size_t outSize)
{
	static const char kAlpha[] = "abcdefghijklmnopqrstuvwxyz0123456789";
	unsigned char rnd[4];
	if (getrandom(rnd, sizeof(rnd), 0) == (ssize_t)sizeof(rnd)) {
		snprintf(out, outSize, "vitruvian-%c%c%c%c",
			kAlpha[rnd[0] % 36], kAlpha[rnd[1] % 36],
			kAlpha[rnd[2] % 36], kAlpha[rnd[3] % 36]);
	} else {
		snprintf(out, outSize, "vitruvian");
	}
}


InstallerWindow::InstallerWindow(bool setupMode)
	:
	BWindow(BRect(0, 0, 520, 480), B_TRANSLATE_SYSTEM_NAME("Installer"),
		B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fSetupMode(setupMode),
	fInstallStatus(kReadyForInstall),
	fWorkerThread(new WorkerThread(BMessenger(this))),
	fCopyEngineCancelSemaphore(-1),
	fBootloaderExplicit(false),
	fSwapExplicit(false),
	fPlanSwapHintMiB(0),
	fPlanSwapMiB(0)
{
	fDestMenu = new BPopUpMenu(B_TRANSLATE("scanning" B_UTF8_ELLIPSIS),
		true, false);
	fDestMenuField = new BMenuField("destMenuField",
		B_TRANSLATE("Install to:"), fDestMenu);

	char hostDefault[24];
	random_hostname_suffix(hostDefault, sizeof(hostDefault));

	fHostnameField = new BTextControl("hostname",
		B_TRANSLATE("Hostname:"), hostDefault, NULL);

	fFullNameField = new BTextControl("fullname",
		B_TRANSLATE("Full name:"), "", NULL);
	fFullNameField->TextView()->SetText("");
	fFullNameField->SetToolTip(B_TRANSLATE("Optional"));

	fUserField = new BTextControl("user",
		B_TRANSLATE("User:"), "", NULL);

	fPasswordField = new BTextControl("password",
		B_TRANSLATE("Password:"), "",
		new BMessage(kMsgPasswordChanged));
	fPasswordField->TextView()->HideTyping(true);
	fPasswordField->SetModificationMessage(
		new BMessage(kMsgPasswordChanged));

	fConfirmField = new BTextControl("confirm",
		B_TRANSLATE("Confirm:"), "",
		new BMessage(kMsgPasswordChanged));
	fConfirmField->TextView()->HideTyping(true);
	fConfirmField->SetModificationMessage(
		new BMessage(kMsgPasswordChanged));

	fSudoCheck = new BCheckBox("sudo",
		B_TRANSLATE("Enable sudo"), NULL);
	fSudoCheck->SetValue(B_CONTROL_ON);

	fAutologinCheck = new BCheckBox("autologin",
		B_TRANSLATE("Log in automatically"), NULL);
	fAutologinCheck->SetValue(B_CONTROL_OFF);

	fAutologinNote = new BStringView("autologinNote",
		B_TRANSLATE("Anyone with physical access will boot into "
			"this session."));
	BFont smallFont(be_plain_font);
	smallFont.SetSize(smallFont.Size() * 0.9f);
	fAutologinNote->SetFont(&smallFont, B_FONT_SIZE);

	fAdvancedCheck = new BCheckBox("advanced",
		B_TRANSLATE("Set a root password"),
		new BMessage(kMsgAdvancedToggled));

	fRootPasswordField = new BTextControl("rootPassword",
		B_TRANSLATE("Root password:"), "",
		new BMessage(kMsgPasswordChanged));
	fRootPasswordField->TextView()->HideTyping(true);
	fRootPasswordField->SetModificationMessage(
		new BMessage(kMsgPasswordChanged));

	fRootConfirmField = new BTextControl("rootConfirm",
		B_TRANSLATE("Confirm root:"), "",
		new BMessage(kMsgPasswordChanged));
	fRootConfirmField->TextView()->HideTyping(true);
	fRootConfirmField->SetModificationMessage(
		new BMessage(kMsgPasswordChanged));

	fPasswordStatus = new BStringView("passwordStatus", "");

	fProgressBar = new BStatusBar("progress", B_TRANSLATE("Progress:"));
	fProgressBar->SetMaxValue(100.0f);
	fProgressBar->SetBarHeight(20);

	fInstallButton = new BButton("install", B_TRANSLATE("Install"),
		new BMessage(kMsgBegin));
	fInstallButton->MakeDefault(true);
	fInstallButton->SetEnabled(false);

	fQuitButton = new BButton("quit", B_TRANSLATE("Quit"),
		new BMessage(kMsgQuit));

	fLaunchDriveSetupButton = new BButton("setup_button",
		B_TRANSLATE("Set up partitions" B_UTF8_ELLIPSIS),
		new BMessage(MSG_LAUNCH_DRIVE_SETUP));

	fTargetSetupButton = new BButton("target_setup",
		B_TRANSLATE("Guided partitioning" B_UTF8_ELLIPSIS),
		new BMessage(MSG_TARGET_SETUP));

	fBootloaderChoiceMenu = new BPopUpMenu("bootloaderChoice");
	fBootloaderField = new BMenuField("bootloaderField",
		B_TRANSLATE("Bootloader:"), fBootloaderChoiceMenu);
	fBootloaderChoiceMenu->SetTargetForItems(this);

	fSwapChoiceMenu = new BPopUpMenu("swapChoice");
	fSwapField = new BMenuField("swapField", B_TRANSLATE("Swap:"),
		fSwapChoiceMenu);

	BString ramText;
	ramText << installer_ram_mib();
	fSwapFileSizeText = ramText;
	fSwapSizeField = new BTextControl("swapSize",
		B_TRANSLATE("Swap size (MiB):"), ramText.String(), NULL);
	fSwapSizeField->SetModificationMessage(new BMessage(kMsgSwapSize));
	fSwapNote = new BStringView("swapNote", "");
	BFont noteFont(be_plain_font);
	noteFont.SetSize(noteFont.Size() * 0.9f);
	fSwapNote->SetFont(&noteFont, B_FONT_SIZE);

	fNvramCheck = new BCheckBox("nvram",
		B_TRANSLATE("Register a firmware boot entry"), NULL);
	fNvramCheck->SetEnabled(false);

	fKeepSshCheck = new BCheckBox("keepSsh",
		B_TRANSLATE("Keep SSH enabled"), NULL);

	BGridView* accountView = new BGridView(B_USE_SMALL_SPACING,
		B_USE_SMALL_SPACING);
	accountView->SetName(B_TRANSLATE("Account"));
	BLayoutBuilder::Grid<>(accountView)
		.SetInsets(B_USE_ITEM_SPACING)
		.AddTextControl(fHostnameField, 0, 0)
		.AddTextControl(fFullNameField, 0, 1)
		.AddTextControl(fUserField, 0, 2)
		.AddTextControl(fPasswordField, 0, 3)
		.AddTextControl(fConfirmField, 0, 4)
		.Add(fSudoCheck, 0, 5, 2)
		.Add(fAutologinCheck, 0, 6, 2)
		.Add(fAutologinNote, 0, 7, 2);

	BGridView* advancedView = new BGridView(B_USE_SMALL_SPACING,
		B_USE_SMALL_SPACING);
	advancedView->SetName(B_TRANSLATE("Advanced"));
	BGridView* targetAdvanced = new BGridView(B_USE_SMALL_SPACING,
		B_USE_SMALL_SPACING);
	fTargetAdvancedView = targetAdvanced;
	BLayoutBuilder::Grid<>(targetAdvanced)
		.AddMenuField(fBootloaderField, 0, 0)
		.AddMenuField(fSwapField, 0, 1)
		.AddTextControl(fSwapSizeField, 0, 2)
		.Add(fSwapNote, 0, 3, 2)
		.Add(fNvramCheck, 0, 4, 2)
		.Add(new BSeparatorView(B_HORIZONTAL), 0, 5, 2);
	BLayoutBuilder::Grid<>(advancedView)
		.SetInsets(B_USE_ITEM_SPACING)
		.Add(targetAdvanced, 0, 0, 2)
		.Add(fAdvancedCheck, 0, 1, 2)
		.AddTextControl(fRootPasswordField, 0, 2)
		.AddTextControl(fRootConfirmField, 0, 3)
		.Add(fKeepSshCheck, 0, 4, 2)
		.AddGlue(0, 5, 2);

	BTabView* setupTabs = new BTabView("setup", B_WIDTH_FROM_LABEL);
	setupTabs->SetBorder(B_NO_BORDER);
	setupTabs->AddTab(accountView);
	setupTabs->AddTab(advancedView);
	// Empty notes would still take a row of height.
	fPasswordStatus->Hide();

	// Fields fill the box but never shrink below a usable width.
	float fieldWidth = be_plain_font->StringWidth("M") * 24;
	BTextControl* fields[] = { fHostnameField, fFullNameField, fUserField,
		fPasswordField, fConfirmField, fRootPasswordField, fRootConfirmField };
	for (BTextControl* field : fields) {
		BLayoutItem* item = field->CreateTextViewLayoutItem();
		item->SetExplicitMinSize(BSize(fieldWidth, B_SIZE_UNSET));
	}

	fMenuBar = new BMenuBar("main menu");
	fToolsMenu = new BMenu(B_TRANSLATE("Tools"));
	fBootloaderMenu = new BMenu(B_TRANSLATE("Install bootloader"));
	fBootloaderSetupItem = new BMenuItem(
		B_TRANSLATE("Configure boot menu" B_UTF8_ELLIPSIS),
		new BMessage(MSG_BOOTLOADER_SETUP));
	fBootloaderSetupItem->SetEnabled(false);
	fToolsMenu->AddItem(fBootloaderSetupItem);
	fToolsMenu->AddItem(fBootloaderMenu);
	fMenuBar->AddItem(fToolsMenu);

	// Menu fields keep their preferred width in a grid; let them fill it.
	BMenuField* menuFields[] = { fDestMenuField, fBootloaderField, fSwapField };
	for (BMenuField* field : menuFields) {
		field->CreateMenuBarLayoutItem()->SetExplicitMaxSize(
			BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	}

	BGridView* targetGrid = new BGridView(B_USE_SMALL_SPACING,
		B_USE_SMALL_SPACING);
	BLayoutBuilder::Grid<>(targetGrid)
		.AddMenuField(fDestMenuField, 0, 0)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 1, 1)
			.Add(fLaunchDriveSetupButton)
			.Add(fTargetSetupButton)
			.AddGlue()
		.End();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.AddGroup(B_VERTICAL, B_USE_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(targetGrid)
			.Add(setupTabs)
			.Add(fPasswordStatus)
			.Add(fProgressBar)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(fQuitButton)
				.Add(fInstallButton)
			.End()
		.End();

	if (fSetupMode) {
		// Account box only; the target is the running root.
		fMenuBar->Hide();
		targetGrid->Hide();
		fTargetAdvancedView->Hide();
		fInstallButton->SetLabel(B_TRANSLATE("Set up"));
		SetTitle(B_TRANSLATE("Set up this install"));
		fWorkerThread->SetInPlace(true);
	}

	_UpdateAdvancedEnabled();
	_UpdatePasswordStatus();

	CenterOnScreen();
	Show();

	// Subscribe to disk device add/remove so a USB stick plugged in while
	// the installer is open triggers a rescan of the target menu.
	BDiskDeviceRoster().StartWatching(BMessenger(this));
	// DriveSetup quits after the user repartitions; rescan on its exit so
	// the fresh layout is offered without a manual refresh.
	be_roster->StartWatching(BMessenger(this));

	PostMessage(kMsgInit);
}


InstallerWindow::~InstallerWindow()
{
	BDiskDeviceRoster().StopWatching(BMessenger(this));
	be_roster->StopWatching(BMessenger(this));
	_SetCopyEngineCancelSemaphore(-1);
}


void
InstallerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgInit:
			_ScanPartitions();
			_UpdateControls();
			break;

		case TARGET_PARTITION:
		{
			PartitionMenuItem* item = (PartitionMenuItem*)
				fDestMenu->FindMarked();
			if (item != NULL) {
				fDestMenuField->MenuItem()->SetLabel(item->Label());
				fWorkerThread->SetInPlace(
					item->ID() == (partition_id)kInPlacePartitionId);
				_RebuildBootloaderMenu();
				// A plan describes one specific target; picking another
				// one drops it rather than silently reusing it.
				if (!fPlanText.IsEmpty()
					&& item->ID() != (partition_id)kPlannedPartitionId) {
					_ClearPlan();
				}
			}
			_UpdateControls();
			break;
		}

		case MSG_LAUNCH_DRIVE_SETUP:
			_LaunchDriveSetup();
			break;

		case MSG_TARGET_SETUP:
		{
			if (fInstallStatus != kReadyForInstall)
				break;
			TargetSetupWindow* window = new TargetSetupWindow(
				BMessenger(this));
			window->Show();
			break;
		}

		case MSG_PLAN_READY:
		{
			const char* plan = NULL;
			if (message->FindString("plan", &plan) == B_OK) {
				fPlanText = plan;
				fPlanSwapHint.Truncate(0);
				message->FindString("swap", &fPlanSwapHint);
				fPlanSwapHintMiB = 0;
				message->FindInt64("swap_size", &fPlanSwapHintMiB);
				fSwapExplicit = false;
				// _ScanPartitions adds and marks the planned item.
				_ScanPartitions();
				PartitionMenuItem* planned
					= (PartitionMenuItem*)fDestMenu->FindMarked();
				if (planned != NULL)
					fDestMenuField->MenuItem()->SetLabel(
						planned->Label());
				fWorkerThread->SetInPlace(false);
				_UpdateControls();
			}
			break;
		}

		case kMsgBootChoice:
		{
			const char* choice = NULL;
			if (message->FindString("choice", &choice) == B_OK) {
				fBootloaderChoice = choice;
				fBootloaderExplicit = true;
			}
			_UpdateControls();
			break;
		}

		case kMsgSwapChoice:
		{
			const char* choice = NULL;
			if (message->FindString("choice", &choice) == B_OK) {
				fSwapChoice = choice;
				fSwapExplicit = true;
			}
			_UpdateSwapControls();
			_UpdateControls();
			break;
		}

		case kMsgSwapSize:
			_UpdateSwapControls();
			_UpdateControls();
			break;

		case B_SOME_APP_LAUNCHED:
		{
			entry_ref app;
			if (message->FindRef("ref", &app) == B_OK
				&& app.name != NULL
				&& strcmp(app.name, "DriveSetup") == 0) {
				fLaunchDriveSetupButton->SetEnabled(false);
			}
			break;
		}

		case B_SOME_APP_QUIT:
		{
			entry_ref app;
			if (message->FindRef("ref", &app) == B_OK
				&& app.name != NULL
				&& strcmp(app.name, "DriveSetup") == 0) {
				fLaunchDriveSetupButton->SetEnabled(true);
				// DriveSetup may have repartitioned; refresh targets.
				_ScanPartitions();
			}
			break;
		}

		case MSG_INSTALL_BOOTLOADER:
		{
			int64 id = -1;
			if (message->FindInt64("id", &id) == B_OK)
				fWorkerThread->InstallBootloader((partition_id)id);
			break;
		}

		case MSG_BOOTLOADER_SETUP:
			fWorkerThread->BootloaderSetup();
			break;

		case kMsgAdvancedToggled:
			_UpdateAdvancedEnabled();
			_UpdatePasswordStatus();
			break;

		case kMsgPasswordChanged:
			_UpdatePasswordStatus();
			break;

		case kMsgBegin:
		{
			BString err;
			if (!_ValidateSetup(err)) {
				BAlert* alert = new BAlert(B_TRANSLATE("Cannot install"),
					err.String(), B_TRANSLATE("OK"), NULL, NULL,
					B_WIDTH_AS_USUAL, B_STOP_ALERT);
				alert->Go();
				break;
			}
			PartitionMenuItem* item = (PartitionMenuItem*)
				fDestMenu->FindMarked();
			if (item == NULL) {
				fInstallButton->SetEnabled(false);
				break;
			}
			bool inPlace = (item->ID() == (partition_id)kInPlacePartitionId);

			InstallSpec spec;
			spec.planText = fPlanText;
			_ReadBootloaderChoice();
			_ReadSwapChoice();
			if (!inPlace) {
				if (fBootloaderChoice.StartsWith("esp:")) {
					spec.efiMode = "vitruvian";
					spec.biosMode = "none";
					spec.espPath = fBootloaderChoice.String() + 4;
				} else if (fBootloaderChoice.StartsWith("both:")) {
					spec.efiMode = "vitruvian";
					spec.biosMode = "disk";
					spec.espPath = fBootloaderChoice.String() + 5;
				} else if (fBootloaderChoice == "bios:disk") {
					spec.efiMode = "none";
					spec.biosMode = "disk";
					spec.espPath = "none";
				} else {
					spec.efiMode = "none";
					spec.biosMode = "none";
					spec.espPath = "none";
				}

				spec.nvram = fNvramCheck->IsEnabled()
					&& fNvramCheck->Value() == B_CONTROL_ON;

				if (fSwapChoice.StartsWith("part:")) {
					spec.swapMode = "partition";
					spec.swapDevice = fSwapChoice.String() + 5;
				} else if (fSwapChoice == "plan") {
					spec.swapMode = "partition";
				} else if (fSwapChoice == "file") {
					spec.swapMode = "file";
					spec.swapSizeMiB = _SwapSizeMiB();
				} else {
					spec.swapMode = "none";
				}
			}

			spec.keepSsh = fKeepSshCheck->Value() == B_CONTROL_ON;

			BString targetLabel = item->Label();
			BString prompt = _ComposeSummary(targetLabel.String(), inPlace);
			BAlert* alert = new BAlert(B_TRANSLATE("Confirm install"),
				prompt.String(),
				B_TRANSLATE("Cancel"),
				inPlace ? B_TRANSLATE("Set up") : B_TRANSLATE("Install"),
				NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() != 1)
				break;

			BString conf = _ComposeSetupConf();
			fPasswordField->SetText("");
			fConfirmField->SetText("");
			fRootPasswordField->SetText("");
			fRootConfirmField->SetText("");

			fInstallStatus = kInstalling;
			fInstallButton->SetEnabled(false);
			fDestMenuField->SetEnabled(false);
			fSwapField->SetEnabled(false);
			fSwapSizeField->SetEnabled(false);
			fBootloaderField->SetEnabled(false);
			fNvramCheck->SetEnabled(false);
			fKeepSshCheck->SetEnabled(false);
			fHostnameField->SetEnabled(false);
			fFullNameField->SetEnabled(false);
			fUserField->SetEnabled(false);
			fPasswordField->SetEnabled(false);
			fConfirmField->SetEnabled(false);
			fSudoCheck->SetEnabled(false);
			fAutologinCheck->SetEnabled(false);
			fAdvancedCheck->SetEnabled(false);
			fRootPasswordField->SetEnabled(false);
			fRootConfirmField->SetEnabled(false);
			fBootloaderSetupItem->SetEnabled(false);
			fLaunchDriveSetupButton->SetEnabled(false);
			fTargetSetupButton->SetEnabled(false);

			sem_id cancelSem = create_sem(0, "installer_cancel");
			_SetCopyEngineCancelSemaphore(cancelSem);
			fWorkerThread->SetLock(cancelSem);
			fWorkerThread->SetSetupConf(conf);
			explicit_bzero(conf.LockBuffer(0), conf.Length());
			conf.UnlockBuffer(0);
			fWorkerThread->StartInstall(item->ID(), spec);
			break;
		}

		case MSG_STATUS_MESSAGE:
		{
			const char* text = NULL;
			if (message->FindString("status", &text) == B_OK)
				fProgressBar->SetText(text);
			float progress = 0.0f;
			if (message->FindFloat("progress", &progress) == B_OK)
				fProgressBar->SetTo(progress);
			break;
		}

		case MSG_INSTALL_FINISHED:
		{
			_SetCopyEngineCancelSemaphore(-1);
			fInstallStatus = kFinished;
			fProgressBar->SetTo(100.0f);

			PartitionMenuItem* target =
				(PartitionMenuItem*)fDestMenu->FindMarked();
			bool inPlace = target != NULL
				&& target->ID() == (partition_id)kInPlacePartitionId;
			BString text = B_TRANSLATE("Installation completed successfully.");
			text << "\n\n";
			if (inPlace) {
				text << B_TRANSLATE("You can now restart into the installed "
					"system.");
			} else if (fBootloaderChoice == "none") {
				text << B_TRANSLATE("No boot loader was installed, as chosen. "
					"Set one up before restarting.");
			} else {
				text << B_TRANSLATE("The boot loader was installed. "
					"You can now restart into the installed system.");
			}
			if (fSetupMode) {
				_FinishSetup();
				break;
			}
			BAlert* alert = new BAlert(B_TRANSLATE("Installation complete"),
				text.String(), B_TRANSLATE("Quit"), NULL, NULL,
				B_WIDTH_AS_USUAL, B_INFO_ALERT);
			alert->Go();
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case MSG_RESET:
		{
			_SetCopyEngineCancelSemaphore(-1);
			status_t err = B_OK;
			message->FindInt32("error", &err);
			fInstallStatus = kReadyForInstall;
			fInstallButton->SetEnabled(true);
			fDestMenuField->SetEnabled(true);
			fHostnameField->SetEnabled(true);
			fFullNameField->SetEnabled(true);
			fUserField->SetEnabled(true);
			fPasswordField->SetEnabled(true);
			fConfirmField->SetEnabled(true);
			fSudoCheck->SetEnabled(true);
			fAutologinCheck->SetEnabled(true);
			fAdvancedCheck->SetEnabled(true);
			_UpdateAdvancedEnabled();
			fBootloaderSetupItem->SetEnabled(fDestMenu->FindMarked() != NULL);
			fLaunchDriveSetupButton->SetEnabled(true);
			fTargetSetupButton->SetEnabled(true);
			_UpdateSwapControls();

			BString detail;
			message->FindString("detail", &detail);

			BString msg;
			msg.SetToFormat("%s: %s",
				B_TRANSLATE("Installation failed"), strerror(err));
			if (!detail.IsEmpty())
				msg << "\n\n" << detail;
			BAlert* alert = new BAlert(B_TRANSLATE("Installation failed"),
				msg.String(), B_TRANSLATE("OK"), NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go();
			break;
		}

		case kMsgQuit:
			PostMessage(B_QUIT_REQUESTED);
			break;

		case B_DEVICE_UPDATE:
			if (fInstallStatus == kReadyForInstall)
				_ScanPartitions();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


bool
InstallerWindow::QuitRequested()
{
	if (fInstallStatus == kInstalling) {
		BAlert* alert = new BAlert(B_TRANSLATE("Cancel install"),
			B_TRANSLATE("Installation is in progress. Cancel it?"),
			B_TRANSLATE("Continue install"), B_TRANSLATE("Cancel install"),
			NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() == 0)
			return false;
		_QuitCopyEngine(false);
	}
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
InstallerWindow::_ScanPartitions()
{
	BMenuItem* item;
	while ((item = fDestMenu->RemoveItem((int32)0)) != NULL)
		delete item;

	if (fSetupMode) {
		if (_in_place_available()) {
			PartitionMenuItem* inPlace = new PartitionMenuItem("current",
				B_TRANSLATE("Current system (in-place)"),
				B_TRANSLATE("Current system (in-place)"),
				new BMessage(TARGET_PARTITION), kInPlacePartitionId);
			inPlace->SetIsValidTarget(true);
			fDestMenu->AddItem(inPlace, 0);
			inPlace->SetMarked(true);
		}
		_UpdateControls();
		return;
	}
	while ((item = fBootloaderMenu->RemoveItem((int32)0)) != NULL)
		delete item;

	fWorkerThread->ScanDisksPartitions(fDestMenu, fBootloaderMenu);

	if (!fPlanText.IsEmpty()) {
		PartitionMenuItem* planned = new PartitionMenuItem(
			"planned",
			B_TRANSLATE("New Vitruvian partition (created at install "
				"time)"),
			B_TRANSLATE("New Vitruvian partition"),
			new BMessage(TARGET_PARTITION), kPlannedPartitionId);
		planned->SetIsValidTarget(true);
		fDestMenu->AddItem(planned, 0);
		planned->SetMarked(true);
	}

	if (fBootloaderMenu->CountItems() == 0) {
		BMenuItem* noEsp = new BMenuItem(
			B_TRANSLATE("No EFI System Partitions found"), NULL);
		noEsp->SetEnabled(false);
		fBootloaderMenu->AddItem(noEsp);
	}

	if (_in_place_available()) {
		PartitionMenuItem* inPlace = new PartitionMenuItem("current",
			B_TRANSLATE("Current system (in-place)"),
			B_TRANSLATE("Current system (in-place)"),
			new BMessage(TARGET_PARTITION), kInPlacePartitionId);
		inPlace->SetIsValidTarget(true);
		fDestMenu->AddItem(inPlace, 0);
	}

	_RebuildBootloaderMenu();

	if (fDestMenu->CountItems() == 0) {
		fDestMenuField->MenuItem()->SetLabel(
			B_TRANSLATE("No writable partitions found"));
		fInstallButton->SetEnabled(false);
	} else {
		PartitionMenuItem* marked
			= (PartitionMenuItem*)fDestMenu->FindMarked();
		if (marked != NULL)
			fDestMenuField->MenuItem()->SetLabel(marked->Label());
		else
			fDestMenuField->MenuItem()->SetLabel(
				B_TRANSLATE("Choose target volume"));
	}
}


// In-place setup done: offer the free tail, then restart or log out.
void
InstallerWindow::_FinishSetup()
{
	be_app->PostMessage(MSG_SETUP_DONE);

	off_t tail = root_expansion_bytes();
	if (tail >= (off_t)1024 * 1024 * 1024) {
		BString text;
		text.SetToFormat(B_TRANSLATE("There is %.1f GiB of unused space "
			"after the system partition. Expand the system partition to "
			"use it?"), tail / (1024.0 * 1024.0 * 1024.0));
		BAlert* alert = new BAlert(B_TRANSLATE("Expand system partition"),
			text.String(), B_TRANSLATE("Not now"),
			B_TRANSLATE("Expand" B_UTF8_ELLIPSIS), NULL, B_WIDTH_AS_USUAL,
			B_INFO_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() == 1)
			be_roster->Launch("application/x-vnd.vos-ExpandVolume");
	}

	BString text;
	text.SetToFormat(B_TRANSLATE("Setup complete. Log out or restart to "
		"log in as %s."), fUserField->Text());
	// Before the desktop there is no session to leave, only a restart.
	bool desktop = be_roster->IsRunning(kDeskbarSignature);
	BAlert* alert = desktop
		? new BAlert(B_TRANSLATE("Setup complete"), text.String(),
			B_TRANSLATE("Later"), B_TRANSLATE("Log out"),
			B_TRANSLATE("Restart"), B_WIDTH_AS_USUAL, B_INFO_ALERT)
		: new BAlert(B_TRANSLATE("Setup complete"), text.String(),
			B_TRANSLATE("Restart"), NULL, NULL, B_WIDTH_AS_USUAL,
			B_INFO_ALERT);
	int32 choice = alert->Go();
	BRoster::Private roster((BRoster*)be_roster);
	if (desktop) {
		if (choice == 1)
			roster.LogOut(false);
		else if (choice == 2)
			roster.ShutDown(true, false, false);
	} else {
		roster.ShutDown(true, false, false);
	}
	be_app->PostMessage(B_QUIT_REQUESTED);
}


void
InstallerWindow::_UpdateControls()
{
	BString err;
	fInstallButton->SetEnabled(_ValidateSetup(err));
	fBootloaderSetupItem->SetEnabled(fDestMenu->FindMarked() != NULL);
}


void
InstallerWindow::_UpdateAdvancedEnabled()
{
	bool advanced = (fAdvancedCheck->Value() == B_CONTROL_ON);
	fRootPasswordField->SetEnabled(advanced);
	fRootConfirmField->SetEnabled(advanced);
}


void
InstallerWindow::_UpdatePasswordStatus()
{
	const char* pwd  = fPasswordField->Text();
	const char* pwd2 = fConfirmField->Text();

	BString status;
	bool ok = true;

	if (pwd == NULL || *pwd == '\0') {
		ok = false;
	} else if (pwd2 == NULL || strcmp(pwd, pwd2) != 0) {
		status = B_TRANSLATE("Passwords do not match.");
		ok = false;
	} else {
		const char* user = fUserField->Text();
		if (user != NULL && *user != '\0' && strcmp(user, pwd) == 0) {
			status = B_TRANSLATE("Password must not equal the username.");
			ok = false;
		} else if (strlen(pwd) < 8) {
			status = B_TRANSLATE("Password must be at least 8 characters.");
			ok = false;
		} else {
			status = B_TRANSLATE("Password OK.");
		}
	}

	if (ok && fAdvancedCheck->Value() == B_CONTROL_ON) {
		const char* rp  = fRootPasswordField->Text();
		const char* rp2 = fRootConfirmField->Text();
		if (rp == NULL || *rp == '\0' || rp2 == NULL
				|| strcmp(rp, rp2) != 0) {
			status = B_TRANSLATE("Root password: empty or does not match.");
			ok = false;
		}
	}

	fPasswordStatus->SetText(status.String());
	// Hide() nests; only toggle on a real change.
	if (status.IsEmpty() != fPasswordStatus->IsHidden()) {
		if (status.IsEmpty())
			fPasswordStatus->Hide();
		else
			fPasswordStatus->Show();
	}
	_UpdateControls();
}


bool
InstallerWindow::_ValidateSetup(BString& errorOut)
{
	if (fDestMenu->FindMarked() == NULL) {
		errorOut = B_TRANSLATE("Choose a target volume.");
		return false;
	}
	const char* user = fUserField->Text();
	if (user == NULL || *user == '\0') {
		errorOut = B_TRANSLATE("Enter a username.");
		return false;
	}
	if (!islower((unsigned char)user[0]) && user[0] != '_') {
		errorOut = B_TRANSLATE("Username must start with a letter or _.");
		return false;
	}
	for (const char* p = user; *p != '\0'; p++) {
		if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p)
				&& *p != '_' && *p != '-') {
			errorOut = B_TRANSLATE("Username may only contain "
				"a-z, 0-9, _ and -.");
			return false;
		}
	}
	if (strlen(user) > 32) {
		errorOut = B_TRANSLATE("Username too long (max 32 characters).");
		return false;
	}
	if (strcasecmp(user, "root") == 0 || strcasecmp(user, "vos-live") == 0
			|| strcasecmp(user, "vos_login") == 0
			|| strcasecmp(user, "nobody") == 0) {
		errorOut = B_TRANSLATE("Reserved username.");
		return false;
	}
	const char* pwd = fPasswordField->Text();
	if (pwd == NULL || strlen(pwd) < 8) {
		errorOut = B_TRANSLATE("Password too short (min 8 characters).");
		return false;
	}
	if (strcmp(pwd, fConfirmField->Text()) != 0) {
		errorOut = B_TRANSLATE("Password confirmation does not match.");
		return false;
	}
	if (strcmp(user, pwd) == 0) {
		errorOut = B_TRANSLATE("Password must not equal the username.");
		return false;
	}
	if (fAdvancedCheck->Value() == B_CONTROL_ON) {
		const char* rp = fRootPasswordField->Text();
		if (rp == NULL || *rp == '\0') {
			errorOut = B_TRANSLATE("Advanced: enter a root password "
				"or uncheck Advanced.");
			return false;
		}
		if (strcmp(rp, fRootConfirmField->Text()) != 0) {
			errorOut = B_TRANSLATE("Root password confirmation "
				"does not match.");
			return false;
		}
	}
	if (!_SwapSizeOk()) {
		errorOut = B_TRANSLATE("Enter the swap size as a number of MiB "
			"greater than 0, or choose another swap option.");
		return false;
	}
	return true;
}


// xkb rule options implied by the running key_map.
static BString
current_xkb_options()
{
	BString options;
	key_map* map = NULL;
	char* buffer = NULL;
	get_key_map(&map, &buffer);
	if (map != NULL)
		options = look_up_xkb_modifier_options(*map);

	free(map);
	free(buffer);
	return options;
}


// Handles bare "layout=de" and shell-quoted XKBLAYOUT="de".
static bool
read_keyboard_file(const char* path, BString& layout, BString& variant,
	BString& options)
{
	BFile file(path, B_READ_ONLY);
	off_t size;
	if (file.InitCheck() != B_OK || file.GetSize(&size) != B_OK || size <= 0)
		return false;

	if (size > 8192)
		size = 8192;
	BString contents;
	char* buffer = contents.LockBuffer(size + 1);
	if (buffer == NULL)
		return false;
	ssize_t read = file.Read(buffer, size);
	buffer[read > 0 ? read : 0] = '\0';
	contents.UnlockBuffer();

	int32 offset = 0;
	while (offset < contents.Length()) {
		int32 end = contents.FindFirst('\n', offset);
		if (end < 0)
			end = contents.Length();

		BString line;
		contents.CopyInto(line, offset, end - offset);
		offset = end + 1;

		int32 equals = line.FindFirst('=');
		if (equals < 0)
			continue;

		BString key;
		BString value;
		line.CopyInto(key, 0, equals);
		line.CopyInto(value, equals + 1, line.Length() - equals - 1);
		key.Trim();
		value.Trim();
		value.RemoveAll("\"");

		if (key == "layout" || key == "XKBLAYOUT")
			layout = value;
		else if (key == "variant" || key == "XKBVARIANT")
			variant = value;
		else if (key == "options" || key == "XKBOPTIONS")
			options = value;
	}

	return layout.Length() > 0;
}


// Written by FirstBootPrompt; keep the two in step.
static const char* kFirstBootHandoffPath = "/run/vos/firstboot.conf";


static BString
first_boot_language()
{
	BFile file(kFirstBootHandoffPath, B_READ_ONLY);
	char buffer[256];
	ssize_t read = file.InitCheck() == B_OK
		? file.Read(buffer, sizeof(buffer) - 1) : -1;
	if (read <= 0)
		return BString();

	buffer[read] = '\0';
	BString contents(buffer);
	int32 start = contents.FindFirst("language=");
	if (start < 0)
		return BString();

	start += strlen("language=");
	int32 end = contents.FindFirst('\n', start);
	if (end < 0)
		end = contents.Length();

	BString language;
	contents.CopyInto(language, start, end - start);
	language.Trim();
	return language;
}


// Resolved the same way the keyboard add-on does: input/layout, then the
// xkb_layout cache, then /etc/default/keyboard.
static bool
current_keyboard_layout(BString& layout, BString& variant, BString& options)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		BPath namePath(path);
		namePath.Append("input/layout");

		char name[B_FILE_NAME_LENGTH + 1];
		BFile file(namePath.Path(), B_READ_ONLY);
		ssize_t read = file.InitCheck() == B_OK
			? file.Read(name, sizeof(name) - 1) : -1;
		if (read > 0) {
			name[read] = '\0';
			BString keymapName(name);
			keymapName.Trim();
			if (keymapName.Length() > 0) {
				const char* xkbLayout;
				const char* xkbVariant;
				look_up_xkb_layout(keymapName.String(), xkbLayout, xkbVariant);
				layout = xkbLayout;
				variant = xkbVariant;
				// Derived from the running key_map, so the installed
				// system inherits the arrangement actually in force.
				options = current_xkb_options();
				return true;
			}
		}

		BPath cachePath(path);
		cachePath.Append("input/xkb_layout");
		if (read_keyboard_file(cachePath.Path(), layout, variant, options))
			return true;
	}

	return read_keyboard_file("/etc/default/keyboard", layout, variant,
		options);
}


BString
InstallerWindow::_ComposeSetupConf()
{
	BString conf;
	const char* full = fFullNameField->Text();
	const char* host = fHostnameField->Text();
	const char* user = fUserField->Text();

	BString language = first_boot_language();
	if (language.Length() > 0)
		conf << "language=" << language << "\n";

	BString layout;
	BString variant;
	BString options;
	if (current_keyboard_layout(layout, variant, options)) {
		conf << "keymap="         << layout  << "\n";
		conf << "keymap_variant=" << variant << "\n";
		conf << "keymap_options=" << options << "\n";
	}

	conf << "username="  << user << "\n";
	conf << "hostname="  << host << "\n";
	if (full != NULL && *full != '\0')
		conf << "full_name=" << full << "\n";
	conf << "sudo_enabled=" <<
		(fSudoCheck->Value() == B_CONTROL_ON ? "1" : "0") << "\n";
	conf << "autologin=" <<
		(fAutologinCheck->Value() == B_CONTROL_ON ? "1" : "0") << "\n";

	conf << "password=" << fPasswordField->Text() << "\n";
	if (fAdvancedCheck->Value() == B_CONTROL_ON)
		conf << "root_password=" << fRootPasswordField->Text() << "\n";
	return conf;
}


void
InstallerWindow::_SetCopyEngineCancelSemaphore(sem_id id, bool alreadyLocked)
{
	if (fCopyEngineCancelSemaphore >= 0 && !alreadyLocked)
		delete_sem(fCopyEngineCancelSemaphore);
	fCopyEngineCancelSemaphore = id;
}


void
InstallerWindow::_QuitCopyEngine(bool /*askUser*/)
{
	if (fWorkerThread != NULL)
		fWorkerThread->Cancel();
}


void
InstallerWindow::_LaunchDriveSetup()
{
	if (be_roster->Launch(kDriveSetupSignature) != B_OK) {
		// The mime database can be incomplete on live media; fall back
		// to the known install path like the legacy installer did.
		BPath path;
		if (find_directory(B_SYSTEM_APPS_DIRECTORY, &path) != B_OK
			|| path.Append("DriveSetup") != B_OK) {
			path.SetTo("/boot/system/apps/DriveSetup");
		}
		BEntry entry(path.Path());
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK || be_roster->Launch(&ref) != B_OK) {
			BAlert* alert = new BAlert(B_TRANSLATE("DriveSetup"),
				B_TRANSLATE("DriveSetup, the application to configure "
					"disk partitions, could not be launched."),
				B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL,
				B_STOP_ALERT);
			alert->Go();
			return;
		}
	}
	fLaunchDriveSetupButton->SetEnabled(false);
}


void
InstallerWindow::_ClearPlan()
{
	fPlanText.Truncate(0);
	fPlanSwapHint.Truncate(0);
	fPlanSwapHintMiB = 0;
	_ScanPartitions();
}


void
InstallerWindow::_RebuildBootloaderMenu()
{
	BMenuItem* item;
	while ((item = fBootloaderChoiceMenu->RemoveItem((int32)0)) != NULL)
		delete item;
	while ((item = fSwapChoiceMenu->RemoveItem((int32)0)) != NULL)
		delete item;

	// The disk holding the selected target; a planned target is judged by
	// what the plan leaves on its disk.
	BootDiskInfo disk;
	bool inPlace = false;
	PartitionMenuItem* target = (PartitionMenuItem*)fDestMenu->FindMarked();
	if (target != NULL) {
		if (target->ID() == (partition_id)kInPlacePartitionId) {
			inPlace = true;
		} else if (target->ID() == (partition_id)kPlannedPartitionId) {
			BString planDisk;
			int32 at = fPlanText.FindFirst("disk=");
			if (at >= 0) {
				int32 end = fPlanText.FindFirst('\n', at);
				if (end < 0)
					end = fPlanText.Length();
				fPlanText.CopyInto(planDisk, at + 5, end - at - 5);
				planDisk.Trim();
			}
			if (!planDisk.IsEmpty() && !find_disk(planDisk.String(), disk))
				disk.path = planDisk;
			apply_plan_to_disk(fPlanText, disk);
			disk.known = !disk.path.IsEmpty();
		} else {
			BDiskDeviceRoster roster;
			BDiskDevice device;
			BPartition* partition = NULL;
			if (roster.GetPartitionWithID(target->ID(), &device, &partition)
					== B_OK) {
				collect_disk_info(device, disk);
			}
		}
	}

	// UEFI: target-disk ESPs first, then those on other disks.
	BMessage espNodes;
	const char* node;
	for (int32 i = 0; disk.esps.FindString("node", i, &node) == B_OK; i++)
		espNodes.AddString("node", node);
	if (disk.planEsp)
		espNodes.AddString("node", "");
	BDiskDeviceRoster roster;
	roster.RewindDevices();
	BDiskDevice device;
	while (!inPlace && roster.GetNextDevice(&device) == B_OK) {
		BootDiskInfo other;
		collect_disk_info(device, other);
		if (other.path == disk.path)
			continue;
		for (int32 i = 0; other.esps.FindString("node", i, &node) == B_OK;
				i++) {
			espNodes.AddString("node", node);
		}
	}
	bool uefiPossible = disk.known && espNodes.HasString("node");
	bool biosPossible = disk.known && (disk.mbr
		|| (disk.gpt && (disk.biosBoot || disk.planBios)));
	bool biosNeedsPartition = disk.known && disk.gpt && !biosPossible;
	int32 espCount = 0;
	espNodes.GetInfo("node", NULL, &espCount);

	BString biosLabel;
	biosLabel.SetToFormat(B_TRANSLATE("Legacy BIOS (MBR of %s)"),
		disk.path.String());

	struct Entry {
		BString	label;
		BString	choice;
		bool	enabled;
	};
	BList entries;
	BString firstUefi, firstBoth;
	for (int32 i = 0; uefiPossible && espNodes.FindString("node", i, &node)
			== B_OK; i++) {
		BString where = node[0] != '\0' ? BString(node)
			: BString(B_TRANSLATE("new EFI partition"));
		Entry* e = new Entry;
		e->label.SetToFormat(B_TRANSLATE("UEFI (%s)"), where.String());
		e->choice = BString("esp:") << node;
		e->enabled = true;
		entries.AddItem(e);
		if (firstUefi.IsEmpty())
			firstUefi = e->choice;
	}
	{
		Entry* e = new Entry;
		e->label = biosLabel;
		if (biosNeedsPartition)
			e->label << " " << B_TRANSLATE("— needs a 1 MiB BIOS boot "
				"partition");
		e->choice = "bios:disk";
		e->enabled = biosPossible;
		if (biosPossible || biosNeedsPartition)
			entries.AddItem(e);
		else
			delete e;
	}
	for (int32 i = 0; uefiPossible && biosPossible
			&& espNodes.FindString("node", i, &node) == B_OK; i++) {
		Entry* e = new Entry;
		if (espCount > 1) {
			BString where = node[0] != '\0' ? BString(node)
				: BString(B_TRANSLATE("new EFI partition"));
			e->label.SetToFormat(B_TRANSLATE("Both (UEFI %s + legacy BIOS)"),
				where.String());
		} else {
			e->label = B_TRANSLATE("Both (UEFI + legacy BIOS)");
		}
		e->choice = BString("both:") << node;
		e->enabled = true;
		entries.AddItem(e);
		if (firstBoth.IsEmpty())
			firstBoth = e->choice;
	}
	{
		Entry* e = new Entry;
		e->label = B_TRANSLATE("Do not install a boot loader");
		e->choice = "none";
		e->enabled = true;
		entries.AddItem(e);
	}

	// Default follows the running firmware; a removable disk boots
	// anywhere, so it gets both loaders when possible.
	bool uefiFirmware = access("/sys/firmware/efi", F_OK) == 0;
	BString wanted;
	if (!firstBoth.IsEmpty() && disk.removable)
		wanted = firstBoth;
	else if (uefiFirmware && uefiPossible)
		wanted = firstUefi;
	else if (biosPossible)
		wanted = "bios:disk";
	else if (uefiPossible)
		wanted = firstUefi;
	else
		wanted = "none";
	if (fBootloaderExplicit) {
		bool still = false;
		for (int32 i = 0; i < entries.CountItems(); i++) {
			Entry* e = (Entry*)entries.ItemAt(i);
			if (e->enabled && e->choice == fBootloaderChoice)
				still = true;
		}
		if (still)
			wanted = fBootloaderChoice;
		else
			fBootloaderExplicit = false;
	}

	for (int32 i = 0; i < entries.CountItems(); i++) {
		Entry* e = (Entry*)entries.ItemAt(i);
		BMessage* message = new BMessage(kMsgBootChoice);
		message->AddString("choice", e->choice);
		BMenuItem* menuItem = new BMenuItem(e->label.String(), message);
		menuItem->SetEnabled(e->enabled);
		fBootloaderChoiceMenu->AddItem(menuItem);
		if (e->enabled && e->choice == wanted)
			menuItem->SetMarked(true);
		delete e;
	}
	fBootloaderChoiceMenu->SetTargetForItems(this);
	fBootloaderChoice = wanted;
	fBootloaderField->SetEnabled(!inPlace);
	fNvramCheck->SetEnabled(!inPlace && uefiFirmware
		&& (fBootloaderChoice.StartsWith("esp:")
			|| fBootloaderChoice.StartsWith("both:")));

	// Swap: partitions on the target disk, the plan's new one, a file.
	bool planned = target != NULL
		&& target->ID() == (partition_id)kPlannedPartitionId;
	BString firstPart;
	for (int32 i = 0; disk.swaps.FindString("node", i, &node) == B_OK; i++) {
		int64 mib = 0;
		disk.swaps.FindInt64("mib", i, &mib);
		BString label;
		label.SetToFormat(B_TRANSLATE("Swap partition %s (%s)"), node,
			mib_label(mib).String());
		BMessage* message = new BMessage(kMsgSwapChoice);
		message->AddString("choice", BString("part:") << node);
		message->AddInt64("mib", mib);
		fSwapChoiceMenu->AddItem(new BMenuItem(label.String(), message));
		if (firstPart.IsEmpty())
			firstPart = BString("part:") << node;
	}
	if (planned && disk.planSwap) {
		BString label;
		label.SetToFormat(B_TRANSLATE("New swap partition (%s)"),
			mib_label(disk.planSwapMiB).String());
		BMessage* message = new BMessage(kMsgSwapChoice);
		message->AddString("choice", "plan");
		fSwapChoiceMenu->AddItem(new BMenuItem(label.String(), message));
	}
	BMessage* fileMessage = new BMessage(kMsgSwapChoice);
	fileMessage->AddString("choice", "file");
	fSwapChoiceMenu->AddItem(new BMenuItem(B_TRANSLATE("Swap file"),
		fileMessage));
	BMessage* noneMessage = new BMessage(kMsgSwapChoice);
	noneMessage->AddString("choice", "none");
	fSwapChoiceMenu->AddItem(new BMenuItem(B_TRANSLATE("None"),
		noneMessage));
	fSwapChoiceMenu->SetTargetForItems(this);

	BString wantedSwap = !firstPart.IsEmpty() ? firstPart : BString("file");
	if (planned && disk.planSwap) {
		if (fPlanSwapHint == "file")
			wantedSwap = "file";
		else if (fPlanSwapHint == "none")
			wantedSwap = "none";
		else
			wantedSwap = "plan";
	} else if (planned && !fPlanSwapHint.IsEmpty())
		wantedSwap = fPlanSwapHint == "none" ? "none" : "file";
	if (planned && fPlanSwapHint == "file" && fPlanSwapHintMiB > 0)
		fSwapFileSizeText.SetToFormat("%" B_PRId64, fPlanSwapHintMiB);
	if (fSwapExplicit) {
		bool still = false;
		for (int32 i = 0; i < fSwapChoiceMenu->CountItems(); i++) {
			BMenuItem* menuItem = fSwapChoiceMenu->ItemAt(i);
			const char* choice = NULL;
			if (menuItem->Message()->FindString("choice", &choice) == B_OK
				&& fSwapChoice == choice) {
				still = true;
			}
		}
		if (still)
			wantedSwap = fSwapChoice;
		else
			fSwapExplicit = false;
	}
	for (int32 i = 0; i < fSwapChoiceMenu->CountItems(); i++) {
		BMenuItem* menuItem = fSwapChoiceMenu->ItemAt(i);
		const char* choice = NULL;
		if (menuItem->Message()->FindString("choice", &choice) == B_OK
			&& wantedSwap == choice) {
			menuItem->SetMarked(true);
		}
	}
	fSwapChoice = wantedSwap;
	fPlanSwapMiB = (planned && disk.planSwap) ? disk.planSwapMiB : 0;
	fSwapField->SetEnabled(!inPlace);
	_UpdateSwapControls();
	_UpdateControls();
}


// Syncs the swap size field and the hibernation note with the choice.
void
InstallerWindow::_UpdateSwapControls()
{
	bool inPlace = false;
	PartitionMenuItem* target = (PartitionMenuItem*)fDestMenu->FindMarked();
	if (target != NULL)
		inPlace = target->ID() == (partition_id)kInPlacePartitionId;

	const char* shown = NULL;
	BString fixedText;
	bool editable = false;
	if (fSwapChoice == "file") {
		// An edit while a file is chosen is the user's value.
		if (fSwapSizeField->IsEnabled()
			&& strcmp(fSwapSizeField->Text(), fSwapFileSizeText.String())
				!= 0) {
			fSwapFileSizeText = fSwapSizeField->Text();
		}
		shown = fSwapFileSizeText.String();
		editable = true;
	} else if (fSwapChoice == "plan") {
		fixedText << fPlanSwapMiB;
		shown = fixedText.String();
	} else if (fSwapChoice.StartsWith("part:")) {
		BMenuItem* marked = fSwapChoiceMenu->FindMarked();
		int64 mib = 0;
		if (marked != NULL)
			marked->Message()->FindInt64("mib", &mib);
		fixedText << mib;
		shown = fixedText.String();
	}

	if (shown != NULL && strcmp(fSwapSizeField->Text(), shown) != 0)
		fSwapSizeField->SetText(shown);
	fSwapSizeField->SetEnabled(editable && !inPlace
		&& fInstallStatus == kReadyForInstall);

	BString note;
	if (inPlace || fSwapChoice == "none") {
		// nothing to warn about
	} else if (!_SwapSizeOk()) {
		note = B_TRANSLATE("Enter the swap size in MiB (a number above 0). "
			"Install stays disabled until then.");
	} else {
		int64 ram = installer_ram_mib();
		int64 size = fSwapChoice == "file" ? _SwapSizeMiB()
			: strtoll(fSwapSizeField->Text(), NULL, 10);
		if (size < ram)
			note.SetToFormat(B_TRANSLATE("Hibernation needs at least "
				"%" B_PRId64 " MiB"), ram);
	}
	fSwapNote->SetText(note.String());
	// Hide() nests; only toggle on a real change.
	if (note.IsEmpty() != fSwapNote->IsHidden()) {
		if (note.IsEmpty())
			fSwapNote->Hide();
		else
			fSwapNote->Show();
	}
}


int64
InstallerWindow::_SwapSizeMiB() const
{
	const char* text = fSwapSizeField->Text();
	char* end = NULL;
	int64 value = strtoll(text, &end, 10);
	if (end == text || *end != '\0' || value <= 0)
		return 0;
	return value;
}


// Only the swap file takes a typed size; the others carry theirs.
bool
InstallerWindow::_SwapSizeOk() const
{
	PartitionMenuItem* target = (PartitionMenuItem*)fDestMenu->FindMarked();
	if (target != NULL && target->ID() == (partition_id)kInPlacePartitionId)
		return true;
	return fSwapChoice != "file" || _SwapSizeMiB() > 0;
}


void
InstallerWindow::_ReadSwapChoice()
{
	BMenuItem* marked = fSwapChoiceMenu->FindMarked();
	const char* choice = NULL;
	if (marked != NULL && marked->Message() != NULL
		&& marked->Message()->FindString("choice", &choice) == B_OK) {
		fSwapChoice = choice;
	}
}


void
InstallerWindow::_ReadBootloaderChoice()
{
	BMenuItem* marked = fBootloaderChoiceMenu->FindMarked();
	const char* choice = NULL;
	if (marked != NULL && marked->Message() != NULL
		&& marked->Message()->FindString("choice", &choice) == B_OK) {
		fBootloaderChoice = choice;
	}
}


// The single confirmation lists every change; nothing is written until
// the user accepts this summary.
BString
InstallerWindow::_ComposeSummary(const char* targetLabel, bool inPlace)
{
	BString summary;
	if (inPlace) {
		summary = B_TRANSLATE("Set up this system with the account "
			"details above? The live persona will be removed and the "
			"machine will reboot into normal login.");
		return summary;
	}

	summary = B_TRANSLATE("Nothing is written until you confirm. "
		"Afterwards, in order:");
	summary << "\n\n";

	if (!fPlanText.IsEmpty()) {
		BString kind, id, ref, fs, gtype, role, size, start;
		// the trailing blank line flushes the last op
		BString planText(fPlanText);
		planText << "\n";
		int32 offset = 0;
		while (offset < planText.Length()) {
			int32 end = planText.FindFirst('\n', offset);
			if (end < 0)
				end = planText.Length();
			BString line;
			planText.CopyInto(line, offset, end - offset);
			offset = end + 1;
			line.Trim();

			if (line == "[op]") {
				kind.Truncate(0); id.Truncate(0); ref.Truncate(0);
				fs.Truncate(0); gtype.Truncate(0); role.Truncate(0);
				size.Truncate(0); start.Truncate(0);
				continue;
			}
			if (!line.StartsWith("[") && line.FindFirst('=') > 0) {
				int32 equals = line.FindFirst('=');
				BString key, value;
				line.CopyInto(key, 0, equals);
				line.CopyInto(value, equals + 1,
					line.Length() - equals - 1);
				if (key == "kind") kind = value;
				else if (key == "id") id = value;
				else if (key == "target_ref") ref = value;
				else if (key == "filesystem") fs = value;
				else if (key == "gpt_type") gtype = value;
				else if (key == "role") role = value;
				else if (key == "size_mib") size = value;
				else if (key == "start_mib") start = value;
				continue;
			}
			if (kind.IsEmpty())
				continue;
			summary << "- ";
			if (kind == "create_table") {
				summary << B_TRANSLATE("Create a new partition table")
					<< " (" << gtype << ")";
			} else if (kind == "create") {
				summary << B_TRANSLATE("Create partition") << " "
					<< id;
				if (size != "-1")
					summary << " (" << size << " MiB, "
						<< B_TRANSLATE("at") << " " << start
						<< " MiB, " << gtype << ")";
				else
					summary << " (" << B_TRANSLATE("rest of disk")
						<< ", " << gtype << ")";
				if (!role.IsEmpty() && role != "none")
					summary << ", " << role;
			} else if (kind == "format") {
				summary << B_TRANSLATE("Format") << " " << ref
					<< " " << B_TRANSLATE("as") << " " << fs;
				if (!role.IsEmpty() && role != "none")
					summary << " (" << role << ")";
			} else if (kind == "delete") {
				summary << B_TRANSLATE("Delete") << " " << ref;
			} else if (kind == "set_flags") {
				summary << B_TRANSLATE("Set boot flags on") << " "
					<< ref;
			} else {
				summary << kind << " " << ref;
			}
			summary << "\n";
			kind.Truncate(0);
		}
		summary << "\n";
	}

	summary << "- " << B_TRANSLATE("Copy the running system to:")
		<< " " << targetLabel << "\n";
	summary << "- " << B_TRANSLATE("Create the user account; write "
		"fstab, hostname, locale and initramfs") << "\n";

	if (fBootloaderChoice.StartsWith("esp:")) {
		summary << "- " << B_TRANSLATE("Install the UEFI bootloader to "
			"the chosen ESP. Files go under \\EFI\\Vitruvian; "
			"existing boot entries are kept.") << "\n";
	} else if (fBootloaderChoice.StartsWith("both:")) {
		summary << "- " << B_TRANSLATE("Install the UEFI bootloader to "
			"the chosen ESP and the BIOS bootloader (GRUB) to the master "
			"boot record of the target disk.") << "\n";
	} else if (fBootloaderChoice == "bios:disk") {
		summary << "- " << B_TRANSLATE("Install the BIOS bootloader "
			"(GRUB) to the master boot record of the target disk.")
			<< "\n";
	} else {
		summary << "- " << B_TRANSLATE("No bootloader. The installed "
			"system will not boot until you install one.") << "\n";
	}

	if (fSwapChoice.StartsWith("part:")) {
		summary << "- " << B_TRANSLATE("Use the swap partition")
			<< " " << (fSwapChoice.String() + 5) << "\n";
	} else if (fSwapChoice == "plan") {
		summary << "- " << B_TRANSLATE("Use the new swap partition from "
			"the plan") << "\n";
	} else if (fSwapChoice == "file") {
		summary << "- " << B_TRANSLATE("Create a swap file of")
			<< " " << _SwapSizeMiB() << " MiB\n";
	} else {
		summary << "- " << B_TRANSLATE("No swap; hibernation will not be "
			"available.") << "\n";
	}

	PartitionMenuItem* marked = (PartitionMenuItem*)fDestMenu->FindMarked();
	if (marked != NULL && marked->ID() != (partition_id)kPlannedPartitionId
		&& fPlanText.FindFirst("kind=format") < 0) {
		summary << "\n" << B_TRANSLATE("The target partition is not "
			"formatted. Files already on it are kept and may conflict "
			"with the installed system. Formatting it with Guided "
			"partitioning is recommended.") << "\n";
	}

	summary << "\n" << B_TRANSLATE("This cannot be undone. Continue?");
	return summary;
}
