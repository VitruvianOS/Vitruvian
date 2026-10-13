/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef TARGET_SETUP_WINDOW_H
#define TARGET_SETUP_WINDOW_H


#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BButton;
class BCheckBox;
class BMenuField;
class BPopUpMenu;
class BTextControl;
class BTextView;


// Builds a partition plan for the Installer; it is applied only after the
// install confirmation.
class TargetSetupWindow : public BWindow {
public:
								TargetSetupWindow(const BMessenger& notify);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_ScanDisks();
			void				_ScanExistingPartitions();
			void				_UpdateControls();
			bool				_BuildPlan(BString& planOut,
									BString& errorOut);
			void				_Apply();

			BMessenger			fNotify;
			BPopUpMenu*			fDiskMenu;
			BMenuField*			fDiskField;
			BPopUpMenu*			fModeMenu;
			BMenuField*			fModeField;
			BPopUpMenu*			fFilesystemMenu;
			BMenuField*			fFilesystemField;
			BPopUpMenu*			fTableMenu;
			BMenuField*			fTableField;
			BPopUpMenu*			fExistingMenu;
			BMenuField*			fExistingField;
			BTextControl*		fEspSizeField;
			BCheckBox*			fBiosBootCheck;
			BTextView*			fPreview;
			BButton*			fApplyButton;
			BButton*			fCloseButton;

			// Disk path for the "existing partition" mode's target.
			BString				fExistingDiskPath;
};


#endif // TARGET_SETUP_WINDOW_H
