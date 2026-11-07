/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "SettingsWindow.h"

#include <Button.h>
#include <CheckBox.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <ScrollView.h>
#include <TextControl.h>
#include <StringView.h>
#include <TextView.h>
#include <View.h>

#include <time.h>

#include "LocalSendSettings.h"


static const uint32 kMsgSave = 'psav';
static const uint32 kMsgPinToggled = 'ppin';


SettingsWindow::SettingsWindow(BRect frame, LocalSendSettings* settings,
	const LocalSend::LsDevice& device)
	:
	BWindow(frame, "LocalSend Settings", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_RESIZABLE),
	fSettings(settings),
	fDevice(device)
{
	BView* top = new BView("top", B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// No modification message: kMsgSave closes the window.
	fAliasControl = new BTextControl("Alias:", settings->Alias(), NULL);
	fDestinationControl = new BTextControl("Save files to:",
		settings->DestinationFolder(), NULL);

	fPinCheck = new BCheckBox("Require a PIN for incoming transfers",
		new BMessage(kMsgPinToggled));
	fPinCheck->SetValue(settings->RequirePin() ? B_CONTROL_ON
		: B_CONTROL_OFF);
	fPinControl = new BTextControl("PIN:", settings->Pin(), NULL);
	fPinControl->SetEnabled(settings->RequirePin());

	fHistoryView = new BTextView("history");
	fHistoryView->MakeEditable(false);
	BScrollView* historyScroll = new BScrollView("historyScroll",
		fHistoryView, 0, true, true);

	BButton* save = new BButton("Save", new BMessage(kMsgSave));
	save->SetTarget(this);

	BLayoutBuilder::Group<>(top, B_VERTICAL, 8)
		.Add(fAliasControl)
		.Add(fDestinationControl)
		.AddGroup(B_VERTICAL, 4)
			.Add(fPinCheck)
			.Add(fPinControl)
		.End()
		.AddStrut(8)
		.Add(new BStringView("historyLabel", "Transfer history:"))
		.Add(historyScroll, 1.0)
		.AddGroup(B_HORIZONTAL, 10)
			.Add(save)
		.End()
		.SetInsets(10, 10, 10, 10);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(top);

	_RefreshHistory();
}


void
SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPinToggled:
			fPinControl->SetEnabled(fPinCheck->Value() == B_CONTROL_ON);
			break;

		case kMsgSave:
			_Save();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
SettingsWindow::_Save()
{
	fSettings->SetAlias(fAliasControl->Text());
	fSettings->SetDestinationFolder(fDestinationControl->Text());
	fSettings->SetRequirePin(fPinCheck->Value() == B_CONTROL_ON);
	fSettings->SetPin(fPinControl->Text());
	fSettings->Save();
	PostMessage(B_QUIT_REQUESTED);
}


void
SettingsWindow::_RefreshHistory()
{
	BString text;
	const std::vector<LsHistoryEntry>& history = fSettings->History();
	if (history.empty())
		text = "(no transfers yet)";

	for (size_t i = history.size(); i > 0; i--) {
		const LsHistoryEntry& entry = history[i - 1];
		char when[64];
		time_t whenTime = static_cast<time_t>(entry.when);
		struct tm* parts = localtime(&whenTime);
		strftime(when, sizeof(when), "%Y-%m-%d %H:%M", parts);

		text << when << "  ";
		text << (entry.outgoing ? "sent" : "received") << "  ";
		text << entry.fileNames << "  ";
		text << (entry.success ? "ok" : "failed") << "\n";
	}
	fHistoryView->SetText(text.String());
}
