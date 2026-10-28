/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MESSAGES_WINDOW_H
#define _MESSAGES_WINDOW_H


#include <Window.h>
#include <String.h>


class BButton;
class BListView;
class BTextControl;
class MessageStore;


enum {
	kMsgThreadSelected	= 'msTS',
	kMsgSend			= 'msSD',
	kMsgSendReply		= 'msSR',
	kMsgDeleteThread	= 'msDT',
	kMsgStoreChanged	= 'msCH'
};


// SMS browser: thread list per number on the left, conversation and
// compose line on the right.
class MessagesWindow : public BWindow {
public:
								MessagesWindow(const BRect& frame,
									MessageStore* store);

	virtual	void				MessageReceived(BMessage* message);

			void				Refresh();

private:
			void				_Send();
			void				_DeleteThread();
			void				_SelectionChanged();
			const char*			_SelectedNumber();

			MessageStore*		fStore;
			BListView*			fThreadList;
			BListView*			fConversationList;
			BTextControl*		fNumberControl;
			BTextControl*		fComposeControl;
};


#endif // _MESSAGES_WINDOW_H
