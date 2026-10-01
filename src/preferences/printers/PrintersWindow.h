/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINTERS_WINDOW_H
#define _PRINTERS_WINDOW_H


#include <String.h>
#include <Window.h>


class BButton;
class BListView;
class BTextControl;


class PrintersWindow : public BWindow {
public:
						PrintersWindow();

	virtual void		MessageReceived(BMessage* message);

private:
	void				_UpdateQueues();
	void				_UpdateJobs();
	void				_UpdateButtons();
	void				_SetDefault();
	void				_CancelJob();
	void				_AddPrinter();
	void				_RemovePrinter();
	void				_ShowError(const char* text, status_t status);

	BString				_SelectedQueue() const;

	BListView*			fQueueList;
	BListView*			fJobList;
	BTextControl*		fAddNameField;
	BTextControl*		fAddUriField;
	BButton*			fDefaultButton;
	BButton*			fRemoveButton;
	BButton*			fCancelButton;
	BButton*			fAddButton;
};


#endif // _PRINTERS_WINDOW_H
