/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "InCallWindow.h"

#include <Button.h>
#include <GroupView.h>
#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InCallWindow"
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Messenger.h>
#include <StringView.h>

#include <stdio.h>

#include "CallAudioRouter.h"
#include "DialerWindow.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


static BMessage
_DtmfMessage(const char* digit)
{
	BMessage message(kMsgKeypadKey);
	message.AddString("be:value", digit);
	return message;
}


InCallWindow::InCallWindow(const char* number)
	:
	BWindow(BRect(80, 80, 360, 560), B_TRANSLATE("Call in progress"),
		B_TITLED_WINDOW, B_AVOID_FRONT | B_NOT_RESIZABLE),
	fNumber(number != NULL ? number : ""),
	fMuted(false),
	fLoudspeaker(false)
{
	fNumberLabel = new BStringView("number", fNumber.String());
	fNumberLabel->SetFont(be_bold_font);
	fStatusLabel = new BStringView("status", B_TRANSLATE("Active"));

	fMuteButton = new BButton("mute", B_TRANSLATE("Mute"),
		new BMessage('phMU'));
	fSpeakerButton = new BButton("speaker", B_TRANSLATE("Speaker"),
		new BMessage('phSP'));

	BGroupView* keypad = new BGroupView(B_TRANSLATE("Keypad"),
		B_HORIZONTAL, 4);
	BLayoutBuilder::Group<>(keypad, B_VERTICAL, 4)
		.AddGrid(4, 4)
			.Add(new BButton("k1", "1", new BMessage(_DtmfMessage("1"))),
				0, 0)
			.Add(new BButton("k2", "2", new BMessage(_DtmfMessage("2"))),
				1, 0)
			.Add(new BButton("k3", "3", new BMessage(_DtmfMessage("3"))),
				2, 0)
			.Add(new BButton("k4", "4", new BMessage(_DtmfMessage("4"))),
				0, 1)
			.Add(new BButton("k5", "5", new BMessage(_DtmfMessage("5"))),
				1, 1)
			.Add(new BButton("k6", "6", new BMessage(_DtmfMessage("6"))),
				2, 1)
			.Add(new BButton("k7", "7", new BMessage(_DtmfMessage("7"))),
				0, 2)
			.Add(new BButton("k8", "8", new BMessage(_DtmfMessage("8"))),
				1, 2)
			.Add(new BButton("k9", "9", new BMessage(_DtmfMessage("9"))),
				2, 2)
			.Add(new BButton("kstar", "*",
				new BMessage(_DtmfMessage("*"))), 0, 3)
			.Add(new BButton("k0", "0", new BMessage(_DtmfMessage("0"))),
				1, 3)
			.Add(new BButton("khash", "#",
				new BMessage(_DtmfMessage("#"))), 2, 3)
		.End();

	BButton* holdButton = new BButton("hold", B_TRANSLATE("Hold"),
		new BMessage('phHD'));
	BButton* hangUpButton = new BButton("hangup", B_TRANSLATE("Hang up"),
		new BMessage('phHU'));
	hangUpButton->SetExplicitMinSize(BSize(B_SIZE_UNSET, 48));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fNumberLabel)
		.Add(fStatusLabel)
		.Add(keypad)
		.AddGrid()
			.Add(fMuteButton, 0, 0)
			.Add(fSpeakerButton, 1, 0)
			.Add(holdButton, 0, 1, 2, 1)
		.End()
		.Add(hangUpButton);

	CallAudioRouter::Default()->BeginCall();
}


InCallWindow::~InCallWindow()
{
	CallAudioRouter::Default()->EndCall();
}


void
InCallWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgKeypadKey:
		{
			const char* digit = NULL;
			if (message->FindString("be:value", &digit) == B_OK)
				_AppendDtmf(digit);
			break;
		}
		case 'phMU':
			_ToggleMute();
			break;
		case 'phSP':
			_ToggleSpeaker();
			break;
		case 'phHD':
			_Hold();
			break;
		case 'phHU':
			_HangUp();
			break;
		case kTelephonyMsgCallStateChanged:
		case kTelephonyMsgCallRemoved:
			_Update();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
InCallWindow::Pulse()
{
	_Update();
}


void
InCallWindow::_AppendDtmf(const char* digit)
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem == NULL || digit == NULL)
		return;

	modem->SendDtmf(digit, BMessenger(this), kMsgOpReply);
}


void
InCallWindow::_ToggleMute()
{
	fMuted = !fMuted;
	CallAudioRouter::Default()->SetMuted(fMuted);
	fMuteButton->SetLabel(fMuted ? B_TRANSLATE("Unmute")
		: B_TRANSLATE("Mute"));
}


void
InCallWindow::_ToggleSpeaker()
{
	fLoudspeaker = !fLoudspeaker;
	CallAudioRouter::Default()->SetRoute(fLoudspeaker
		? CallAudioRouter::ROUTE_SPEAKER : CallAudioRouter::ROUTE_HANDSET);
	fSpeakerButton->SetLabel(fLoudspeaker ? B_TRANSLATE("Earpiece")
		: B_TRANSLATE("Speaker"));
}


void
InCallWindow::_Hold()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem != NULL)
		modem->Hold(BMessenger(this), kMsgOpReply);
}


void
InCallWindow::_HangUp()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem != NULL)
		modem->HangUp(BMessenger(this), kMsgOpReply);
	fStatusLabel->SetText(B_TRANSLATE("Hanging up..."));
	PostMessage(B_QUIT_REQUESTED);
}


void
InCallWindow::_Update()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem == NULL)
		return;

	uint32 state = modem->CallState();
	if (state == B_TELEPHONY_CALL_UNKNOWN
			|| state == B_TELEPHONY_CALL_TERMINATED) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	fStatusLabel->SetText(modem->CallStateName());
	const char* number = modem->CallNumber();
	if (number != NULL && fNumber != number) {
		fNumber = number;
		fNumberLabel->SetText(fNumber.String());
	}
}
