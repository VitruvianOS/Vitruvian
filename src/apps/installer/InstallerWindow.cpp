/*
 * Copyright 2024, Vitruvian OS.
 * Based on Haiku Installer, Copyright 2005-2020, various authors.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "InstallerWindow.h"

#include <stdio.h>
#include <strings.h>

#include <Alert.h>
#include <Application.h>
#include <Autolock.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <LayoutUtils.h>
#include <Locale.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <StatusBar.h>
#include <String.h>
#include <TextView.h>

#include "InstallerDefs.h"
#include "PartitionMenuItem.h"
#include "tracker_private.h"
#include "WorkerThread.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InstallerWindow"


static const char* kDriveSetupSignature = "application/x-vnd.Haiku-DriveSetup";

const uint32 BEGIN_MESSAGE = 'iBGN';
const uint32 LAUNCH_DRIVE_SETUP = 'iSEP';
const uint32 START_SCAN = 'iSSC';
const uint32 ENCOURAGE_DRIVESETUP = 'iENC';


InstallerWindow::InstallerWindow()
	:
	BWindow(BRect(-2400, -2000, -1800, -1800),
		B_TRANSLATE_SYSTEM_NAME("Installer"), B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fDriveSetupLaunched(false),
	fInstallStatus(kReadyForInstall),
	fWorkerThread(new WorkerThread(this)),
	fCopyEngineCancelSemaphore(-1)
{
	if (!be_roster->IsRunning(kTrackerSignature))
		SetWorkspaces(B_ALL_WORKSPACES);

	rgb_color baseColor = ui_color(B_DOCUMENT_TEXT_COLOR);
	fStatusView = new BTextView("statusView", be_plain_font, &baseColor,
		B_WILL_DRAW);
	fStatusView->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fStatusView->MakeEditable(false);
	fStatusView->MakeSelectable(false);

	font_height height;
	fStatusView->GetFontHeight(&height);
	float fontHeight = height.ascent + height.descent + height.leading;
	fStatusView->SetExplicitMinSize(BSize(fStatusView->StringWidth("W") * 28,
		fontHeight * 5 + 8));

	BGroupView* statusGroup = new BGroupView(B_HORIZONTAL, 10);
	statusGroup->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	statusGroup->GroupLayout()->SetInsets(0, 0, 10, 0);
	statusGroup->AddChild(fStatusView);

	fDestMenu = new BPopUpMenu(B_TRANSLATE("scanning" B_UTF8_ELLIPSIS),
		true, false);
	fSrcMenu = new BPopUpMenu(B_TRANSLATE("scanning" B_UTF8_ELLIPSIS),
		true, false);

	fSrcMenuField = new BMenuField("srcMenuField",
		B_TRANSLATE("Install from:"), fSrcMenu);
	fSrcMenuField->SetAlignment(B_ALIGN_RIGHT);
	fSrcMenuField->SetEnabled(false);

	fDestMenuField = new BMenuField("destMenuField", B_TRANSLATE("Onto:"),
		fDestMenu);
	fDestMenuField->SetAlignment(B_ALIGN_RIGHT);

	fProgressBar = new BStatusBar("progress",
		B_TRANSLATE("Install progress:  "));
	fProgressBar->SetMaxValue(100.0);

	fBeginButton = new BButton("begin_button", B_TRANSLATE("Begin"),
		new BMessage(BEGIN_MESSAGE));
	fBeginButton->MakeDefault(true);
	fBeginButton->SetEnabled(false);

	fLaunchDriveSetupButton = new BButton("setup_button",
		B_TRANSLATE("Set up partitions" B_UTF8_ELLIPSIS),
		new BMessage(LAUNCH_DRIVE_SETUP));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(statusGroup)
		.Add(new BSeparatorView(B_HORIZONTAL, B_PLAIN_BORDER))
		.AddGroup(B_VERTICAL, B_USE_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_SPACING)
			.AddGrid(new BGridView(B_USE_ITEM_SPACING, B_USE_ITEM_SPACING))
				.AddMenuField(fSrcMenuField, 0, 0)
				.AddMenuField(fDestMenuField, 0, 1)
				.AddGlue(2, 0, 1, 2)
				.Add(BSpaceLayoutItem::CreateVerticalStrut(5), 0, 2, 3)
			.End()
			.Add(fProgressBar)
			.AddGroup(B_HORIZONTAL, B_USE_WINDOW_SPACING)
				.Add(fLaunchDriveSetupButton)
				.AddGlue()
				.Add(fBeginButton)
			.End()
		.End()
	.End();

	fProgressBar->SetExplicitMinSize(
		BSize(fProgressBar->StringWidth("100%") * 5, B_SIZE_UNSET));

	if (!be_roster->IsRunning(kDeskbarSignature))
		SetFlags(Flags() | B_NOT_MINIMIZABLE);

	CenterOnScreen();
	Show();

	be_roster->StartWatching(this);
	fDriveSetupLaunched = be_roster->IsRunning(kDriveSetupSignature);

	if (Lock()) {
		fLaunchDriveSetupButton->SetEnabled(!fDriveSetupLaunched);
		Unlock();
	}

	PostMessage(START_SCAN);
}


InstallerWindow::~InstallerWindow()
{
	_SetCopyEngineCancelSemaphore(-1);
	be_roster->StopWatching(this);
}


void
InstallerWindow::MessageReceived(BMessage *msg)
{
	switch (msg->what) {
		case MSG_RESET:
		{
			_SetCopyEngineCancelSemaphore(-1);

			status_t error;
			if (msg->FindInt32("error", &error) == B_OK) {
				char errorMessage[2048];
				snprintf(errorMessage, sizeof(errorMessage),
					B_TRANSLATE("An error was encountered and the "
					"installation was not completed:\n\n"
					"Error:  %s"), strerror(error));
				BAlert* alert = new BAlert("error", errorMessage, B_TRANSLATE("OK"));
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go();
			}

			_DisableInterface(false);
			_UpdateControls();
			break;
		}
		case START_SCAN:
			_ScanPartitions();
			break;
		case BEGIN_MESSAGE:
			switch (fInstallStatus) {
				case kReadyForInstall:
				{
					PartitionMenuItem* targetItem
						= (PartitionMenuItem*)fDestMenu->FindMarked();
					PartitionMenuItem* srcItem
						= (PartitionMenuItem*)fSrcMenu->FindMarked();
					if (srcItem == NULL || targetItem == NULL)
						break;

					_SetCopyEngineCancelSemaphore(create_sem(1,
						"copy engine cancel"));

					fWorkerThread->SetLock(fCopyEngineCancelSemaphore);
					fInstallStatus = kInstalling;
					fWorkerThread->StartInstall(srcItem->ID(),
						targetItem->ID());
					fBeginButton->SetLabel(B_TRANSLATE("Stop"));
					_DisableInterface(true);

					fProgressBar->SetTo(0.0, NULL, NULL);
					break;
				}
				case kInstalling:
				{
					_QuitCopyEngine(true);
					break;
				}
				case kFinished:
					PostMessage(B_QUIT_REQUESTED);
					break;
				case kCancelled:
					break;
			}
			break;
		case SOURCE_PARTITION:
			_UpdateControls();
			break;
		case TARGET_PARTITION:
			_UpdateControls();
			break;
		case LAUNCH_DRIVE_SETUP:
			_LaunchDriveSetup();
			break;
		case ENCOURAGE_DRIVESETUP:
		{
			BAlert* alert = new BAlert("use drive setup", B_TRANSLATE("No partitions have "
				"been found that are suitable for installation. Please set "
				"up partitions and format at least one partition with a "
				"Linux filesystem (ext4, XFS, or btrfs)."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			break;
		}
		case MSG_STATUS_MESSAGE:
		{
			float progress;
			if (msg->FindFloat("progress", &progress) == B_OK) {
				const char* currentItem;
				if (msg->FindString("item", &currentItem) != B_OK) {
					currentItem = B_TRANSLATE_COMMENT("???",
						"Unknown currently copied item");
				}
				BString trailingLabel;
				int32 currentCount;
				int32 maximumCount;
				if (msg->FindInt32("current", &currentCount) == B_OK
					&& msg->FindInt32("maximum", &maximumCount) == B_OK) {
					char buffer[64];
					snprintf(buffer, sizeof(buffer),
						B_TRANSLATE_COMMENT("%1ld of %2ld", "number of files copied"),
						(long int)currentCount, (long int)maximumCount);
					trailingLabel << buffer;
				} else {
					trailingLabel <<
						B_TRANSLATE_COMMENT("?? of ??", "Unknown progress");
				}
				fProgressBar->SetTo(progress, currentItem,
					trailingLabel.String());
			} else {
				const char *status;
				if (msg->FindString("status", &status) == B_OK) {
					fLastStatus = fStatusView->Text();
					_SetStatusMessage(status);
				} else
					_SetStatusMessage(fLastStatus.String());
			}
			break;
		}
		case MSG_INSTALL_FINISHED:
		{
			_SetCopyEngineCancelSemaphore(-1);

			BString status;
			if (be_roster->IsRunning(kDeskbarSignature)) {
				fBeginButton->SetLabel(B_TRANSLATE("Quit"));
				status = B_TRANSLATE("Installation "
					"completed. GRUB bootloader has been installed. Press "
					"'Quit' to leave the Installer or choose a new target "
					"volume to perform another installation.");
			} else {
				fBeginButton->SetLabel(B_TRANSLATE("Quit"));
				status = B_TRANSLATE("Installation "
					"completed. GRUB bootloader has been installed. Press "
					"'Quit' to leave the Installer.");
			}

			_SetStatusMessage(status.String());
			fInstallStatus = kFinished;
			_DisableInterface(false);
			break;
		}
		case B_SOME_APP_LAUNCHED:
		case B_SOME_APP_QUIT:
		{
			const char *signature;
			if (msg->FindString("be:signature", &signature) != B_OK)
				break;
			bool isDriveSetup = !strcasecmp(signature, kDriveSetupSignature);
			if (isDriveSetup) {
				bool launched = msg->what == B_SOME_APP_LAUNCHED;
				bool scanPartitions = fDriveSetupLaunched && !launched;
				fDriveSetupLaunched = launched;

				fBeginButton->SetEnabled(!fDriveSetupLaunched);
				_DisableInterface(fDriveSetupLaunched);
				if (fDriveSetupLaunched) {
					_SetStatusMessage(B_TRANSLATE("Running DriveSetup"
						B_UTF8_ELLIPSIS
						"\n\nClose DriveSetup to continue with the "
						"installation."));
				} else {
					if (scanPartitions)
						_ScanPartitions();
					else if (fInstallStatus != kFinished)
						_UpdateControls();
					else
						PostMessage(MSG_INSTALL_FINISHED);
				}
			}
			break;
		}

		default:
			BWindow::MessageReceived(msg);
			break;
	}
}


bool
InstallerWindow::QuitRequested()
{
	if ((Flags() & B_NOT_MINIMIZABLE) != 0) {
		if (fDriveSetupLaunched) {
			BAlert* alert = new BAlert(B_TRANSLATE("Quit DriveSetup"),
				B_TRANSLATE("Please close the DriveSetup window before "
				"closing the Installer window."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
			return false;
		}
		if (fInstallStatus != kFinished) {
			BAlert* alert = new BAlert(B_TRANSLATE_SYSTEM_NAME("Installer"),
				B_TRANSLATE("Are you sure you want to stop the installation?"),
				B_TRANSLATE("Cancel"), B_TRANSLATE("Stop"), NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() == 0)
				return false;
		}
	} else if (fInstallStatus == kInstalling) {
			BAlert* alert = new BAlert(B_TRANSLATE_SYSTEM_NAME("Installer"),
				B_TRANSLATE("The installation is not complete yet!\n"
				"Are you sure you want to stop it?"),
				B_TRANSLATE("Cancel"), B_TRANSLATE("Stop"), NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() == 0)
				return false;
	}

	_QuitCopyEngine(false);

	BMessage quitWithInstallStatus(B_QUIT_REQUESTED);
	quitWithInstallStatus.AddBool("install_complete",
		fInstallStatus == kFinished);

	fWorkerThread->PostMessage(&quitWithInstallStatus);
	be_app->PostMessage(&quitWithInstallStatus);
	return true;
}


// #pragma mark -


void
InstallerWindow::_LaunchDriveSetup()
{
	if (be_roster->Launch(kDriveSetupSignature) != B_OK) {
		BPath path;
		if (find_directory(B_SYSTEM_APPS_DIRECTORY, &path) != B_OK
			|| path.Append("DriveSetup") != B_OK) {
			path.SetTo("/system/apps/DriveSetup");
		}
		BEntry entry(path.Path());
		entry_ref ref;
		if (entry.GetRef(&ref) != B_OK || be_roster->Launch(&ref) != B_OK) {
			BAlert* alert = new BAlert("error", B_TRANSLATE("DriveSetup, the "
				"application to configure disk partitions, could not be "
				"launched."), B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go();
		}
	}
}


void
InstallerWindow::_DisableInterface(bool disable)
{
	fLaunchDriveSetupButton->SetEnabled(!disable);
	fDestMenuField->SetEnabled(!disable);
}


void
InstallerWindow::_ScanPartitions()
{
	_SetStatusMessage(B_TRANSLATE("Scanning for disks" B_UTF8_ELLIPSIS));

	BMenuItem *item;
	while ((item = fSrcMenu->RemoveItem((int32)0)))
		delete item;
	while ((item = fDestMenu->RemoveItem((int32)0)))
		delete item;

	fWorkerThread->ScanDisksPartitions(fSrcMenu, fDestMenu);

	if (fInstallStatus != kFinished)
		_UpdateControls();
	else
		PostMessage(MSG_INSTALL_FINISHED);
}


void
InstallerWindow::_UpdateControls()
{
	PartitionMenuItem* srcItem = (PartitionMenuItem*)fSrcMenu->FindMarked();

	if (srcItem) {
		fSrcMenuField->MenuItem()->SetLabel(srcItem->MenuLabel());
	} else {
		fSrcMenuField->MenuItem()->SetLabel(
			B_TRANSLATE_COMMENT("This system", "Source partition label"));
	}

	bool foundOneSuitableTarget = false;
	for (int32 i = fDestMenu->CountItems() - 1; i >= 0; i--) {
		PartitionMenuItem* dstItem
			= (PartitionMenuItem*)fDestMenu->ItemAt(i);

		dstItem->SetEnabled(dstItem->IsValidTarget());

		if (dstItem->IsEnabled())
			foundOneSuitableTarget = true;
	}

	PartitionMenuItem* dstItem = (PartitionMenuItem*)fDestMenu->FindMarked();
	if (dstItem) {
		fDestMenuField->MenuItem()->SetLabel(dstItem->MenuLabel());
	} else {
		if (fDestMenu->CountItems() == 0)
			fDestMenuField->MenuItem()->SetLabel(
				B_TRANSLATE_COMMENT("<none>", "No partition available"));
		else
			fDestMenuField->MenuItem()->SetLabel(
				B_TRANSLATE("Please choose target"));
	}

	BString statusText;
	if (srcItem != NULL && dstItem != NULL) {
		statusText.SetToFormat(B_TRANSLATE("Press the 'Begin' button to install "
			"from '%1s' onto '%2s'."), srcItem->Name(), dstItem->Name());
	} else if (srcItem != NULL) {
		if (!foundOneSuitableTarget) {
			statusText = B_TRANSLATE(
				"Vitruvian has to be installed on a partition that uses "
				"a Linux filesystem (ext4, XFS, or btrfs), but there are "
				"currently no such partitions available. "
				"Click on 'Set up partitions" B_UTF8_ELLIPSIS
				"' to create one.");
		} else {
			statusText = B_TRANSLATE(
				"Choose the disk you want to install "
				"onto from the pop-up menu. Then click 'Begin'.");
		}
	} else if (dstItem != NULL) {
		statusText = B_TRANSLATE("Press 'Begin' to start the installation.");
	} else {
		statusText = B_TRANSLATE("Choose the destination disk "
			"from the pop-up menu. Then click 'Begin'.");
	}

	_SetStatusMessage(statusText.String());

	fInstallStatus = kReadyForInstall;
	fBeginButton->SetLabel(B_TRANSLATE("Begin"));
	fBeginButton->SetEnabled(dstItem != NULL);

	if (!foundOneSuitableTarget) {
		PostMessage(ENCOURAGE_DRIVESETUP);
	}
}


void
InstallerWindow::_SetStatusMessage(const char *text)
{
	fStatusView->SetText(text);
	fStatusView->InvalidateLayout();
}


void
InstallerWindow::_SetCopyEngineCancelSemaphore(sem_id id, bool alreadyLocked)
{
	if (fCopyEngineCancelSemaphore >= 0) {
		if (!alreadyLocked)
			acquire_sem(fCopyEngineCancelSemaphore);
		delete_sem(fCopyEngineCancelSemaphore);
	}
	fCopyEngineCancelSemaphore = id;
}


void
InstallerWindow::_QuitCopyEngine(bool askUser)
{
	if (fCopyEngineCancelSemaphore < 0)
		return;

	acquire_sem(fCopyEngineCancelSemaphore);

	bool quit = true;
	if (askUser) {
		BAlert* alert = new BAlert("cancel",
			B_TRANSLATE("Are you sure you want to stop the installation?"),
			B_TRANSLATE_COMMENT("Continue", "In alert after pressing Stop"),
			B_TRANSLATE_COMMENT("Stop", "In alert after pressing Stop"), 0,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		quit = alert->Go() != 0;
	}

	if (quit) {
		_SetCopyEngineCancelSemaphore(-1, true);
	} else
		release_sem(fCopyEngineCancelSemaphore);
}
