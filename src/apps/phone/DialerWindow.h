/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _DIALER_WINDOW_H
#define _DIALER_WINDOW_H


#include <Window.h>
#include <String.h>


class BButton;
class BListView;
class BStringView;
class BTextControl;
class Phone;


enum {
	kMsgDial			= 'phDL',
	kMsgDialReply		= 'phDR',
	kMsgHangUpAll		= 'phHU',
	kMsgOpReply			= 'phOP',
	kMsgKeypadKey		= 'phKY'
};


// Keypad plus call log; calls land in the in-call window owned by the
// Phone app as soon as the modem reports the call active.
class DialerWindow : public BWindow {
public:
								DialerWindow(const BRect& frame);

	virtual	void				MessageReceived(BMessage* message);

			void				AppendKeypadKey(char key);

private:
			void				_Dial();
			void				_HangUpAll();
			void				_UpdateStatus();

			BTextControl*		fNumberControl;
			BStringView*		fStatusView;
			BListView*			fLogView;
			BButton*			fDialButton;
			BButton*			fHangUpButton;
};


#endif // _DIALER_WINDOW_H
