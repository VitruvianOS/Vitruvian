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
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
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
	fLineMenu = new BPopUpMenu("lines");

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
			.Add(new BMenuField("line", B_TRANSLATE("Line:"),
				fLineMenu))
			.AddGroup(B_HORIZONTAL, 4)
				.Add(fComposeControl)
				.Add(sendButton)
			.End()
		.End();

	threadScroll->SetExplicitSize(BSize(180, B_SIZE_UNSET));

	fThreadList->SetSelectionMessage(new BMessage(kMsgThreadSelected));
	_RefreshLines();
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
		case kMsgLinesChanged:
			_RefreshLines();
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
MessagesWindow::_RefreshLines()
{
	fLineMenu->RemoveItems(0, fLineMenu->CountItems(), true);

	BTelephonyRoster* roster = BTelephonyRoster::Default();
	int32 count = roster->CountModems();
	for (int32 i = 0; i < count; i++) {
		BModem* modem = roster->ModemAt(i);
		if (modem == NULL)
			continue;

		const char* label = modem->OperatorName();
		if (label == NULL || label[0] == '\0')
			label = modem->Path();
		BMessage message(kMsgLineSelected);
		message.AddString("line", modem->Path());
		BMenuItem* item = new BMenuItem(label, new BMessage(message));
		fLineMenu->AddItem(item);
		if (i == 0)
			item->SetMarked(true);
	}
}


const char*
MessagesWindow::_SelectedLine()
{
	BMenuItem* item = fLineMenu->FindMarked();
	if (item == NULL)
		return NULL;

	const char* path = NULL;
	item->Message()->FindString("line", &path);
	return path;
}


void
MessagesWindow::_Send()
{
	BTelephonyRoster* roster = BTelephonyRoster::Default();
	const char* line = _SelectedLine();
	BModem* modem = line != NULL ? roster->ModemForPath(line)
		: roster->ModemAt(0);
	if (modem == NULL)
		return;

	const char* number = fNumberControl->Text();
	const char* text = fComposeControl->Text();
	if (number == NULL || number[0] == '\0'
			|| text == NULL || text[0] == '\0')
		return;

	// A SIP line carries the body as a MESSAGE; a modem line as
	// SMS. The BModem call is the same either way.
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
