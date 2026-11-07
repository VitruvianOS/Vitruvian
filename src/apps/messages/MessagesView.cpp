/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "MessagesView.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Dragger.h>
#include <Font.h>
#include <GridLayout.h>
#include <GroupLayout.h>
#include <Invoker.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <ScrollView.h>
#include <Size.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "MessageStore.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MessagesView"


const char* MessagesView::kAppSignature = "application/x-vnd.vos-Messages";


class ThreadListView : public BListView {
public:
	ThreadListView(const char* name, const char* hint)
		:
		BListView(name, B_SINGLE_SELECTION_LIST),
		fHint(hint)
	{
	}

	virtual void Draw(BRect updateRect)
	{
		BListView::Draw(updateRect);
		if (CountItems() > 0)
			return;

		SetHighColor(tint_color(ui_color(B_LIST_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
		SetLowColor(ui_color(B_LIST_BACKGROUND_COLOR));
		font_height height;
		GetFontHeight(&height);
		BRect bounds = Bounds();
		float width = StringWidth(fHint.String());
		DrawString(fHint.String(), BPoint((bounds.Width() - width) / 2,
			(bounds.Height() + height.ascent) / 2));
	}

private:
	BString	fHint;
};


static BString
_FormatSms(const StoredSms& sms)
{
	char line[512];
	struct tm timeParts;
	localtime_r(&sms.timestamp, &timeParts);
	snprintf(line, sizeof(line), "[%02d:%02d %s %s] %s",
		timeParts.tm_hour, timeParts.tm_min,
		sms.direction, sms.state, sms.text.String());
	return BString(line);
}


static bool
_IsMessagesApp()
{
	app_info info;
	return be_app != NULL && be_app->GetAppInfo(&info) == B_OK
		&& strcasecmp(info.signature, "application/x-vnd.vos-Messages") == 0;
}


MessagesView::MessagesView(const char* name, MessageStore* store)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fStore(store),
	fOwnsStore(false)
{
	_Build();
}


MessagesView::MessagesView(BMessage* archive)
	:
	BView(archive),
	fStore(NULL),
	fOwnsStore(true)
{
	_Build();
}


MessagesView::~MessagesView()
{
	if (fOwnsStore)
		delete fStore;
}


BArchivable*
MessagesView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "MessagesView"))
		return NULL;

	return new MessagesView(archive);
}


status_t
MessagesView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, false);
	if (status == B_OK)
		status = archive->AddString("add_on", kAppSignature);
	if (status == B_OK)
		status = archive->AddString("class", "MessagesView");

	return status;
}


void
MessagesView::_Build()
{
	fRoster = BTelephonyRoster::Default();
	if (fStore == NULL) {
		fStore = new MessageStore();
		fOwnsStore = true;
	}

	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fStatusView = new BStringView("status", B_TRANSLATE("Messages"));
	fThreadList = new ThreadListView("threads",
		B_TRANSLATE("No conversations"));
	fConversationList = new BListView("conversation",
		B_SINGLE_SELECTION_LIST);
	fNumberControl = new BTextControl("number", B_TRANSLATE("Number:"),
		"", NULL);
	fComposeControl = new BTextControl("compose", NULL, "",
		new BMessage(kMsgSend));
	fSendButton = new BButton("send", B_TRANSLATE("Send"),
		new BMessage(kMsgSend));
	fLineMenu = new BPopUpMenu("lines");
	BMenuField* lineField = new BMenuField("line", B_TRANSLATE("Line:"),
		fLineMenu);

	float textHeight = fNumberControl->PreferredSize().height;
	fMenuButton = new BButton("menu", "\xE2\x98\xB0",
		new BMessage(kMsgMessagesMenu));
	fMenuButton->SetExplicitMinSize(BSize(textHeight, textHeight));
	fMenuButton->SetExplicitMaxSize(BSize(textHeight, textHeight));
	fMenuButton->SetExplicitPreferredSize(BSize(textHeight, textHeight));

	float fontSize = be_plain_font->Size();
	fThreadList->SetSelectionMessage(new BMessage(kMsgThreadSelected));
	fThreadList->SetExplicitMinSize(BSize(fontSize * 8, fontSize * 8));
	fConversationList->SetExplicitMinSize(BSize(fontSize * 16,
		fontSize * 8));

	BScrollView* threadScroll = new BScrollView("threadscroll",
		fThreadList, B_FRAME_EVENTS | B_WILL_DRAW, false, true);
	BScrollView* conversationScroll = new BScrollView("conversationscroll",
		fConversationList, B_FRAME_EVENTS | B_WILL_DRAW, false, true);
	threadScroll->SetExplicitPreferredSize(BSize(
		be_plain_font->StringWidth("+00 000 000 0000") + fontSize * 2,
		B_SIZE_UNSET));

	RefreshLines();

	BGroupLayout* bottom = NULL;
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0.0f)
			.Add(fStatusView)
			.AddGlue()
			.Add(fMenuButton)
		.End()
		.AddSplit(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 1.0f)
			.Add(threadScroll, 1)
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 3)
				.Add(conversationScroll)
				.AddGrid(B_USE_DEFAULT_SPACING, B_USE_HALF_ITEM_SPACING)
					.AddTextControl(fNumberControl, 0, 0)
					.AddMenuField(lineField, 0, 1)
				.End()
				.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
					.Add(fComposeControl)
					.Add(fSendButton)
				.End()
			.End()
		.End()
		.AddGroup(B_HORIZONTAL, 0)
			.GetLayout(&bottom)
			.AddGlue()
		.End();

	// The glue alone gives the dragger row no height.
	bottom->SetExplicitMinSize(BSize(0, 7));
	bottom->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 7));
	if (FindView("_dragger_") == NULL)
		bottom->AddView(new BDragger(this));

	Refresh();
}


bool
MessagesView::_IsReplicated() const
{
	return !_IsMessagesApp();
}


void
MessagesView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fMenuButton->SetTarget(this);
	fSendButton->SetTarget(this);
	fComposeControl->SetTarget(this);
	fThreadList->SetTarget(this);
	fLineMenu->SetTargetForItems(this);

	if (!_IsReplicated())
		Window()->SetDefaultButton(fSendButton);
	else {
		fRoster->StartWatching(BMessenger(this),
			kTelephonyWatchMessages | kTelephonyWatchModems);
	}
}


void
MessagesView::DetachedFromWindow()
{
	if (_IsReplicated())
		fRoster->StopWatching(BMessenger(this));

	BView::DetachedFromWindow();
}


void
MessagesView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgThreadSelected:
			_SelectionChanged();
			break;
		case kMsgSend:
			_Send();
			break;
		case kMsgMessagesMenu:
			_ShowMenu();
			break;
		case kMsgNewMessage:
			_NewMessage();
			break;
		case kMsgDeleteThread:
			_ConfirmDelete();
			break;
		case kMsgDeleteConfirmed:
		{
			int32 which = 0;
			if (message->FindInt32("which", &which) == B_OK && which == 1)
				_DeleteThread();
			break;
		}
		case kMsgSendReply:
		{
			int32 status = B_ERROR;
			message->FindInt32("status", &status);
			if (status == B_OK) {
				fComposeControl->SetText("");
				fStatusView->SetText(B_TRANSLATE("Messages"));
				Refresh();
			} else
				fStatusView->SetText(B_TRANSLATE("Send failed"));
			break;
		}
		case kMsgStoreChanged:
			Refresh();
			break;
		case kMsgLinesChanged:
		case kTelephonyMsgModemAdded:
		case kTelephonyMsgModemRemoved:
			RefreshLines();
			break;
		case kTelephonyMsgSmsAdded:
			_OnSmsAdded(message);
			break;
		case kTelephonyMsgSmsStateChanged:
			_OnSmsStateChanged(message);
			break;
		case kMsgLineSelected:
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
MessagesView::_OnSmsAdded(BMessage* message)
{
	bool received = false;
	message->FindBool(kTelephonyFieldSmsReceived, &received);
	if (!received)
		return;

	// The running application stores and notifies itself.
	if (_IsMessagesApp() || be_roster->IsRunning(kAppSignature)) {
		if (fOwnsStore)
			fStore->Load();
		Refresh();
		return;
	}

	BString number;
	message->FindString(kTelephonyFieldSmsNumber, &number);
	BString text;
	message->FindString(kTelephonyFieldSmsText, &text);
	fStore->AddIncoming(number.String(),
		text.IsEmpty() ? "..." : text.String());
	Refresh();
}


void
MessagesView::_OnSmsStateChanged(BMessage* message)
{
	bool received = false;
	message->FindBool(kTelephonyFieldSmsReceived, &received);
	if (received)
		return;

	BString number;
	message->FindString(kTelephonyFieldSmsNumber, &number);
	uint32 state = 0;
	message->FindUInt32(kTelephonyFieldSmsState, &state);
	if (state == B_TELEPHONY_SMS_SENT)
		fStore->UpdateLastOutgoingState(number.String(), "sent");
	else if (state == B_TELEPHONY_SMS_UNKNOWN)
		fStore->UpdateLastOutgoingState(number.String(), "failed");
	Refresh();
}


void
MessagesView::_ShowMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("messages menu", false, false);

	BMenuItem* newItem = new BMenuItem(B_TRANSLATE("New message"),
		new BMessage(kMsgNewMessage), 'N');
	newItem->SetTarget(this);
	menu->AddItem(newItem);

	BMenuItem* deleteItem = new BMenuItem(
		B_TRANSLATE("Delete conversation" B_UTF8_ELLIPSIS),
		new BMessage(kMsgDeleteThread));
	deleteItem->SetTarget(this);
	deleteItem->SetEnabled(fThreadList->CurrentSelection() >= 0);
	menu->AddItem(deleteItem);
	menu->AddSeparatorItem();

	BDragger* dragger = dynamic_cast<BDragger*>(FindView("_dragger_"));
	if (_IsReplicated()) {
		BMenuItem* item = new BMenuItem(B_TRANSLATE("Remove replicant"),
			new BMessage(B_TRASH_TARGET));
		if (dragger != NULL)
			item->SetTarget(dragger);
		else
			item->SetEnabled(false);
		menu->AddItem(item);
	} else {
		BMenuItem* item = new BMenuItem(B_TRANSLATE("Quit"),
			new BMessage(B_QUIT_REQUESTED), 'Q');
		item->SetTarget(be_app);
		menu->AddItem(item);
	}

	BRect frame = fMenuButton->Frame();
	menu->Go(ConvertToScreen(BPoint(frame.left, frame.bottom + 1)), true,
		false, true);
}


void
MessagesView::Refresh()
{
	BString selectedNumber = _SelectedNumber();

	fThreadList->MakeEmpty();
	fConversationList->MakeEmpty();

	for (int32 i = 0; i < fStore->CountThreads(); i++) {
		SmsThread* thread = fStore->ThreadAt(i);
		fThreadList->AddItem(new BStringItem(thread->number.String()));
		if (!selectedNumber.IsEmpty() && thread->number == selectedNumber)
			fThreadList->Select(i);
	}

	_SelectionChanged();
	fThreadList->Invalidate();
}


BString
MessagesView::_SelectedNumber()
{
	SmsThread* thread = fStore->ThreadAt(fThreadList->CurrentSelection());
	return thread != NULL ? thread->number : BString();
}


void
MessagesView::_SelectionChanged()
{
	fConversationList->MakeEmpty();

	SmsThread* thread = fStore->FindThread(_SelectedNumber().String());
	if (thread == NULL)
		return;

	fNumberControl->SetText(thread->number.String());
	for (int32 i = 0; i < thread->messages.CountItems(); i++) {
		StoredSms* sms = thread->messages.ItemAt(i);
		if (sms != NULL) {
			fConversationList->AddItem(
				new BStringItem(_FormatSms(*sms).String()));
		}
	}
}


void
MessagesView::RefreshLines()
{
	fLineMenu->RemoveItems(0, fLineMenu->CountItems(), true);

	int32 count = fRoster->CountModems();
	if (count == 0) {
		BMenuItem* none = new BMenuItem(B_TRANSLATE("No lines"),
			new BMessage(kMsgLineSelected));
		none->SetMarked(true);
		fLineMenu->AddItem(none);
		fLineMenu->SetEnabled(false);
		return;
	}

	fLineMenu->SetEnabled(true);
	for (int32 i = 0; i < count; i++) {
		BModem* modem = fRoster->ModemAt(i);
		if (modem == NULL)
			continue;

		const char* label = modem->OperatorName();
		if (label == NULL || label[0] == '\0')
			label = modem->Path();
		BMessage message(kMsgLineSelected);
		message.AddString("line", modem->Path());
		BMenuItem* item = new BMenuItem(label, new BMessage(message));
		item->SetTarget(this);
		fLineMenu->AddItem(item);
		if (i == 0)
			item->SetMarked(true);
	}
}


const char*
MessagesView::_SelectedLine()
{
	BMenuItem* item = fLineMenu->FindMarked();
	if (item == NULL || item->Message() == NULL)
		return NULL;

	const char* path = NULL;
	item->Message()->FindString("line", &path);
	return path;
}


void
MessagesView::_NewMessage()
{
	fThreadList->DeselectAll();
	fConversationList->MakeEmpty();
	fNumberControl->SetText("");
	fComposeControl->SetText("");
	fNumberControl->MakeFocus(true);
}


void
MessagesView::_Send()
{
	const char* line = _SelectedLine();
	BModem* modem = line != NULL ? fRoster->ModemForPath(line)
		: fRoster->ModemAt(0);
	if (modem == NULL) {
		fStatusView->SetText(B_TRANSLATE("No line"));
		return;
	}

	const char* number = fNumberControl->Text();
	const char* text = fComposeControl->Text();
	if (number == NULL || number[0] == '\0'
			|| text == NULL || text[0] == '\0')
		return;

	status_t status = modem->SendMessage(number, text, BMessenger(this),
		kMsgSendReply);
	if (status != B_OK) {
		fStatusView->SetText(B_TRANSLATE("Send failed"));
		return;
	}

	fStore->AddOutgoing(number, text);
	Refresh();
}


void
MessagesView::_ConfirmDelete()
{
	BString number = _SelectedNumber();
	if (number.IsEmpty())
		return;

	BString text(B_TRANSLATE("Delete the conversation with %number%?"));
	text.ReplaceFirst("%number%", number.String());
	BAlert* alert = new BAlert(B_TRANSLATE("Delete conversation"),
		text.String(), B_TRANSLATE("Cancel"), B_TRANSLATE("Delete"),
		NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	alert->Go(new BInvoker(new BMessage(kMsgDeleteConfirmed), this));
}


void
MessagesView::_DeleteThread()
{
	BString number = _SelectedNumber();
	if (number.IsEmpty())
		return;

	fStore->RemoveThread(number.String());
	Refresh();
}
