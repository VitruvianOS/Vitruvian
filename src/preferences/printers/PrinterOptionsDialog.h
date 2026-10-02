/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINTER_OPTIONS_DIALOG_H
#define _PRINTER_OPTIONS_DIALOG_H


#include <List.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include "printcups.h"


class BButton;
class BPopUpMenu;
class BTextControl;


class PrinterOptionsDialog : public BWindow {
public:
						PrinterOptionsDialog(const BMessenger& replyTo,
							const BString& queue,
							const printcups_queue_info& info,
							bool renameMode);

	virtual void		MessageReceived(BMessage* message);

private:
	void				_SetupRename();
	void				_SetupOptions();
	void				_LoadChoices(const char* option, BPopUpMenu* menu,
							const char* current);
	BString				_MarkedValue(BPopUpMenu* menu, BList& values);
	void				_Save();
	void				_UpdateBusy(bool busy);

	BMessenger			fReplyTo;
	BString				fQueue;
	printcups_queue_info fInfo;
	bool				fRenameMode;
	bool				fBusy;
	BTextControl*		fDescriptionField;
	BTextControl*		fLocationField;
	BPopUpMenu*			fMediaMenu;
	BPopUpMenu*			fSidesMenu;
	BPopUpMenu*			fColorMenu;
	BPopUpMenu*			fQualityMenu;
	BList				fMediaValues;		// BString* per menu item
	BList				fSidesValues;
	BList				fColorValues;
	BList				fQualityValues;
	BButton*			fSaveButton;
	BButton*			fCancelButton;
};


#endif // _PRINTER_OPTIONS_DIALOG_H
