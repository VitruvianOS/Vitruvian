/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef SERVICES_WINDOW_H
#define SERVICES_WINDOW_H

#include <ObjectList.h>
#include <String.h>
#include <Window.h>

#include "ServicesDefs.h"
#include "ServicesUnit.h"


class BButton;
class BCheckBox;
class BMenuBar;
class BMenuItem;
class BPopUpMenu;
class BRadioButton;
class BStringView;
class BTextControl;
class BTextView;
class ServicesList;
class ServicesWorker;


class ServicesWindow : public BWindow {
	typedef BWindow Inherited;
public:
								ServicesWindow(BRect frame);
	virtual						~ServicesWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_BuildMenuBar();
			void				_BuildLayout();

			void				_SwitchManager();
			void				_ApplyFilter();
			void				_RebuildList();
			void				_ShowSelectedDetail();
			void				_PostUnitAction(uint32 what);
			void				_SetStatus(const char* text);
			void				_ShowError(BMessage* message);
			void				_HandleUnitsBatch(BMessage* message);
			void				_HandleUnitsReady(BMessage* message);
			void				_HandleUnitUpdated(BMessage* message);
			void				_HandleUnitRemoved(BMessage* message);
			void				_HandleJournal(BMessage* message);
			void				_HandleActionDone(BMessage* message);
			void				_UpdateActionButtons();

			BMenuBar*			fMenuBar;
			BMenuItem*			fSystemItem;
			BMenuItem*			fUserItem;
			BMenuItem*			fRefreshItem;

			BRadioButton*		fSystemRadio;
			BRadioButton*		fUserRadio;
			BCheckBox*			fAllUnitsCheck;
			BTextControl*		fFilterControl;

			BButton*			fStartButton;
			BButton*			fStopButton;
			BButton*			fRestartButton;
			BButton*			fEnableButton;
			BButton*			fDisableButton;

			ServicesList*		fList;
			BStringView*		fDetailTitle;
			BStringView*		fDetailState;
			BTextView*			fJournalView;
			BStringView*		fStatusLabel;

			ServicesWorker*		fWorker;
			// Owning: the window is the model owner.
			BObjectList<ServicesUnit, true>	fUnits;
			BString				fFilterText;
			unit_filter			fUnitFilter;
			bool				fUsesUser;
			bool				fLoading;
};


#endif // SERVICES_WINDOW_H
