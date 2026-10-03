/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHANGE_SUMMARY_WINDOW_H
#define CHANGE_SUMMARY_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BMessage;
class BOutlineListView;


// Modal list of the resolved transaction; posts kMsgSummaryApply or
// kMsgSummaryCancel to its target and never touches apt itself.
class ChangeSummaryWindow : public BWindow {
	typedef BWindow Inherited;
public:
								ChangeSummaryWindow(BWindow* parent,
									const BMessenger& target,
									const BMessage* details,
									const char* summary);
	virtual						~ChangeSummaryWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			int32				_AddGroup(const BMessage* details,
									const char* field, const char* label);
	static	BString				_FindSummaryLine(const char* summary,
									const char* needle);

			BMessenger			fTarget;
			BOutlineListView*	fListView;
			bool				fConfirmed;
};


#endif // CHANGE_SUMMARY_WINDOW_H
