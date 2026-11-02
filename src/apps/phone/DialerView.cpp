/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "DialerView.h"

#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Dragger.h>
#include <Font.h>
#include <GridLayout.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
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

#include "CallLog.h"
#include "Phone.h"
#include <telephony/Modem.h>
#include <telephony/TelephonyRoster.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DialerView"


const char* DialerView::kAppSignature = "application/x-vnd.vos-Phone";


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


static bool
_IsPhoneApp()
{
	app_info info;
	return be_app != NULL && be_app->GetAppInfo(&info) == B_OK
		&& strcasecmp(info.signature, "application/x-vnd.vos-Phone") == 0;
}


DialerView::DialerView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fRoster(NULL),
	fKeypad(NULL),
	fLineMenu(NULL)
{
	_Build();
}


DialerView::DialerView(BMessage* archive)
	:
	BView(archive),
	fRoster(NULL),
	fKeypad(NULL),
	fLineMenu(NULL)
{
	_Build();
}


DialerView::~DialerView()
{
}


BArchivable*
DialerView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "DialerView"))
		return NULL;

	return new DialerView(archive);
}


status_t
DialerView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, false);
	if (status == B_OK)
		status = archive->AddString("add_on", kAppSignature);
	if (status == B_OK)
		status = archive->AddString("class", "DialerView");

	return status;
}


void
DialerView::_Build()
{
	fRoster = BTelephonyRoster::Default();

	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fNumberControl = new BTextControl("number", B_TRANSLATE("Number:"),
		"", new BMessage(kMsgDial));
	fStatusView = new BStringView("status", B_TRANSLATE("Ready"));
	fLogView = new BListView("calllog", B_SINGLE_SELECTION_LIST);
	float fontSize = be_plain_font->Size();
	fLogView->SetExplicitMinSize(BSize(B_SIZE_UNSET, fontSize * 8));

	float textHeight = fNumberControl->PreferredSize().height;
	fMenuButton = new BButton("menu", "\xE2\x98\xB0",
		new BMessage(kMsgDialerMenu));
	fMenuButton->SetExplicitMinSize(BSize(textHeight, textHeight));
	fMenuButton->SetExplicitMaxSize(BSize(textHeight, textHeight));
	fMenuButton->SetExplicitPreferredSize(BSize(textHeight, textHeight));

	fDialButton = new BButton("dial", B_TRANSLATE("Call"),
		new BMessage(kMsgDial));
	fHangUpButton = new BButton("hangup", B_TRANSLATE("Hang up"),
		new BMessage(kMsgHangUpAll));
	fHangUpButton->SetEnabled(false);
	float buttonWidth = max_c(fDialButton->PreferredSize().width,
		fHangUpButton->PreferredSize().width);
	BSize buttonSize(buttonWidth, B_SIZE_UNSET);
	fDialButton->SetExplicitMinSize(buttonSize);
	fHangUpButton->SetExplicitMinSize(buttonSize);

	fLineMenu = new BPopUpMenu("lines");
	RefreshLines();
	BMenuField* lineField = new BMenuField("line", B_TRANSLATE("Line:"),
		fLineMenu);

	fLogScroll = new BScrollView("logscroll", fLogView,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	float gap = be_control_look->DefaultItemSpacing() / 2;
	BGroupLayout* bottom = NULL;
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fStatusView)
			.AddGlue()
			.Add(fMenuButton)
		.End()
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_HALF_ITEM_SPACING)
			.AddTextControl(fNumberControl, 0, 0)
			.AddMenuField(lineField, 0, 1)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fDialButton)
			.Add(fHangUpButton)
			.AddGlue()
		.End()
		.AddGrid(gap, gap)
			.GetLayout(&fKeypad)
		.End()
		.Add(fLogScroll)
		.AddGroup(B_HORIZONTAL, 0)
			.GetLayout(&bottom)
			.AddGlue()
		.End();

	float keySide = fontSize * 3;
	for (int32 i = 0; i < 12; i++) {
		BButton* key = new BButton(NULL, kKeypadKeys[i],
			new BMessage(_KeypadMessage(i)));
		key->SetExplicitMinSize(BSize(keySide, keySide));
		key->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
		fKeypad->AddView(key, i % 3, i / 3);
	}

	// A replicant restored from an archive brings its own dragger.
	if (FindView("_dragger_") == NULL)
		bottom->AddView(new BDragger(this));

	_SeedCallLog();
}


void
DialerView::_SeedCallLog()
{
	if (!_IsPhoneApp())
		return;

	Phone* app = (Phone*)be_app;
	CallLog* log = app->CallLogInstance();
	if (log == NULL)
		return;

	for (int32 i = 0; i < log->CountEntries(); i++) {
		const CallLogEntry* entry = log->EntryAt(i);
		if (entry != NULL)
			fLogView->AddItem(new BStringItem(_FormatLogEntry(*entry)));
	}
}


bool
DialerView::_IsReplicated() const
{
	return !_IsPhoneApp();
}


void
DialerView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fNumberControl->SetTarget(this);
	fDialButton->SetTarget(this);
	fHangUpButton->SetTarget(this);
	fMenuButton->SetTarget(this);
	fLineMenu->SetTargetForItems(this);
	for (int32 i = 0; i < fKeypad->BLayout::CountItems(); i++)
		((BButton*)fKeypad->BLayout::ItemAt(i)->View())->SetTarget(this);

	fRoster->StartWatching(BMessenger(this), kTelephonyWatchAll);
	_UpdateStatus();
}


void
DialerView::DetachedFromWindow()
{
	fRoster->StopWatching(BMessenger(this));

	BView::DetachedFromWindow();
}


void
DialerView::RefreshLines()
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
DialerView::SelectedLine()
{
	BMenuItem* item = fLineMenu->FindMarked();
	if (item == NULL || item->Message() == NULL)
		return NULL;

	const char* path = NULL;
	item->Message()->FindString("line", &path);
	return path;
}


void
DialerView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgDial:
			_Dial();
			break;
		case kMsgHangUpAll:
			_HangUpAll();
			break;
		case kMsgDialerMenu:
			_ShowMenu();
			break;
		case kMsgToggleCallLog:
			if (fLogScroll->IsHidden())
				fLogScroll->Show();
			else
				fLogScroll->Hide();
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
				if (!reason.IsEmpty())
					text << ": " << reason;
				fStatusView->SetText(text.String());
			}
			_UpdateStatus();
			break;
		}
		case kMsgLineSelected:
			_UpdateStatus();
			break;
		case kTelephonyMsgModemAdded:
		case kTelephonyMsgModemRemoved:
			RefreshLines();
			break;
		case kTelephonyMsgCallStateChanged:
		case kTelephonyMsgCallAdded:
		case kTelephonyMsgCallRemoved:
			_UpdateStatus();
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
DialerView::_ShowMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("dialer menu", false, false);

	BMenuItem* logItem = new BMenuItem(B_TRANSLATE("Call log"),
		new BMessage(kMsgToggleCallLog));
	logItem->SetTarget(this);
	logItem->SetMarked(!fLogScroll->IsHidden());
	menu->AddItem(logItem);
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
DialerView::AppendKeypadKey(char key)
{
	BString number(fNumberControl->Text());
	number << key;
	fNumberControl->SetText(number.String());
}


void
DialerView::_Dial()
{
	const char* line = SelectedLine();
	BModem* modem = line != NULL ? fRoster->ModemForPath(line)
		: fRoster->ModemAt(0);
	if (modem == NULL) {
		fStatusView->SetText(B_TRANSLATE("No line"));
		return;
	}

	const char* number = fNumberControl->Text();
	if (number == NULL || number[0] == '\0') {
		fStatusView->SetText(B_TRANSLATE("Enter a number"));
		return;
	}

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
DialerView::_HangUpAll()
{
	const char* line = SelectedLine();
	BModem* modem = line != NULL ? fRoster->ModemForPath(line)
		: fRoster->ModemAt(0);
	if (modem == NULL)
		return;

	modem->HangUp(BMessenger(this), kMsgOpReply);
	fStatusView->SetText(B_TRANSLATE("Hanging up..."));
}


void
DialerView::_UpdateStatus()
{
	const char* line = SelectedLine();
	BModem* modem = line != NULL ? fRoster->ModemForPath(line)
		: fRoster->ModemAt(0);
	if (modem == NULL) {
		fStatusView->SetText(B_TRANSLATE("No line"));
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
		if (!fRoster->IsServiceAvailable())
			fStatusView->SetText(B_TRANSLATE("No telephony service"));
		else {
			BString text(B_TRANSLATE("Ready"));
			// SIP lines report no sampled signal.
			if (strncmp(modem->Path(), "sip:", 4) != 0) {
				text << " (" << modem->AccessTypeString() << " "
					<< modem->SignalBars() << "/4)";
			}
			fStatusView->SetText(text.String());
		}
		return;
	}

	BString text(modem->CallStateName());
	const char* number = modem->CallNumber();
	if (number != NULL && number[0] != '\0')
		text << " " << number;
	fStatusView->SetText(text.String());
}
