/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Phone.h"

#include <stdio.h>
#include <stdlib.h>

#include <Roster.h>
#include <String.h>

#include "CallLog.h"
#include "DialerWindow.h"
#include "InCallWindow.h"
#include "IncomingCallWindow.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


Phone::Phone()
	:
	BApplication("application/x-vnd.vos-Phone"),
	fRoster(NULL),
	fCallLog(NULL),
	fDialerWindow(NULL),
	fInCallWindow(NULL),
	fIncomingWindow(NULL),
	fSelfMessenger(this),
	fLastCallState(B_TELEPHONY_CALL_UNKNOWN),
	fLastCallDirection(B_TELEPHONY_DIRECTION_UNKNOWN)
{
	fCallLog = new CallLog();
	fRoster = BTelephonyRoster::Default();
}


void
Phone::ReadyToRun()
{
	fRoster->StartWatching(fSelfMessenger, kTelephonyWatchAll);

	const char* modemPath = fRoster->DefaultModemPath();
	if (modemPath != NULL) {
		// Sample signal so the status lines stay alive during calls.
		fRoster->SetupSignalRate(modemPath, 5, fSelfMessenger, 'phSR');
	}

	fDialerWindow = new DialerWindow(BRect(50, 50, 330, 620));
	fDialerWindow->Show();
}


void
Phone::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kTelephonyMsgCallAdded:
		case kTelephonyMsgCallStateChanged:
		case kTelephonyMsgSmsAdded:
			_OnRosterNotification(message);
			break;
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
Phone::_OnRosterNotification(BMessage* message)
{
	BModem* modem = NULL;
	const char* modemPath = NULL;
	if (message->FindString(kTelephonyFieldModemPath, &modemPath) == B_OK)
		modem = fRoster->ModemForPath(modemPath);
	if (modem == NULL)
		modem = fRoster->ModemAt(0);
	if (modem == NULL)
		return;

	switch (message->what) {
		case kTelephonyMsgCallAdded:
		case kTelephonyMsgCallStateChanged:
		{
			uint32 state = modem->CallState();
			if (state == B_TELEPHONY_CALL_RINGING_IN)
				_ShowIncomingCall(message);
			else if (state == B_TELEPHONY_CALL_ACTIVE)
				_ShowInCall();
			else if (state == B_TELEPHONY_CALL_TERMINATED) {
				_RecordCallOutcome(state);
				_CloseInCall();
				if (fIncomingWindow != NULL) {
					fIncomingWindow->PostMessage(B_QUIT_REQUESTED);
					fIncomingWindow = NULL;
				}
			} else if (state == B_TELEPHONY_CALL_DIALING
					|| state == B_TELEPHONY_CALL_RINGING_OUT)
				_ShowInCall();

			fLastCallState = state;
			BString number(modem->CallNumber());
			if (!number.IsEmpty())
				fLastCallNumber = number;
			fLastCallDirection = modem->CallDirection();
			break;
		}
		case kTelephonyMsgSmsAdded:
			// Messages are the Messages app's job; the Phone app only
			// needs to know a call came in.
			break;
		default:
			break;
	}

	if (fDialerWindow != NULL)
		fDialerWindow->PostMessage(message);
}


void
Phone::_ShowIncomingCall(BMessage* message)
{
	if (fIncomingWindow != NULL) {
		fIncomingWindow->Activate();
		return;
	}

	BString number;
	message->FindString(kTelephonyFieldCallNumber, &number);

	fIncomingWindow = new IncomingCallWindow(number.String());
	fIncomingWindow->Show();
}


void
Phone::_ShowInCall()
{
	if (fIncomingWindow != NULL) {
		fIncomingWindow->PostMessage(B_QUIT_REQUESTED);
		fIncomingWindow = NULL;
	}

	if (fInCallWindow != NULL) {
		fInCallWindow->Activate();
		return;
	}

	BModem* modem = fRoster->ModemAt(0);
	BString number;
	if (modem != NULL)
		number = modem->CallNumber();

	fInCallWindow = new InCallWindow(number.String());
	fInCallWindow->Show();
}


void
Phone::_CloseInCall()
{
	if (fInCallWindow != NULL) {
		fInCallWindow->PostMessage(B_QUIT_REQUESTED);
		fInCallWindow = NULL;
	}
}


void
Phone::_RecordCallOutcome(uint32 state)
{
	(void)state;
	if (fLastCallNumber.IsEmpty())
		return;

	CallLogEntry entry;
	entry.number = fLastCallNumber;
	entry.direction = fLastCallDirection == B_TELEPHONY_DIRECTION_INCOMING
		? "in" : "out";
	entry.timestamp = time(NULL);
	// Terminated while still ringing in means the far side never
	// answered: that is a missed call.
	entry.result = fLastCallState == B_TELEPHONY_CALL_RINGING_IN
		? "missed" : "completed";

	fCallLog->Add(entry);
	fLastCallNumber = "";
	fLastCallState = B_TELEPHONY_CALL_UNKNOWN;
}


int
main()
{
	Phone* app = new Phone();
	app->Run();
	delete app;
	return 0;
}
