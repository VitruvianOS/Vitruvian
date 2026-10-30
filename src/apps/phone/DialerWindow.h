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
class BMenuBar;
class BPopUpMenu;
class BStringView;
class BTextControl;
class Phone;
class PhoneReplicantView;


enum {
	kMsgDial			= 'phDL',
	kMsgDialReply		= 'phDR',
	kMsgHangUpAll		= 'phHU',
	kMsgOpReply			= 'phOP',
	kMsgKeypadKey		= 'phKY',
	kMsgLineSelected	= 'phLN'
};


// Keypad plus call log and a line chooser; modem and SIP lines both
// appear there and calls land in the in-call window owned by the
// Phone app as soon as the line reports the call active.
class DialerWindow : public BWindow {
public:
								DialerWindow(const BRect& frame);

	virtual	void				MessageReceived(BMessage* message);

			void				AppendKeypadKey(char key);
			void				RefreshLines();

			// Path of the line the dialer will use; empty means
			// "first line in the roster".
			const char*			SelectedLine();

private:
			void				_Dial();
			void				_HangUpAll();
			void				_UpdateStatus();

			BTextControl*		fNumberControl;
			BStringView*		fStatusView;
			PhoneReplicantView*	fReplicantView;
			BListView*			fLogView;
			BButton*			fDialButton;
			BButton*			fHangUpButton;
			BPopUpMenu*			fLineMenu;
};


#endif // _DIALER_WINDOW_H
