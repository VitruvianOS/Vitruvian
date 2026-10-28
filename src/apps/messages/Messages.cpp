/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Messages.h"

#include <Notification.h>
#include <Roster.h>

#include <stdio.h>

#include "MessageStore.h"
#include "MessagesWindow.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Messages"


Messages::Messages()
	:
	BApplication("application/x-vnd.vos-Messages"),
	fStore(NULL),
	fWindow(NULL),
	fSelfMessenger(this)
{
	fStore = new MessageStore();
}


void
Messages::ReadyToRun()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	roster->StartWatching(fSelfMessenger,
		kTelephonyWatchMessages | kTelephonyWatchModems);

	fWindow = new MessagesWindow(BRect(60, 60, 640, 480), fStore);
	fWindow->Show();
}


void
Messages::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kTelephonyMsgSmsAdded:
			_OnSmsAdded(message);
			break;
		case kTelephonyMsgSmsStateChanged:
			_OnSmsStateChanged(message);
			break;
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


void
Messages::_OnSmsAdded(BMessage* message)
{
	bool received = false;
	message->FindBool(kTelephonyFieldSmsReceived, &received);
	if (!received)
		return;

	BString number;
	message->FindString(kTelephonyFieldSmsNumber, &number);
	BString text;
	message->FindString(kTelephonyFieldSmsText, &text);

	// A multipart receive reports empty text until the last part
	// lands; store the placeholder then let the state change rewrite
	// nothing (the final text arrives with the RECEIVED state).
	fStore->AddIncoming(number.String(),
		text.IsEmpty() ? "..." : text.String());

	_NotifyIncoming(number.String(), text.String());

	if (fWindow != NULL)
		fWindow->PostMessage(kMsgStoreChanged);
}


void
Messages::_OnSmsStateChanged(BMessage* message)
{
	bool received = false;
	message->FindBool(kTelephonyFieldSmsReceived, &received);
	if (received)
		return;

	BString number;
	message->FindString(kTelephonyFieldSmsNumber, &number);

	uint32 state = 0;
	message->FindUInt32(kTelephonyFieldSmsState, &state);

	const char* name = "sending";
	if (state == B_TELEPHONY_SMS_SENT)
		name = "sent";
	else if (state == B_TELEPHONY_SMS_UNKNOWN)
		name = "failed";

	// Multipart sends land in SENDING first; only terminal states
	// rewrite the stored entry.
	if (state == B_TELEPHONY_SMS_SENT || state == B_TELEPHONY_SMS_UNKNOWN)
		fStore->UpdateLastOutgoingState(number.String(), name);

	if (fWindow != NULL)
		fWindow->PostMessage(kMsgStoreChanged);
}


void
Messages::_NotifyIncoming(const char* number, const char* text)
{
	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup(BString("Messages"));
	BString title("SMS from ");
	title << (number != NULL ? number : "");
	notification.SetTitle(title);
	notification.SetContent(BString(text != NULL ? text : ""));
	notification.SetOnClickApp("application/x-vnd.vos-Messages");
	notification.Send();
}


int
main()
{
	Messages* app = new Messages();
	app->Run();
	delete app;
	return 0;
}
