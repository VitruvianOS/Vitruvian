/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NetworkSharesWindow.h"

#include "AddShareDialog.h"
#include "ShareListItem.h"
#include "ShareWorker.h"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <List.h>
#include <ListItem.h>
#include <ListView.h>
#include <Message.h>
#include <Path.h>
#include <Roster.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <InterfaceDefs.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <vector>

#include "ShareCore.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NetworkShares"


static const char* kShareDir = "/etc/vos/network-shares";


NetworkSharesWindow::NetworkSharesWindow()
	:
	BWindow(BRect(80, 80, 720, 480), B_TRANSLATE("Network shares"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
	fHelperBusy(false)
{
	fListView = new BListView("shares", B_SINGLE_SELECTION_LIST);
	fListView->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
	fListView->SetInvocationMessage(new BMessage(kMsgMount));
	fScrollView = new BScrollView("share_scroll", fListView, 0, false, true);

	fAddButton = new BButton("add", B_TRANSLATE("Add" B_UTF8_ELLIPSIS),
		new BMessage(kMsgAdd));
	fEditButton = new BButton("edit", B_TRANSLATE("Edit" B_UTF8_ELLIPSIS),
		new BMessage(kMsgEdit));
	fRemoveButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRemove));
	fMountButton = new BButton("mount", B_TRANSLATE("Mount now"),
		new BMessage(kMsgMount));
	fUnmountButton = new BButton("unmount", B_TRANSLATE("Unmount now"),
		new BMessage(kMsgUnmount));
	fRefreshButton = new BButton("refresh",
		B_TRANSLATE("Refresh" B_UTF8_ELLIPSIS), new BMessage(kMsgRefresh));

	fStatusView = new BStringView("status", B_TRANSLATE("No shares yet."));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fScrollView)
		.AddGroup(B_HORIZONTAL)
			.Add(fAddButton)
			.Add(fEditButton)
			.Add(fRemoveButton)
			.AddGlue()
			.Add(fMountButton)
			.Add(fUnmountButton)
			.Add(fRefreshButton)
		.End()
		.Add(fStatusView);

	CenterOnScreen();
	LoadShares();
}


void
NetworkSharesWindow::LoadShares()
{
	_RefreshList();
}


void
NetworkSharesWindow::_RefreshList()
{
	fListView->MakeEmpty();

	std::vector<ShareListEntry> entries;
	BDirectory dir(kShareDir);
	if (dir.InitCheck() == B_OK) {
		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			char name[B_FILE_NAME_LENGTH];
			if (entry.GetName(name) != B_OK)
				continue;
			size_t len = strlen(name);
			if (len < 6 || strcmp(name + len - 6, ".share") != 0)
				continue;
			std::string id(name, len - 6);
			if (!VosNetworkSharesValidId(id.c_str()))
				continue;

			BFile file(&entry, B_READ_ONLY);
			if (file.InitCheck() != B_OK)
				continue;
			off_t size = 0;
			file.GetSize(&size);
			if (size <= 0 || size > 64 * 1024)
				continue;
			std::string text((size_t)size, '\0');
			if (file.Read(&text[0], size) != size)
				continue;

			ShareListEntry item;
			std::string error;
			if (!VosNetworkSharesParseShareFile(text, &item.spec, &error))
				continue;

			struct stat st;
			item.mounted = stat(item.spec.mountPoint.c_str(), &st) == 0
				&& st.st_dev != 0;
			entries.push_back(item);
		}
	}

	for (size_t i = 0; i < entries.size(); i++)
		fListView->AddItem(new ShareListItem(entries[i]));

	BString statusText;
	statusText << entries.size() << B_TRANSLATE(" share(s). "
		"Passwords are stored in /etc/vos/network-shares, never in fstab.");
	fStatusView->SetText(statusText.String());
	UpdateButtons();
}


void
NetworkSharesWindow::UpdateButtons()
{
	bool hasSelection = fListView->CurrentSelection() >= 0;
	fEditButton->SetEnabled(hasSelection && !fHelperBusy);
	fRemoveButton->SetEnabled(hasSelection && !fHelperBusy);
	fMountButton->SetEnabled(hasSelection && !fHelperBusy);
	fUnmountButton->SetEnabled(hasSelection && !fHelperBusy);
	fAddButton->SetEnabled(!fHelperBusy);
	fRefreshButton->SetEnabled(!fHelperBusy);
}


bool
NetworkSharesWindow::_GetSelected(ShareListEntry* entry)
{
	int32 index = fListView->CurrentSelection();
	if (index < 0)
		return false;
	ShareListItem* item = (ShareListItem*)fListView->ItemAt(index);
	if (item == NULL)
		return false;
	*entry = item->Entry();
	return true;
}


void
NetworkSharesWindow::OpenAddDialog(const VosNetworkShareSpec* fill)
{
	VosNetworkShareSpec draft;
	if (fill != NULL)
		draft = *fill;
	AddShareDialog* dialog = new AddShareDialog(this, fill != NULL ? &draft : NULL);
	dialog->Show();
}


void
NetworkSharesWindow::_RunHelper(const char* command, const ShareListEntry& entry,
	const BString& extraArgs)
{
	BString args;
	BString stdinData;
	args << entry.spec.id.c_str() << " ";
	if (strcmp(command, "add") == 0 || strcmp(command, "edit") == 0) {
		args << VosNetworkSharesProtocolName(entry.spec.protocol) << " "
			<< entry.spec.server.c_str() << " "
			<< entry.spec.exportName.c_str() << " "
			<< entry.spec.mountPoint.c_str() << " "
			<< VosNetworkSharesModeName(entry.spec.mode) << " "
			<< entry.spec.options.c_str() << " "
			<< entry.spec.username.c_str() << " "
			<< entry.spec.domain.c_str();
		// SMB password goes on stdin; never on the helper argv.
		if (entry.spec.protocol == kShareProtocolSMB
				&& !entry.spec.password.empty())
			stdinData << entry.spec.password.c_str() << "\n";
	} else if (strcmp(command, "mount") == 0 || strcmp(command, "unmount") == 0
			|| strcmp(command, "remove") == 0 || strcmp(command, "set-mode") == 0) {
		args = entry.spec.id.c_str();
		if (strcmp(command, "set-mode") == 0 && extraArgs.Length() > 0)
			args << " " << extraArgs;
	}

	fHelperBusy = true;
	UpdateButtons();
	fStatusView->SetText(B_TRANSLATE("Working" B_UTF8_ELLIPSIS));
	ShareWorker::Post(command, args, BMessenger(this), stdinData);
}


void
NetworkSharesWindow::_ShowError(const BString& message)
{
	BAlert* alert = new BAlert(B_TRANSLATE("Network shares"),
		message.String(), B_TRANSLATE("OK"));
	alert->Go();
}


void
NetworkSharesWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAdd:
			OpenAddDialog(NULL);
			break;

		case kMsgEdit:
		{
			ShareListEntry entry;
			if (_GetSelected(&entry)) {
				VosNetworkShareSpec fill = entry.spec;
				OpenAddDialog(&fill);
			}
			break;
		}

		case kMsgRemove:
		{
			ShareListEntry entry;
			if (!_GetSelected(&entry))
				break;
			_RunHelper("remove", entry);
			break;
		}

		case kMsgMount:
		{
			ShareListEntry entry;
			if (_GetSelected(&entry))
				_RunHelper("mount", entry);
			break;
		}

		case kMsgUnmount:
		{
			ShareListEntry entry;
			if (_GetSelected(&entry))
				_RunHelper("unmount", entry);
			break;
		}

		case kMsgRefresh:
			_RefreshList();
			break;

		case kMsgOpenAdd:
			OpenAddDialog(NULL);
			break;

		case kMsgSelectionChanged:
			UpdateButtons();
			break;

		case kMsgHelperDone:
		{
			fHelperBusy = false;
			int32 status = B_OK;
			message->FindInt32("status", &status);

			bool dialogOk = false;
			if (message->FindBool("dialogOk", &dialogOk) == B_OK && dialogOk) {
				ShareListEntry entry;
				const char* value = NULL;
				message->FindString("protocol", &value);
				bool ok = false;
				entry.spec.protocol = VosNetworkSharesParseProtocol(value, &ok);
				message->FindString("server", &value);
				entry.spec.server = value != NULL ? value : "";
				message->FindString("export", &value);
				entry.spec.exportName = value != NULL ? value : "";
				message->FindString("mountpoint", &value);
				entry.spec.mountPoint = value != NULL ? value : "";
				message->FindString("mode", &value);
				entry.spec.mode = VosNetworkSharesParseMode(value, &ok);
				message->FindString("options", &value);
				entry.spec.options = value != NULL ? value : "";
				message->FindString("username", &value);
				entry.spec.username = value != NULL ? value : "";
				message->FindString("password", &value);
				entry.spec.password = value != NULL ? value : "";
				message->FindString("domain", &value);
				entry.spec.domain = value != NULL ? value : "";

				std::string leaf = entry.spec.exportName;
				size_t slash = leaf.find_last_of('/');
				if (slash != std::string::npos)
					leaf = leaf.substr(slash + 1);
				std::string id = entry.spec.server + "_" + leaf;
				std::string cleaned;
				for (size_t i = 0; i < id.size(); i++) {
					char c = id[i];
					if (std::isalnum((unsigned char)c) || c == '_' || c == '-')
						cleaned += c;
				}
				if (cleaned.empty() || !std::isalnum((unsigned char)cleaned[0]))
					cleaned = "share" + cleaned;
				if (cleaned.size() > 64)
					cleaned.resize(64);
				if (!std::isalnum((unsigned char)cleaned[0])) {
					std::string prefix = "share";
					prefix += cleaned;
					cleaned = prefix.substr(0, 64);
				}
				entry.spec.id = cleaned;

				const char* cmd = "add";
				struct stat st;
				std::string sharePath = std::string(kShareDir) + "/"
					+ entry.spec.id + ".share";
				if (stat(sharePath.c_str(), &st) == 0)
					cmd = "edit";
				_RunHelper(cmd, entry);
				break;
			}

			if (status != B_OK) {
				BString text(B_TRANSLATE("The helper did not finish. "
					"Check the polkit prompt and the server address."));
				_ShowError(text);
			}
			_RefreshList();
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
NetworkSharesWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
