/*
 * Copyright 2024, Vitruvian OS.
 * Based on Haiku Installer, Copyright 2005-2010, various authors.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef INSTALLER_WINDOW_H
#define INSTALLER_WINDOW_H


#include <String.h>
#include <Window.h>


class BButton;
class BMenu;
class BMenuField;
class BMenuItem;
class BStatusBar;
class BStringView;
class BTextView;
class WorkerThread;

enum InstallStatus {
	kReadyForInstall,
	kInstalling,
	kFinished,
	kCancelled
};


class InstallerWindow : public BWindow {
public:
								InstallerWindow();
	virtual						~InstallerWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
private:
			void				_LaunchDriveSetup();
			void				_DisableInterface(bool disable);
			void				_ScanPartitions();
			void				_UpdateControls();
			void				_SetStatusMessage(const char* text);

			void				_SetCopyEngineCancelSemaphore(sem_id id,
									bool alreadyLocked = false);
			void				_QuitCopyEngine(bool askUser);

			BTextView*			fStatusView;
			BMenu*				fSrcMenu;
			BMenu*				fDestMenu;
			BMenuField*			fSrcMenuField;
			BMenuField*			fDestMenuField;

			BStatusBar*			fProgressBar;

			BButton*			fBeginButton;
			BButton*			fLaunchDriveSetupButton;

			bool				fDriveSetupLaunched;
			InstallStatus		fInstallStatus;

			WorkerThread*		fWorkerThread;
			BString				fLastStatus;
			sem_id				fCopyEngineCancelSemaphore;
};


#endif // INSTALLER_WINDOW_H
