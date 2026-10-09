/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ADD_PRINTER_DIALOG_H
#define _ADD_PRINTER_DIALOG_H


#include <ColumnListView.h>
#include <List.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include "printcups.h"


class BButton;
class BPopUpMenu;
class BRadioButton;
class BTextControl;


class DeviceRow : public BRow {
public:
						DeviceRow(const printcups_device& device);

	const BString&		Uri() const { return fUri; }
	bool				Everywhere() const { return fEverywhere; }

private:
	BString				fUri;
	bool				fEverywhere;
};


class PpdRow : public BRow {
public:
						PpdRow(const printcups_ppd& ppd);

	const BString&		Name() const { return fName; }

private:
	BString				fName;
};


class AddPrinterDialog : public BWindow {
public:
						AddPrinterDialog(const BMessenger& replyTo);

	virtual void		MessageReceived(BMessage* message);

private:
	void				_UpdateButtons();
	void				_LoadPpds(const BString& uri);
	BString				_SelectedModel();

	BMessenger			fReplyTo;
	BRadioButton*		fDiscoverRadio;
	BRadioButton*		fManualRadio;
	BColumnListView*	fDeviceList;
	BTextControl*		fUriField;
	BTextControl*		fNameField;
	BPopUpMenu*			fModelMenu;
	BList				fPpdNames;			// BString* per menu item
	BButton*			fRefreshButton;
	BButton*			fAddButton;
	BButton*			fCancelButton;
	bool				fBusy;
};


#endif // _ADD_PRINTER_DIALOG_H
