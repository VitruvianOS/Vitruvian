/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "ExtensionsWindow.h"
#include "NetPlusApp.h"
#include "NetPlusMessages.h"

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Path.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>

#include <string>
#include <vector>

#include <web/WebExtensions.h>


static ExtensionsWindow* sInstance = NULL;


/* static */
void
ExtensionsWindow::ShowWindow(BRect frame)
{
	if (sInstance == NULL) {
		sInstance = new ExtensionsWindow(frame);
		sInstance->Show();
	} else if (sInstance->IsHidden()) {
		sInstance->Show();
	} else {
		sInstance->Activate();
	}
}


ExtensionsWindow::ExtensionsWindow(BRect frame)
	:
	BWindow(frame, "NetPlus Extensions", B_TITLED_WINDOW,
		B_NOT_MINIMIZABLE),
	fScroll(NULL),
	fList(NULL),
	fInstallButton(NULL),
	fInstallPanel(NULL)
{
	fList = new BView("list", B_WILL_DRAW);
	fList->SetLayout(new BGroupLayout(B_VERTICAL));
	fList->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fScroll = new BScrollView("scroll", fList, B_FRAME_EVENTS
		| B_WILL_DRAW, false, true);
	fInstallButton = new BButton("Install From Folder" B_UTF8_ELLIPSIS,
		new BMessage(MSG_EXTENSION_INSTALL));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fScroll, 1.0f)
		.Add(fInstallButton);

	_RebuildList();
}


ExtensionsWindow::~ExtensionsWindow()
{
	sInstance = NULL;
	delete fInstallPanel;
}


bool
ExtensionsWindow::QuitRequested()
{
	sInstance = NULL;
	BWindow::QuitRequested();
	return true;
}


void
ExtensionsWindow::_AddRow(const WebExtensionInfo& info)
{
	BString label(info.name.c_str());
	if (!info.version.empty())
		label << " " << info.version.c_str();
	BCheckBox* check = new BCheckBox(label.String(),
		new BMessage(MSG_EXTENSION_TOGGLE));
	check->SetValue(info.enabled);
	check->Message()->AddString("id", info.id.c_str());
	if (!info.available)
		check->SetEnabled(false);

	BString meta(info.kind.c_str());
	if (!info.licence.empty())
		meta << ", " << info.licence.c_str();
	if (info.userOwned)
		meta << ", user";
	if (!info.available)
		meta << ", unavailable";
	BStringView* details = new BStringView("details", meta.String());

	BView* row = new BView("row", B_WILL_DRAW);
	if (info.userOwned) {
		BButton* remove = new BButton("Remove",
			new BMessage(MSG_EXTENSION_REMOVE));
		remove->Message()->AddString("id", info.id.c_str());
		BLayoutBuilder::Group<>(row, B_HORIZONTAL)
			.Add(check)
			.AddGlue()
			.Add(details)
			.Add(remove);
	} else
		BLayoutBuilder::Group<>(row, B_HORIZONTAL)
			.Add(check)
			.AddGlue()
			.Add(details);

	fList->AddChild(row);
}


void
ExtensionsWindow::_RebuildList()
{
	BWebExtensions* extensions = NetPlusApp::Extensions();
	while (BView* child = fList->ChildAt((int32)0))
		fList->RemoveChild(child);

	if (extensions == NULL)
		return;
	std::vector<WebExtensionInfo> list = extensions->List();
	if (list.empty()) {
		fList->AddChild(new BStringView("empty",
			"No extensions installed"));
		return;
	}
	for (const auto& info : list)
		_AddRow(info);
}


void
ExtensionsWindow::MessageReceived(BMessage* message)
{
	BWebExtensions* extensions = NetPlusApp::Extensions();
	switch (message->what) {
		case MSG_EXTENSIONS_CHANGED:
			_RebuildList();
			break;

		case MSG_EXTENSION_TOGGLE:
		{
			const char* id = NULL;
			if (extensions == NULL
				|| message->FindString("id", &id) != B_OK)
				break;
			int32 value = 0;
			message->FindInt32("be:value", &value);
			extensions->SetEnabled(id, value != 0);
			break;
		}

		case MSG_EXTENSION_REMOVE:
		{
			const char* id = NULL;
			if (extensions == NULL
				|| message->FindString("id", &id) != B_OK)
				break;
			std::string error;
			if (extensions->RemoveUserExtension(id, &error) != B_OK) {
				BAlert* alert = new BAlert("Remove Extension",
					error.c_str(), "OK");
				alert->Go();
			}
			break;
		}

		case MSG_EXTENSION_INSTALL:
			delete fInstallPanel;
			{
				BMessage panelMessage(MSG_INSTALL_PANEL_DONE);
				fInstallPanel = new BFilePanel(B_OPEN_PANEL,
					new BMessenger(this), NULL, 0, false,
					&panelMessage);
			}
			fInstallPanel->Show();
			break;

		case MSG_INSTALL_PANEL_DONE:
		{
			delete fInstallPanel;
			fInstallPanel = NULL;
			entry_ref ref;
			if (extensions == NULL
				|| message->FindRef("refs", &ref) != B_OK)
				break;
			BPath path(&ref);
			if (path.InitCheck() != B_OK)
				break;
			std::string error;
			if (extensions->InstallFromFolder(path.Path(), &error)
				!= B_OK) {
				BAlert* alert = new BAlert("Install Extension",
					error.c_str(), "OK");
				alert->Go();
			}
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
