/*
 * Copyright 2026, Angelo Scarnà. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef CHANGE_SUMMARY_WINDOW_H
#define CHANGE_SUMMARY_WINDOW_H

#include <Messenger.h>
#include <Window.h>


class BMessage;
class BOutlineListView;


// Modal review dialog shown after apt resolves a transaction. It lays the
// pulled-in packages out as a Synaptic-style tree (grouped by verb) instead
// of dumping apt's prose into a BAlert. It never touches the package system
// itself: on "Apply" it posts kMsgSummaryApply back to its target and lets
// MainWindow drive the real transaction; on "Cancel"/close it posts
// kMsgSummaryCancel.
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
