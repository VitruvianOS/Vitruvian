/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "IncomingCallWindow.h"

#include <Button.h>
#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "IncomingCallWindow"
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Messenger.h>
#include <Screen.h>
#include <StringView.h>

#include "DialerWindow.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


IncomingCallWindow::IncomingCallWindow(const char* number)
	:
	BWindow(BScreen(B_MAIN_SCREEN_ID).Frame(), B_TRANSLATE("Incoming call"),
		B_TITLED_WINDOW, B_NOT_MOVABLE | B_NOT_CLOSABLE
			| B_NOT_MINIMIZABLE | B_NOT_ZOOMABLE
			| B_AVOID_FOCUS | B_WILL_ACCEPT_FIRST_CLICK),
	fNumber(number != NULL ? number : "")
{
	BStringView* titleLabel = new BStringView("title",
		B_TRANSLATE("Incoming call"));
	titleLabel->SetFont(be_bold_font);

	fNumberLabel = new BStringView("number", fNumber.String());
	fNumberLabel->SetFont(be_bold_font);

	BButton* answerButton = new BButton("answer", B_TRANSLATE("Answer"),
		new BMessage('phAN'));
	answerButton->SetExplicitMinSize(BSize(B_SIZE_UNSET, 72));

	BButton* declineButton = new BButton("decline", B_TRANSLATE("Decline"),
		new BMessage('phDC'));
	declineButton->SetExplicitMinSize(BSize(B_SIZE_UNSET, 72));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_BIG_INSETS)
		.AddGlue()
		.Add(titleLabel)
		.Add(fNumberLabel)
		.AddGlue()
		.AddGrid()
			.Add(answerButton, 0, 0)
			.Add(declineButton, 1, 0)
		.End()
		.AddGlue();
}


void
IncomingCallWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case 'phAN':
			_Answer();
			break;
		case 'phDC':
			_Decline();
			break;
		case kTelephonyMsgCallStateChanged:
		case kTelephonyMsgCallRemoved:
		{
			// The call went away under us (remote hangup): leave.
			BTelephonyRoster* roster = BTelephonyRoster::Default();
			BModem* modem = roster->ModemAt(0);
			if (modem != NULL) {
				uint32 state = modem->CallState();
				if (state == B_TELEPHONY_CALL_UNKNOWN
						|| state == B_TELEPHONY_CALL_TERMINATED
						|| state == B_TELEPHONY_CALL_ACTIVE) {
					PostMessage(B_QUIT_REQUESTED);
					break;
				}
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
IncomingCallWindow::_Answer()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem != NULL)
		modem->Answer(BMessenger(this), kMsgOpReply);

	// The Phone app swaps in the in-call window when the call turns
	// active; this window closes on that same notification.
}


void
IncomingCallWindow::_Decline()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem != NULL)
		modem->HangUp(BMessenger(this), kMsgOpReply);
	PostMessage(B_QUIT_REQUESTED);
}
