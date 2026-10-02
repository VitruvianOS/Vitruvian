/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINTERS_WINDOW_H
#define _PRINTERS_WINDOW_H


#include <ColumnListView.h>
#include <String.h>
#include <Window.h>

#include "printcups.h"


class BButton;
class BColumnListView;
class BStringView;


class PrinterRow : public BRow {
public:
						PrinterRow(const printcups_queue_info& info);

	const BString&		Name() const { return fName; }
	bool				IsDefault() const { return fDefault; }

private:
	static const char*	StatusText(const printcups_queue_info& info);

	BString				fName;
	bool				fDefault;
};


class JobRow : public BRow {
public:
						JobRow(const printcups_job& job);

	int32				Id() const { return fId; }
	const BString&		Queue() const { return fQueue; }

private:
	static const char*	StateText(int32 state);

	int32				fId;
	BString				fQueue;
};


class PrintersWindow : public BWindow {
public:
						PrintersWindow();

	virtual void		MessageReceived(BMessage* message);

private:
	void				_SetupLists();
	void				_UpdateQueues();
	void				_UpdateJobs();
	void				_UpdateButtons();
	void				_SetStatus(const char* text);
	void				_SetError(const BString& text, status_t status);

	PrinterRow*			_SelectedPrinter() const;
	BString				_SelectedQueue() const;

	void				_LoadQueues();
	void				_LoadJobs();
	void				_DoDefault();
	void				_DoRemove();
	void				_DoRename();
	void				_DoSetAccepting(bool accepting);
	void				_DoTestPage();
	void				_DoOptions();
	void				_DoAddPrinter();
	void				_DoCancelJob();
	void				_DoHoldJob();
	void				_DoReleaseJob();
	void				_DoPurgeJobs();

	BColumnListView*	fPrinterList;
	BColumnListView*	fJobList;
	BStringView*		fStatusView;
	BButton*			fAddButton;
	BButton*			fRemoveButton;
	BButton*			fDefaultButton;
	BButton*			fRenameButton;
	BButton*			fEnableButton;
	BButton*			fDisableButton;
	BButton*			fTestPageButton;
	BButton*			fOptionsButton;
	BButton*			fRefreshButton;
	BButton*			fCancelButton;
	BButton*			fHoldButton;
	BButton*			fReleaseButton;
	BButton*			fPurgeButton;
	bool				fBusy;
};


#endif // _PRINTERS_WINDOW_H
