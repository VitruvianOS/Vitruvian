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
class BMenuField;
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
								InstallerWindow();
	virtual						~InstallerWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_ScanPartitions();
			void				_UpdateControls();
			void				_UpdateAdvancedEnabled();
			void				_UpdatePasswordStatus();
			bool				_ValidateSetup(BString& errorOut);
			BString				_ComposeSetupConf();
			void				_SetCopyEngineCancelSemaphore(sem_id id,
									bool alreadyLocked = false);
			void				_QuitCopyEngine(bool askUser);

			BMenu*				fDestMenu;
			BMenuField*			fDestMenuField;

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

			InstallStatus		fInstallStatus;

			WorkerThread*		fWorkerThread;
			sem_id				fCopyEngineCancelSemaphore;
};


#endif // INSTALLER_WINDOW_H
