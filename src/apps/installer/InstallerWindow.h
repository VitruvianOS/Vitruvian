/*
 * Copyright 2009-2010, Stephan Aßmus <superstippi@gmx.de>
 * Copyright 2005, Jérôme DUVAL
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef INSTALLER_WINDOW_H
#define INSTALLER_WINDOW_H


#include <String.h>
#include <Window.h>


class BBox;
class BButton;
class BCheckBox;
class BMenu;
class BMenuBar;
class BMenuField;
class BMenuItem;
class BPopUpMenu;
class BStatusBar;
class BStringView;
class BTextControl;
class BTextView;
class WorkerThread;


enum InstallStatus {
	kReadyForInstall,
	kInstalling,
	kFinished
};


class InstallerWindow : public BWindow {
public:
								InstallerWindow(bool setupMode = false);
	virtual						~InstallerWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_ScanPartitions();
			void				_UpdateControls();
			void				_FinishSetup();
			void				_UpdateAdvancedEnabled();
			void				_UpdatePasswordStatus();
			bool				_ValidateSetup(BString& errorOut);
			BString				_ComposeSetupConf();
			void				_SetCopyEngineCancelSemaphore(sem_id id,
									bool alreadyLocked = false);
			void				_QuitCopyEngine(bool askUser);
			void				_LaunchDriveSetup();
			void				_RebuildBootloaderMenu();
			void				_ClearPlan();
			BString				_ComposeSummary(const char* targetLabel,
									bool inPlace);
			void				_ReadBootloaderChoice();
			void				_ReadSwapChoice();
			void				_UpdateSwapControls();
			int64				_SwapSizeMiB() const;
			bool				_SwapSizeOk() const;

			BMenu*				fDestMenu;
			BMenuField*			fDestMenuField;

			BMenuBar*			fMenuBar;
			BMenu*				fToolsMenu;
			BMenu*				fBootloaderMenu;
			BMenuItem*			fBootloaderSetupItem;
			BPopUpMenu*			fBootloaderChoiceMenu;
			BMenuField*			fBootloaderField;
			BPopUpMenu*			fSwapChoiceMenu;
			BMenuField*			fSwapField;
			BTextControl*		fSwapSizeField;
			BStringView*		fSwapNote;
			BView*				fTargetAdvancedView;
			BCheckBox*			fNvramCheck;
			BCheckBox*			fKeepSshCheck;
			BButton*			fLaunchDriveSetupButton;
			BButton*			fTargetSetupButton;

			BTextControl*		fHostnameField;
			BTextControl*		fFullNameField;
			BTextControl*		fUserField;
			BTextControl*		fPasswordField;
			BTextControl*		fConfirmField;
			BCheckBox*			fSudoCheck;
			BCheckBox*			fAutologinCheck;
			BStringView*		fAutologinNote;
			BCheckBox*			fAdvancedCheck;
			BTextControl*		fRootPasswordField;
			BTextControl*		fRootConfirmField;
			BStringView*		fPasswordStatus;

			BStatusBar*			fProgressBar;
			BButton*			fInstallButton;
			BButton*			fQuitButton;

			bool				fSetupMode;
			InstallStatus		fInstallStatus;

			WorkerThread*		fWorkerThread;
			sem_id				fCopyEngineCancelSemaphore;

			// Partition plan built by the target-setup dialog; applied
			// by the helper only after the final confirmation.
			BString				fPlanText;
			BString				fBootloaderChoice;
			bool				fBootloaderExplicit;
			// "part:<devnode>", "plan", "file" or "none".
			BString				fSwapChoice;
			bool				fSwapExplicit;
			BString				fSwapFileSizeText;
			BString				fPlanSwapHint;
			int64				fPlanSwapHintMiB;
			int64				fPlanSwapMiB;
};


#endif // INSTALLER_WINDOW_H
