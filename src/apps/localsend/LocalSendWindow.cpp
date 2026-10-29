/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "LocalSendWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <FilePanel.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Message.h>
#include <Notification.h>
#include <ScrollView.h>
#include <StatusBar.h>
#include <StringList.h>
#include <StringView.h>
#include <View.h>

#include <Entry.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>

#include "DeviceListItem.h"
#include "LocalSendMessages.h"
#include "LocalSendSettings.h"
#include "LocalSendManager.h"
#include "ReceivePromptWindow.h"
#include "SettingsWindow.h"


static const uint32 kMsgSendFiles = 'sfil';
static const uint32 kMsgSendSelected = 'ssel';
static const uint32 kMsgShowSettings = 'setg';
static const uint32 kMsgAbout = 'abot';
static const uint32 kMsgFilePanelClosed = 'fpcl';
static const uint32 kMsgPromptAccept = 'pacc';
static const uint32 kMsgPromptDecline = 'pdec';
static const uint32 kMsgPromptAlways = 'palw';


// List that shows a hint while it has no rows.
class HintListView : public BListView {
public:
	HintListView(const char* name, const char* hint)
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

		SetHighColor(tint_color(ui_color(B_PANEL_TEXT_COLOR),
			B_LIGHTEN_1_TINT));
		font_height height;
		GetFontHeight(&height);
		BRect bounds = Bounds();
		float x = (bounds.Width() - StringWidth(fHint.String())) / 2;
		DrawString(fHint.String(), BPoint(max_c(x, 4),
			bounds.Height() / 2 + height.ascent / 2));
	}

private:
	BString	fHint;
};


LocalSendWindow::LocalSendWindow(BRect frame, LocalSendManager* manager,
	LocalSendSettings* settings)
	:
	BWindow(frame, "LocalSend", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE),
	fManager(manager),
	fSettings(settings),
	fFilePanel(NULL),
	fLastFraction(0)
{
	BView* top = new BView("top", B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* fileMenu = new BMenu("File");
	fileMenu->AddItem(new BMenuItem("Send Files...",
		new BMessage(kMsgSendFiles), 'S'));
	fileMenu->AddItem(new BMenuItem("Send to Selected Device",
		new BMessage(kMsgSendSelected), 'D'));
	fileMenu->AddSeparatorItem();
	fileMenu->AddItem(new BMenuItem("Settings...",
		new BMessage(kMsgShowSettings), ','));
	fileMenu->AddSeparatorItem();
	BMenuItem* quitItem = new BMenuItem("Quit",
		new BMessage(B_QUIT_REQUESTED), 'Q');
	fileMenu->AddItem(quitItem);
	fileMenu->SetTargetForItems(this);
	// Quit must reach the app so it shuts the networking down; the
	// window on its own would only close itself.
	quitItem->SetTarget(be_app_messenger);
	menuBar->AddItem(fileMenu);

	BMenu* helpMenu = new BMenu("Help");
	BMenuItem* aboutItem = new BMenuItem("About LocalSend",
		new BMessage(kMsgAbout));
	aboutItem->SetTarget(this);
	helpMenu->AddItem(aboutItem);
	menuBar->AddItem(helpMenu);

	fDeviceList = new HintListView("devices",
		"No devices found yet. Other LocalSend devices appear here.");
	fDeviceList->SetSelectionMessage(new BMessage('sdev'));
	BScrollView* listScroll = new BScrollView("scroll", fDeviceList, 0, false,
		true, B_FANCY_BORDER);

	fSendButton = new BButton("Send", new BMessage(kMsgSendSelected));
	fSendButton->SetEnabled(false);

	fProgress = new BStatusBar("progress", "");
	fProgress->SetMaxValue(1.0);

	fStatus = new BStringView("status", "Waiting for devices...");

	BLayoutBuilder::Group<>(top, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(listScroll, 1.0)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(fSendButton)
				.Add(fProgress, 1.0)
			.End()
			.Add(fStatus)
		.End();

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(top);

	fDeviceList->SetTarget(this);

	// Accept file drops from Tracker.
	SetFlags(Flags() | B_WILL_ACCEPT_FIRST_CLICK);
}


bool
LocalSendWindow::QuitRequested()
{
	// Closing the window quits the app; LocalSendApp::QuitRequested
	// stops the networking on the way out.
	return true;
}


void
LocalSendWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSendFiles:
			_PickFiles();
			break;

		case kMsgSendSelected:
			_SendToSelected();
			break;

		case kMsgShowSettings:
			_ShowSettings();
			break;

		case kMsgAbout:
		{
			BAlert* alert = new BAlert("About",
				"LocalSend for V\\OS\n\n"
				"Protocol-compatible file transfer with the\n"
				"LocalSend ecosystem (localsend.org).\n",
				"OK");
			alert->Go();
			break;
		}

		case kMsgFilePanelClosed:
		{
			entry_ref ref;
			int32 index = 0;
			fPendingFiles.clear();
			while (message->FindRef("refs", index, &ref) == B_OK) {
				BPath path(&ref);
				if (path.Path() != NULL)
					fPendingFiles.push_back(BString(path.Path()));
				index++;
			}
			if (!fPendingFiles.empty())
				_SendToSelected();
			break;
		}

		case 'sdev':
		{
			fSendButton->SetEnabled(fDeviceList->CurrentSelection() >= 0);
			break;
		}

		case kMsgDeviceUpdate:
			_HandleDeviceUpdate(message);
			break;

		case kMsgReceivePrompt:
			_HandleIncomingPrompt(message);
			break;

		case kMsgSendProgress:
			_HandleProgress(message);
			break;

		case kMsgSendDone:
			_HandleDone(message);
			break;

		case B_REFS_RECEIVED:
			// Files dropped on the app icon: stash for next send.
			fPendingFiles.clear();
			{
				entry_ref ref;
				int32 index = 0;
				while (message->FindRef("refs", index, &ref) == B_OK) {
					BPath path(&ref);
					if (path.Path() != NULL)
						fPendingFiles.push_back(BString(path.Path()));
					index++;
				}
			}
			if (!fPendingFiles.empty()) {
				BString statusText("Ready to send ");
				statusText << (fPendingFiles.size() == 1
					? "1 file" : "N files")
					<< "; pick a device.";
				_UpdateStatus(statusText);
				fSendButton->SetEnabled(fDeviceList->CurrentSelection()
					>= 0);
			}
			break;

		case kMsgPromptAccept:
		case kMsgPromptAlways:
		{
			bool always = false;
			message->FindBool("always", &always);
			if (message->what == kMsgPromptAlways)
				always = true;
			std::vector<BString> none; // empty = all files
			fManager->AnswerPrompt(true, always, none);
			_UpdateStatus("Incoming transfer accepted.");
			break;
		}

		case kMsgPromptDecline:
		{
			std::vector<BString> none;
			fManager->AnswerPrompt(false, false, none);
			_UpdateStatus("Incoming transfer declined.");
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
LocalSendWindow::_HandleDeviceUpdate(BMessage* message)
{
	BString alias;
	BString model;
	BString fingerprint;
	BString address;
	BString typeName;
	BString version;
	BString protocol;
	uint16 port = 0;
	bool download = false;

	message->FindString("alias", &alias);
	message->FindString("model", &model);
	message->FindString("fingerprint", &fingerprint);
	message->FindString("address", &address);
	message->FindString("typeName", &typeName);
	message->FindString("version", &version);
	message->FindString("protocol", &protocol);
	message->FindUInt16("port", &port);
	message->FindBool("download", &download);

	// Refresh in place when we already know this fingerprint, so
	// the periodic re-announces do not drop the user's selection.
	int32 count = fDeviceList->CountItems();
	for (int32 i = 0; i < count; i++) {
		DeviceListItem* item
			= static_cast<DeviceListItem*>(fDeviceList->ItemAt(i));
		if (item->Device().fingerprint == fingerprint.String()) {
			item->UpdateDevice(alias, typeName, model, fingerprint,
				address, port, protocol, download, version);
			fDeviceList->InvalidateItem(i);

			char status[128];
			snprintf(status, sizeof(status), "%d device(s) found",
				fDeviceList->CountItems());
			_UpdateStatus(BString(status));
			return;
		}
	}

	DeviceListItem* item = new DeviceListItem(alias, typeName, model,
		fingerprint, address, port, protocol, download, version);
	fDeviceList->AddItem(item);

	char status[128];
	snprintf(status, sizeof(status), "%d device(s) found",
		fDeviceList->CountItems());
	_UpdateStatus(BString(status));
}


void
LocalSendWindow::_HandleIncomingPrompt(BMessage* message)
{
	BString alias;
	message->FindString("alias", &alias);

	BStringList files;
	const char* file;
	for (int32 i = 0;
		message->FindString("file", i, &file) == B_OK; i++)
		files.Add(file);

	ReceivePromptWindow* prompt = new ReceivePromptWindow(BRect(0, 0, 420,
		0), alias, files, BMessenger(this));
	prompt->CenterIn(this->Frame());
	prompt->Show();
}


void
LocalSendWindow::_HandleProgress(BMessage* message)
{
	int64 sent = 0;
	int64 total = 0;
	BString peer;
	BString file;
	message->FindInt64("sent", &sent);
	message->FindInt64("total", &total);
	message->FindString("peer", &peer);
	message->FindString("file", &file);

	float fraction = 0;
	if (message->FindFloat("fraction", &fraction) == B_OK && fraction > 0) {
		fProgress->SetMaxValue(1.0);
		fProgress->Update(fraction - fLastFraction, file.String());
		fLastFraction = fraction;
	}

	BString statusText = "Sending ";
	statusText << file << " to " << peer;
	_UpdateStatus(statusText);
}


void
LocalSendWindow::_HandleDone(BMessage* message)
{
	bool success = false;
	BString peer;
	int32 files = 0;
	int64 bytes = 0;
	message->FindBool("success", &success);
	message->FindString("peer", &peer);
	message->FindInt32("files", &files);
	message->FindInt64("bytes", &bytes);

	BString text;
	if (success) {
		text << "Sent " << files << " file(s) (" << bytes
			<< " bytes) to " << peer;
	} else {
		text = "Transfer to ";
		text << peer << " failed.";
	}
	_UpdateStatus(text);

	if (success) {
		BNotification notification(B_INFORMATION_NOTIFICATION);
		notification.SetTitle("LocalSend");
		notification.SetContent(text);
		notification.Send();
	}
	fProgress->Reset();
}


void
LocalSendWindow::_SendToSelected()
{
	int32 index = fDeviceList->CurrentSelection();
	if (index < 0)
		return;

	DeviceListItem* item
		= static_cast<DeviceListItem*>(fDeviceList->ItemAt(index));

	// Files may come from the picker or from a Tracker drop.
	if (fPendingFiles.empty()) {
		_PickFiles();
		return;
	}

	fManager->SendFiles(fPendingFiles, item->Address(), item->Device());
	fPendingFiles.clear();
	fProgress->Reset();
	fLastFraction = 0;
	BString statusText("Sending to ");
	statusText << item->Alias() << "...";
	_UpdateStatus(statusText);
}


void
LocalSendWindow::_PickFiles()
{
	if (fFilePanel == NULL) {
		BMessage* panelMessage = new BMessage(kMsgFilePanelClosed);
		fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), NULL,
			B_FILE_NODE, true, panelMessage);
	}
	fFilePanel->Show();
}


void
LocalSendWindow::_ShowSettings()
{
	SettingsWindow* window = new SettingsWindow(BRect(0, 0, 480, 380),
		fSettings, fManager->OurDevice());
	window->CenterIn(Frame());
	window->Show();
}


void
LocalSendWindow::_UpdateStatus(const BString& text)
{
	fStatus->SetText(text.String());
}
