/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef _RECEIVE_PROMPT_WINDOW_H
#define _RECEIVE_PROMPT_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <StringList.h>
#include <Window.h>

// Modal-ish prompt shown when a peer wants to send files. Three ways
// out: accept once, decline, or accept and always trust this device.
// Answers are forwarded to |answerTarget| (the main window).
class ReceivePromptWindow : public BWindow {
public:
						ReceivePromptWindow(BRect frame,
							const BString& senderAlias,
							const BStringList& files,
							const BMessenger& answerTarget);

	virtual	void		MessageReceived(BMessage* message);
	virtual	bool		QuitRequested();

private:
			void		_Answer(bool accept, bool always);

			BString		fSenderAlias;
			BMessenger	fAnswerTarget;
};

#endif // _RECEIVE_PROMPT_WINDOW_H
