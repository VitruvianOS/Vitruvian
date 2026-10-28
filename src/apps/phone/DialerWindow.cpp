/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "DialerWindow.h"

#include <Alert.h>
#include <Button.h>
#include <GroupView.h>
#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DialerWindow"
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Locale.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>

#include <stdio.h>

#include "CallLog.h"
#include "Phone.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


static const char* kKeypadKeys[12] = {
	"1", "2", "3",
	"4", "5", "6",
	"7", "8", "9",
	"*", "0", "#"
};


static BMessage
_KeypadMessage(int32 index)
{
	BMessage message(kMsgKeypadKey);
	message.AddString("be:value", kKeypadKeys[index]);
	return message;
}


static const char*
_FormatLogEntry(const CallLogEntry& entry)
{
	static char line[160];
	struct tm timeParts;
	localtime_r(&entry.timestamp, &timeParts);
	snprintf(line, sizeof(line), "%s %s %s %02d:%02d",
		entry.direction, entry.number.String(), entry.result,
		timeParts.tm_hour, timeParts.tm_min);
	return line;
}


DialerWindow::DialerWindow(const BRect& frame)
	:
	BWindow(frame, B_TRANSLATE("Phone"), B_TITLED_WINDOW,
		B_AVOID_FRONT | B_NOT_RESIZABLE)
{
	fNumberControl = new BTextControl("number", B_TRANSLATE("Number:"),
		"", new BMessage(kMsgDial));
	fStatusView = new BStringView("status", B_TRANSLATE("Ready"));
	fLogView = new BListView("calllog", B_SINGLE_SELECTION_LIST);

	fDialButton = new BButton("dial", B_TRANSLATE("Call"),
		new BMessage(kMsgDial));
	fHangUpButton = new BButton("hangup", B_TRANSLATE("Hang up"),
		new BMessage(kMsgHangUpAll));
	fHangUpButton->SetEnabled(false);

	BGroupView* keypad = new BGroupView(B_TRANSLATE("Keypad"),
		B_HORIZONTAL, 4);
	BLayoutBuilder::Group<>(keypad, B_VERTICAL, 4)
		.AddGrid(4, 4)
			.Add(new BButton("k0", kKeypadKeys[0],
				new BMessage(_KeypadMessage(0))), 0, 0)
			.Add(new BButton("k1", kKeypadKeys[1],
				new BMessage(_KeypadMessage(1))), 1, 0)
			.Add(new BButton("k2", kKeypadKeys[2],
				new BMessage(_KeypadMessage(2))), 2, 0)
			.Add(new BButton("k3", kKeypadKeys[3],
				new BMessage(_KeypadMessage(3))), 0, 1)
			.Add(new BButton("k4", kKeypadKeys[4],
				new BMessage(_KeypadMessage(4))), 1, 1)
			.Add(new BButton("k5", kKeypadKeys[5],
				new BMessage(_KeypadMessage(5))), 2, 1)
			.Add(new BButton("k6", kKeypadKeys[6],
				new BMessage(_KeypadMessage(6))), 0, 2)
			.Add(new BButton("k7", kKeypadKeys[7],
				new BMessage(_KeypadMessage(7))), 1, 2)
			.Add(new BButton("k8", kKeypadKeys[8],
				new BMessage(_KeypadMessage(8))), 2, 2)
			.Add(new BButton("k9", kKeypadKeys[9],
				new BMessage(_KeypadMessage(9))), 0, 3)
			.Add(new BButton("kstar", kKeypadKeys[10],
				new BMessage(_KeypadMessage(10))), 1, 3)
			.Add(new BButton("khash", kKeypadKeys[11],
				new BMessage(_KeypadMessage(11))), 2, 3)
		.End();

	BScrollView* logScroll = new BScrollView("logscroll", fLogView,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.Add(fNumberControl)
		.Add(fStatusView)
		.AddGrid()
			.Add(fDialButton, 0, 0)
			.Add(fHangUpButton, 1, 0)
		.End()
		.Add(keypad)
		.Add(logScroll);

	// Seed the log list from the stored history.
	Phone* app = (Phone*)be_app;
	if (app != NULL && app->CallLogInstance() != NULL) {
		CallLog* log = app->CallLogInstance();
		for (int32 i = 0; i < log->CountEntries(); i++) {
			const CallLogEntry* entry = log->EntryAt(i);
			if (entry != NULL)
				fLogView->AddItem(new BStringItem(
					_FormatLogEntry(*entry)));
		}
	}
}


void
DialerWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDial:
			_Dial();
			break;
		case kMsgHangUpAll:
			_HangUpAll();
			break;
		case kMsgKeypadKey:
		{
			const char* key = NULL;
			if (message->FindString("be:value", &key) == B_OK
					|| message->FindString("label", &key) == B_OK) {
				if (key != NULL && key[0] != '\0')
					AppendKeypadKey(key[0]);
			}
			break;
		}
		case kMsgDialReply:
		case kMsgOpReply:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status != B_OK) {
				BString reason;
				message->FindString("reason", &reason);
				BString text(B_TRANSLATE("Operation failed"));
				if (!reason.IsEmpty()) {
					text << ": " << reason;
				}
				fStatusView->SetText(text.String());
			}
			_UpdateStatus();
			break;
		}
		case kTelephonyMsgCallStateChanged:
		case kTelephonyMsgCallAdded:
		case kTelephonyMsgCallRemoved:
			_UpdateStatus();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
DialerWindow::AppendKeypadKey(char key)
{
	BString number(fNumberControl->Text());
	number << key;
	fNumberControl->SetText(number.String());
}


void
DialerWindow::_Dial()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem == NULL) {
		fStatusView->SetText(B_TRANSLATE("No modem"));
		return;
	}

	const char* number = fNumberControl->Text();
	if (number == NULL || number[0] == '\0') {
		fStatusView->SetText(B_TRANSLATE("Enter a number"));
		return;
	}

	// Contacts lookup: bind to the contacts kit when it exists; the
	// tree has none yet, so dialing uses the raw number.
	status_t status = modem->Dial(number, BMessenger(this), kMsgDialReply);
	if (status != B_OK) {
		fStatusView->SetText(B_TRANSLATE("Dial failed to dispatch"));
		return;
	}

	fDialButton->SetEnabled(false);
	fHangUpButton->SetEnabled(true);
	fStatusView->SetText(B_TRANSLATE("Dialing..."));
}


void
DialerWindow::_HangUpAll()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem == NULL)
		return;

	modem->HangUp(BMessenger(this), kMsgOpReply);
	fStatusView->SetText(B_TRANSLATE("Hanging up..."));
}


void
DialerWindow::_UpdateStatus()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem == NULL) {
		fStatusView->SetText(B_TRANSLATE("No modem"));
		fDialButton->SetEnabled(false);
		fHangUpButton->SetEnabled(false);
		return;
	}

	uint32 callState = modem->CallState();
	bool inCall = callState != B_TELEPHONY_CALL_UNKNOWN
		&& callState != B_TELEPHONY_CALL_TERMINATED;
	fDialButton->SetEnabled(!inCall);
	fHangUpButton->SetEnabled(inCall);

	if (!inCall) {
		if (!roster->IsServiceAvailable())
			fStatusView->SetText(B_TRANSLATE("No modem service"));
		else {
			BString text(B_TRANSLATE("Ready"));
			text << " (" << modem->AccessTypeString() << " "
				<< modem->SignalBars() << "/4)";
			fStatusView->SetText(text.String());
		}
		return;
	}

	BString text(modem->CallStateName());
	const char* number = modem->CallNumber();
	if (number != NULL && number[0] != '\0') {
		text << " ";
		text << number;
	}
	fStatusView->SetText(text.String());
}
