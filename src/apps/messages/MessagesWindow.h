/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MESSAGES_WINDOW_H
#define _MESSAGES_WINDOW_H


#include <Window.h>

#include "MessagesView.h"


class MessagesWindow : public BWindow {
public:
								MessagesWindow(const BRect& frame,
									MessageStore* store);

	virtual	bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

			void				Refresh();

private:
			MessagesView*		fMessagesView;
};


#endif // _MESSAGES_WINDOW_H
