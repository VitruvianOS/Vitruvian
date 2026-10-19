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
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <DiskDevice.h>
#include <DiskDeviceRoster.h>
#include <DiskDeviceVisitor.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Partition.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <Keymap.h>

#include "InstallerDefs.h"
#include "PartitionMenuItem.h"
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

// Real partition IDs are >= 0; -2 tags the in-place pseudo-item.
static const int32 kInPlacePartitionId = -2;


// Collects ESPs for the bootloader destination menu.
class BootloaderEspVisitor : public BDiskDeviceVisitor {
public:
	BootloaderEspVisitor(BMenu* menu)
		:
		fMenu(menu)
	{
	}

	virtual bool Visit(BDiskDevice* /*device*/)
	{
		return false;
	}

	virtual bool Visit(BPartition* partition, int32 /*level*/)
	{
		if (partition == NULL || partition->ContentSize() <= 0)
			return false;
		const char* type = partition->Type();
		if (type == NULL)
			return false;
		if (strcmp(type, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") != 0
			&& strcasecmp(type, "EFI") != 0) {
			return false;
		}
		BPath path;
		if (partition->GetPath(&path) != B_OK)
			return false;
		BString contentName = partition->ContentName();
		BString label;
		if (!contentName.IsEmpty())
			label.SetToFormat("%s (%s)", contentName.String(), path.Path());
		else
			label.SetToFormat(B_TRANSLATE("ESP (%s)"), path.Path());
		BMessage* message = new BMessage(kMsgBootChoice);
		message->AddString("choice", BString() << "esp:" << path.Path());
		fMenu->AddItem(new BMenuItem(label.String(), message));
		return false;
	}

private:
	BMenu* fMenu;
};


// Live ISO's / is read-only squashfs; statvfs ST_RDONLY excludes it.
static bool
_in_place_available()
{
	if (access("/etc/vos/live", F_OK) != 0)
		return false;
	struct statvfs vfs;
	if (statvfs("/", &vfs) != 0)
		return false;
	if ((vfs.f_flag & ST_RDONLY) != 0)
		return false;
	return true;
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


InstallerWindow::InstallerWindow()
	:
	BWindow(BRect(0, 0, 520, 480), B_TRANSLATE_SYSTEM_NAME("Installer"),
		B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fInstallStatus(kReadyForInstall),
	fWorkerThread(new WorkerThread(BMessenger(this))),
	fCopyEngineCancelSemaphore(-1)
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
		B_TRANSLATE("Advanced"),
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
		B_TRANSLATE("Create or format target" B_UTF8_ELLIPSIS),
		new BMessage(MSG_TARGET_SETUP));

	fBootloaderChoiceMenu = new BPopUpMenu("bootloaderChoice");
	fBootloaderField = new BMenuField("bootloaderField",
		B_TRANSLATE("Bootloader:"), fBootloaderChoiceMenu);
	fBootloaderChoiceMenu->SetTargetForItems(this);

	BBox* setupBox = new BBox("setup");
	setupBox->SetLabel(B_TRANSLATE("Set up your account"));
	BLayoutBuilder::Group<>(setupBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_ITEM_SPACING)
		.Add(fHostnameField)
		.Add(fFullNameField)
		.Add(fUserField)
		.Add(fPasswordField)
		.Add(fConfirmField)
		.Add(fSudoCheck)
		.Add(fAutologinCheck)
		.Add(fAutologinNote)
		.Add(fAdvancedCheck)
		.Add(fRootPasswordField)
		.Add(fRootConfirmField)
		.Add(fPasswordStatus);

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

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fMenuBar)
		.AddGroup(B_HORIZONTAL)
			.Add(fDestMenuField)
			.Add(fBootloaderField)
		.End()
		.Add(setupBox)
		.Add(fProgressBar)
		.AddGroup(B_HORIZONTAL)
			.Add(fLaunchDriveSetupButton)
			.Add(fTargetSetupButton)
			.AddGlue()
			.Add(fQuitButton)
			.Add(fInstallButton)
		.End();

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
			if (message->FindString("choice", &choice) == B_OK)
				fBootloaderChoice = choice;
			break;
		}

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
			if (!inPlace) {
				if (fBootloaderChoice.StartsWith("esp:")) {
					spec.efiMode = "vitruvian";
					spec.biosMode = "none";
					spec.espPath = fBootloaderChoice.String() + 4;
				} else if (fBootloaderChoice == "bios:disk") {
					spec.efiMode = "none";
					spec.biosMode = "disk";
					spec.espPath = "none";
				} else {
					spec.efiMode = "none";
					spec.biosMode = "none";
					spec.espPath = "none";
				}
			}

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
			bool canBootloader = target != NULL
				&& target->ID() != (partition_id)kInPlacePartitionId;

			if (canBootloader) {
				BAlert* alert = new BAlert(B_TRANSLATE("Installation complete"),
					B_TRANSLATE("Installation completed successfully.\n\n"
						"Install the bootloader on the target disk now? Skip "
						"this if you manage your own bootloader or are "
						"dual-booting."),
					B_TRANSLATE("Skip"), B_TRANSLATE("Install bootloader"),
					NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
				alert->SetShortcut(0, B_ESCAPE);
				if (alert->Go() == 1) {
					fWorkerThread->InstallBootloader(target->ID());
					break;
				}
			} else {
				BAlert* alert = new BAlert(B_TRANSLATE("Installation complete"),
					B_TRANSLATE("Installation completed successfully. "
						"You can now reboot into the installed system."),
					B_TRANSLATE("Quit"), NULL, NULL,
					B_WIDTH_AS_USUAL, B_INFO_ALERT);
				alert->Go();
			}
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
	_ScanPartitions();
}


void
InstallerWindow::_RebuildBootloaderMenu()
{
	BMenuItem* item;
	while ((item = fBootloaderChoiceMenu->RemoveItem((int32)0)) != NULL)
		delete item;

	bool uefi = access("/sys/firmware/efi", F_OK) == 0;
	if (uefi) {
		BDiskDeviceRoster roster;
		roster.RewindDevices();
		BDiskDevice device;
		while (roster.GetNextDevice(&device) == B_OK) {
			BootloaderEspVisitor visitor(fBootloaderChoiceMenu);
			device.VisitEachDescendant(&visitor);
		}
	} else {
		BMessage* message = new BMessage(kMsgBootChoice);
		message->AddString("choice", "bios:disk");
		fBootloaderChoiceMenu->AddItem(new BMenuItem(
			B_TRANSLATE("Master boot record of the target disk"), message));
	}

	BMessage* noneMessage = new BMessage(kMsgBootChoice);
	noneMessage->AddString("choice", "none");
	fBootloaderChoiceMenu->AddItem(new BMenuItem(
		B_TRANSLATE("None (the system will not boot)"), noneMessage));

	int32 count = fBootloaderChoiceMenu->CountItems();
	int32 mark = 0;
	for (int32 i = 0; i < count; i++) {
		BMenuItem* menuItem = fBootloaderChoiceMenu->ItemAt(i);
		const char* choice = NULL;
		if (menuItem->Message() != NULL
			&& menuItem->Message()->FindString("choice", &choice) == B_OK
			&& fBootloaderChoice == choice) {
			mark = i;
			break;
		}
	}
	if (mark < count) {
		fBootloaderChoiceMenu->ItemAt(mark)->SetMarked(true);
		BMenuItem* marked = fBootloaderChoiceMenu->FindMarked();
		const char* choice = NULL;
		if (marked != NULL && marked->Message() != NULL
			&& marked->Message()->FindString("choice", &choice) == B_OK) {
			fBootloaderChoice = choice;
		}
	}
	if (fBootloaderChoice.IsEmpty())
		fBootloaderChoice = uefi ? "none" : "bios:disk";
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
		int32 offset = 0;
		while (offset < fPlanText.Length()) {
			int32 end = fPlanText.FindFirst('\n', offset);
			if (end < 0)
				end = fPlanText.Length();
			BString line;
			fPlanText.CopyInto(line, offset, end - offset);
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
	} else if (fBootloaderChoice == "bios:disk") {
		summary << "- " << B_TRANSLATE("Install the BIOS bootloader "
			"(GRUB) to the master boot record of the target disk.")
			<< "\n";
	} else {
		summary << "- " << B_TRANSLATE("No bootloader. The installed "
			"system will not boot until you install one.") << "\n";
	}

	summary << "\n" << B_TRANSLATE("This cannot be undone. Continue?");
	return summary;
}
