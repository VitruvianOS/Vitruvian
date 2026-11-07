/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _PHONE_APP_H
#define _PHONE_APP_H


#include <Application.h>

#include <Messenger.h>


class BTelephonyRoster;
class CallLog;
class DialerWindow;


// Phone application: dialer, in-call window and the full-screen
// incoming-call window. One BModem (the default modem) backs every
// operation; notifications arrive on the app's looper.
class Phone : public BApplication {
public:
								Phone();

	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);

			CallLog*			CallLogInstance() { return fCallLog; }

private:
			void				_OnRosterNotification(BMessage* message);
			void				_ShowIncomingCall(BMessage* message);
			void				_ShowInCall();
			void				_CloseInCall();
			void				_RecordCallOutcome(uint32 state);

			BTelephonyRoster*	fRoster;
			CallLog*			fCallLog;
			DialerWindow*		fDialerWindow;
			class BWindow*		fInCallWindow;
			class BWindow*		fIncomingWindow;
			BMessenger			fSelfMessenger;
			uint32				fLastCallState;
			BString				fLastCallNumber;
			uint32				fLastCallDirection;
};


#endif // _PHONE_APP_H
