/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRINTERS_WINDOW_H
#define _PRINTERS_WINDOW_H


#include <Window.h>


class BButton;
class BListView;
class BStringItem;
class BTextControl;


class PrintersWindow : public BWindow {
public:
						PrintersWindow();

	virtual void		MessageReceived(BMessage* message);
	virtual bool		QuitRequested();

private:
	void				_UpdateQueues();
	void				_UpdateJobs();
	void				_SetDefault();
	void				_CancelJob();
	void				_AddPrinter();
	void				_RemovePrinter();

	class BStringItem*	_SelectedQueue() const;
	BString				_SelectedJobName() const;
	int32				_SelectedJobId() const;

	BListView*		fQueueList;
	BListView*		fJobList;
	BTextControl*	fAddNameField;
	BTextControl*	fAddUriField;
	BButton*		fDefaultButton;
	BButton*		fCancelButton;
	BButton*		fAddButton;
	BButton*		fRemoveButton;
};


#endif // _PRINTERS_WINDOW_H
