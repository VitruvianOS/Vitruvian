/*
 * Copyright 2009-2011, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2005, Jérôme DUVAL
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "InstallerWindow.h"

#include <ctype.h>
#include <stdio.h>
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
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include "InstallerDefs.h"
#include "PartitionMenuItem.h"
#include "WorkerThread.h"


#define B_TRANSLATION_CONTEXT "InstallerWindow"


static const uint32 kMsgBegin           = 'iBGN';
static const uint32 kMsgInit            = 'iINI';
static const uint32 kMsgQuit            = 'iQTA';
static const uint32 kMsgAdvancedToggled = 'iADV';
static const uint32 kMsgPasswordChanged = 'iPWC';
static const uint32 kMsgInPlaceSelected = 'iINP';

// Real partition IDs are >= 0; -2 tags the in-place pseudo-item.
static const int32 kInPlacePartitionId = -2;


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

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL)
			.Add(fDestMenuField)
		.End()
		.Add(setupBox)
		.Add(fProgressBar)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fQuitButton)
			.Add(fInstallButton)
		.End();

	_UpdateAdvancedEnabled();
	_UpdatePasswordStatus();

	CenterOnScreen();
	Show();

	PostMessage(kMsgInit);
}


InstallerWindow::~InstallerWindow()
{
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
					item->ID() == kInPlacePartitionId);
			}
			_UpdateControls();
			break;
		}

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
			bool inPlace = (item->ID() == kInPlacePartitionId);
			const char* prompt = inPlace
				? B_TRANSLATE("Set up this system with the account "
					"details above? The live persona will be removed "
					"and the machine will reboot into normal login.")
				: B_TRANSLATE("The selected volume will be erased and "
					"Vitruvian will be installed to it. This cannot be "
					"undone. Continue?");
			BAlert* alert = new BAlert(B_TRANSLATE("Confirm install"),
				prompt,
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

			sem_id cancelSem = create_sem(0, "installer_cancel");
			_SetCopyEngineCancelSemaphore(cancelSem);
			fWorkerThread->SetLock(cancelSem);
			fWorkerThread->SetSetupConf(conf);
			explicit_bzero(conf.LockBuffer(0), conf.Length());
			conf.UnlockBuffer(0);
			fWorkerThread->StartInstall(item->ID());
			break;
		}

		case MSG_STATUS_MESSAGE:
		{
			const char* text = NULL;
			if (message->FindString("status", &text) == B_OK)
				fProgressBar->SetText(text);
			off_t bytesWritten = 0;
			if (message->FindInt64("current bytes", &bytesWritten) == B_OK) {
				off_t total = 100;
				message->FindInt64("total bytes", &total);
				float pct = 0.0f;
				if (total > 0)
					pct = 100.0f * bytesWritten / total;
				fProgressBar->SetTo(pct);
			}
			break;
		}

		case MSG_INSTALL_FINISHED:
		{
			_SetCopyEngineCancelSemaphore(-1);
			fInstallStatus = kFinished;
			fProgressBar->SetTo(100.0f);

			BAlert* alert = new BAlert(B_TRANSLATE("Installation complete"),
				B_TRANSLATE("Installation completed successfully. "
					"You can now reboot into the installed system."),
				B_TRANSLATE("Quit"), NULL, NULL,
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

			BString msg;
			msg.SetToFormat("%s: %s",
				B_TRANSLATE("Installation failed"), strerror(err));
			BAlert* alert = new BAlert(B_TRANSLATE("Installation failed"),
				msg.String(), B_TRANSLATE("OK"), NULL, NULL,
				B_WIDTH_AS_USUAL, B_STOP_ALERT);
			alert->Go();
			break;
		}

		case kMsgQuit:
			PostMessage(B_QUIT_REQUESTED);
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

	fWorkerThread->ScanDisksPartitions(fDestMenu);

	if (_in_place_available()) {
		PartitionMenuItem* inPlace = new PartitionMenuItem("current",
			B_TRANSLATE("Current system (in-place)"),
			B_TRANSLATE("Current system (in-place)"),
			new BMessage(TARGET_PARTITION), kInPlacePartitionId);
		inPlace->SetIsValidTarget(true);
		fDestMenu->AddItem(inPlace, 0);
	}

	if (fDestMenu->CountItems() == 0) {
		fDestMenuField->MenuItem()->SetLabel(
			B_TRANSLATE("No writable partitions found"));
		fInstallButton->SetEnabled(false);
	} else {
		fDestMenuField->MenuItem()->SetLabel(
			B_TRANSLATE("Choose target volume"));
	}
}


void
InstallerWindow::_UpdateControls()
{
	BString err;
	fInstallButton->SetEnabled(_ValidateSetup(err));
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


BString
InstallerWindow::_ComposeSetupConf()
{
	BString conf;
	const char* full = fFullNameField->Text();
	const char* host = fHostnameField->Text();
	const char* user = fUserField->Text();

	conf << "username="  << user << "\n";
	conf << "hostname="  << host << "\n";
	if (full != NULL && *full != '\0')
		conf << "full_name=" << full << "\n";
	conf << "sudo_enabled=" <<
		(fSudoCheck->Value() == B_CONTROL_ON ? "1" : "0") << "\n";
	conf << "autologin=" <<
		(fAutologinCheck->Value() == B_CONTROL_ON ? "1" : "0") << "\n";

	// Helper hashes under root; conf goes via stdin, never to disk.
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
