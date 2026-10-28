/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _INCOMING_CALL_WINDOW_H
#define _INCOMING_CALL_WINDOW_H


#include <Window.h>
#include <String.h>


class BButton;
class BStringView;


// Full-screen incoming call UI for the mobile shell: one big number,
// Answer and Decline. Deliberately not closable from the title bar so
// the call cannot be lost to a stray click.
class IncomingCallWindow : public BWindow {
public:
								IncomingCallWindow(const char* number);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Answer();
			void				_Decline();

			BString				fNumber;
			BStringView*		fNumberLabel;
};


#endif // _INCOMING_CALL_WINDOW_H
