/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MessagesWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <Locale.h>
#include <ListView.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>

#include <stdio.h>

#include "MessageStore.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MessagesWindow"


static const char*
_FormatSms(const StoredSms& sms)
{
	static char line[512];
	struct tm timeParts;
	localtime_r(&sms.timestamp, &timeParts);
	snprintf(line, sizeof(line), "[%02d:%02d %s %s] %s",
		timeParts.tm_hour, timeParts.tm_min,
		sms.direction, sms.state, sms.text.String());
	return line;
}


MessagesWindow::MessagesWindow(const BRect& frame, MessageStore* store)
	:
	BWindow(frame, B_TRANSLATE("Messages"), B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS),
	fStore(store)
{
	fThreadList = new BListView("threads", B_SINGLE_SELECTION_LIST);
	fConversationList = new BListView("conversation",
		B_SINGLE_SELECTION_LIST);
	fNumberControl = new BTextControl("number", B_TRANSLATE("To:"),
		"", NULL);
	fComposeControl = new BTextControl("compose", B_TRANSLATE("Message:"),
		"", new BMessage(kMsgSend));

	BButton* sendButton = new BButton("send", B_TRANSLATE("Send"),
		new BMessage(kMsgSend));
	BButton* deleteButton = new BButton("delete", B_TRANSLATE("Delete"),
		new BMessage(kMsgDeleteThread));

	BScrollView* threadScroll = new BScrollView("threadscroll",
		fThreadList, B_FRAME_EVENTS | B_WILL_DRAW, false, true);
	BScrollView* conversationScroll = new BScrollView("convsroll",
		fConversationList, B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_SMALL_INSETS)
		.AddGroup(B_VERTICAL, 4)
			.Add(threadScroll)
			.Add(deleteButton)
		.End()
		.AddGroup(B_VERTICAL, 4)
			.Add(conversationScroll)
			.Add(fNumberControl)
			.AddGroup(B_HORIZONTAL, 4)
				.Add(fComposeControl)
				.Add(sendButton)
			.End()
		.End();

	threadScroll->SetExplicitSize(BSize(180, B_SIZE_UNSET));

	fThreadList->SetSelectionMessage(new BMessage(kMsgThreadSelected));
	Refresh();
}


void
MessagesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgThreadSelected:
			_SelectionChanged();
			break;
		case kMsgSend:
			_Send();
			break;
		case kMsgDeleteThread:
			_DeleteThread();
			break;
		case kMsgSendReply:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status == B_OK) {
				fComposeControl->SetText("");
				Refresh();
			}
			break;
		}
		case kMsgStoreChanged:
			Refresh();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
MessagesWindow::Refresh()
{
	int32 selected = fThreadList->CurrentSelection();
	BString selectedNumber(_SelectedNumber());

	fThreadList->MakeEmpty();
	fConversationList->MakeEmpty();

	for (int32 i = 0; i < fStore->CountThreads(); i++) {
		SmsThread* thread = fStore->ThreadAt(i);
		fThreadList->AddItem(new BStringItem(thread->number.String()));
		if (!selectedNumber.IsEmpty()
				&& thread->number == selectedNumber)
			fThreadList->Select(i);
	}

	(void)selected;
	_SelectionChanged();
}


const char*
MessagesWindow::_SelectedNumber()
{
	static BString number;
	int32 index = fThreadList->CurrentSelection();
	SmsThread* thread = fStore->ThreadAt(index);
	number = thread != NULL ? thread->number : "";
	return number.String();
}


void
MessagesWindow::_SelectionChanged()
{
	fConversationList->MakeEmpty();

	const char* number = _SelectedNumber();
	SmsThread* thread = fStore->FindThread(number);
	if (thread == NULL)
		return;

	fNumberControl->SetText(thread->number.String());
	for (int32 i = 0; i < thread->messages.CountItems(); i++) {
		StoredSms* sms = thread->messages.ItemAt(i);
		if (sms != NULL)
			fConversationList->AddItem(new BStringItem(
				_FormatSms(*sms)));
	}
}


void
MessagesWindow::_Send()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	BModem* modem = roster->ModemAt(0);
	if (modem == NULL)
		return;

	const char* number = fNumberControl->Text();
	const char* text = fComposeControl->Text();
	if (number == NULL || number[0] == '\0'
			|| text == NULL || text[0] == '\0')
		return;

	status_t status = modem->SendMessage(number, text, BMessenger(this),
		kMsgSendReply);
	if (status != B_OK)
		return;

	fStore->AddOutgoing(number, text);
	Refresh();
}


void
MessagesWindow::_DeleteThread()
{
	const char* number = _SelectedNumber();
	if (number[0] == '\0')
		return;

	fStore->RemoveThread(number);
	Refresh();
}
