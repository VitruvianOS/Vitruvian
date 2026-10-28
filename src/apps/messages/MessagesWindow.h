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
class BPopUpMenu;
class BTextControl;
class MessageStore;


enum {
	kMsgThreadSelected	= 'msTS',
	kMsgSend			= 'msSD',
	kMsgSendReply		= 'msSR',
	kMsgDeleteThread	= 'msDT',
	kMsgStoreChanged	= 'msCH',
	kMsgLinesChanged	= 'msLC',
	kMsgLineSelected	= 'msLN'
};


// SMS/SIP-message browser: thread list per number on the left,
// conversation and compose line on the right. The line chooser picks
// which modem or SIP account carries the outgoing message.
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
			void				_RefreshLines();
			const char*			_SelectedLine();

			MessageStore*		fStore;
			BListView*			fThreadList;
			BListView*			fConversationList;
			BTextControl*		fNumberControl;
			BTextControl*		fComposeControl;
			BPopUpMenu*			fLineMenu;
};


#endif // _MESSAGES_WINDOW_H
