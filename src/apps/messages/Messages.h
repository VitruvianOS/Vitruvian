/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MESSAGES_APP_H
#define _MESSAGES_APP_H


#include <Application.h>
#include <Messenger.h>
#include <String.h>


class MessageStore;
class MessagesWindow;


// SMS client: threads per number, compose, delivery state and a
// BNotification whenever a message arrives from the network.
class Messages : public BApplication {
public:
								Messages();

	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_OnSmsAdded(BMessage* message);
			void				_OnSmsStateChanged(BMessage* message);
			void				_NotifyIncoming(const char* number,
									const char* text);

			MessageStore*		fStore;
			MessagesWindow*		fWindow;
			BMessenger			fSelfMessenger;
};


#endif // _MESSAGES_APP_H
