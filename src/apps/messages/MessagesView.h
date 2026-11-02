/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MESSAGES_VIEW_H
#define _MESSAGES_VIEW_H


#include <View.h>


class BButton;
class BListView;
class BPopUpMenu;
class BStringView;
class BTelephonyRoster;
class BTextControl;
class MessageStore;


enum {
	kMsgThreadSelected	= 'msTS',
	kMsgSend			= 'msSD',
	kMsgSendReply		= 'msSR',
	kMsgDeleteThread	= 'msDT',
	kMsgDeleteConfirmed	= 'msDC',
	kMsgStoreChanged	= 'msCH',
	kMsgLinesChanged	= 'msLC',
	kMsgLineSelected	= 'msLN',
	kMsgMessagesMenu	= 'msMN',
	kMsgNewMessage		= 'msNM'
};


class MessagesView : public BView {
public:
								MessagesView(const char* name,
									MessageStore* store);
								MessagesView(BMessage* archive);

	virtual						~MessagesView();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				Refresh();
			void				RefreshLines();

private:
			void				_Build();
			void				_Send();
			void				_NewMessage();
			void				_ConfirmDelete();
			void				_DeleteThread();
			void				_SelectionChanged();
			BString				_SelectedNumber();
			const char*			_SelectedLine();
			void				_ShowMenu();
			bool				_IsReplicated() const;
			void				_OnSmsAdded(BMessage* message);
			void				_OnSmsStateChanged(BMessage* message);

	static const char*			kAppSignature;

			BTelephonyRoster*	fRoster;
			MessageStore*		fStore;
			bool				fOwnsStore;
			BStringView*		fStatusView;
			BButton*			fMenuButton;
			BListView*			fThreadList;
			BListView*			fConversationList;
			BTextControl*		fNumberControl;
			BTextControl*		fComposeControl;
			BButton*			fSendButton;
			BPopUpMenu*			fLineMenu;
};


#endif // _MESSAGES_VIEW_H
