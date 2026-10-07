/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "JoinWiFiWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <TextControl.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "JoinWiFiWindow"


static const uint32 kMsgConnect = 'jcon';
static const uint32 kMsgChanged = 'jchg';


JoinWiFiWindow::JoinWiFiWindow(const BMessenger& target,
	const BMessage& adapters, const char* selectedDevice)
	:
	BWindow(BRect(0, 0, 360, 10), B_TRANSLATE("Join other network"),
		B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE),
	fTarget(target),
	fAdapterMenu(NULL)
{
	fSSID = new BTextControl(B_TRANSLATE("Network name:"), NULL,
		new BMessage(kMsgChanged));
	fSSID->SetModificationMessage(new BMessage(kMsgChanged));

	static const struct { const char* label; const char* security; }
		kSecurity[] = {
			{ B_TRANSLATE_MARK("WPA/WPA2 Personal"), "wpa" },
			{ B_TRANSLATE_MARK("WPA3 Personal"), "sae" },
			{ B_TRANSLATE_MARK("WEP"), "wep" },
			{ B_TRANSLATE_MARK("None"), "none" },
		};
	fSecurityMenu = new BPopUpMenu("security");
	for (size_t i = 0; i < B_COUNT_OF(kSecurity); i++) {
		BMessage* message = new BMessage(kMsgChanged);
		message->AddString("security", kSecurity[i].security);
		fSecurityMenu->AddItem(new BMenuItem(
			B_TRANSLATE_NOCOLLECT(kSecurity[i].label), message));
	}
	fSecurityMenu->ItemAt(0)->SetMarked(true);
	BMenuField* securityField = new BMenuField(B_TRANSLATE("Security:"),
		fSecurityMenu);

	fPassword = new BTextControl(B_TRANSLATE("Password:"), NULL,
		new BMessage(kMsgChanged));
	fPassword->TextView()->HideTyping(true);
	fPassword->SetModificationMessage(new BMessage(kMsgChanged));

	BLayoutBuilder::Grid<> grid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING);
	grid.AddTextControl(fSSID, 0, 0)
		.AddMenuField(securityField, 0, 1)
		.AddTextControl(fPassword, 0, 2);

	BString path;
	if (adapters.FindString("path", 1, &path) == B_OK) {
		fAdapterMenu = new BPopUpMenu("adapter");
		BString name;
		for (int32 i = 0; adapters.FindString("path", i, &path) == B_OK; i++) {
			adapters.FindString("name", i, &name);
			BMessage* message = new BMessage(kMsgChanged);
			message->AddString("device", path);
			BMenuItem* item = new BMenuItem(name, message);
			fAdapterMenu->AddItem(item);
			if (selectedDevice != NULL && path == selectedDevice)
				item->SetMarked(true);
		}
		if (fAdapterMenu->FindMarked() == NULL)
			fAdapterMenu->ItemAt(0)->SetMarked(true);
		grid.AddMenuField(new BMenuField(B_TRANSLATE("Adapter:"),
			fAdapterMenu), 0, 3);
	} else
		adapters.FindString("path", 0, &fDevice);

	fRemember = new BCheckBox(B_TRANSLATE("Connect automatically"));
	fRemember->SetValue(B_CONTROL_ON);

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	fConnect = new BButton(B_TRANSLATE("Connect"), new BMessage(kMsgConnect));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(grid.View())
		.Add(fRemember)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(fConnect)
		.End();

	SetDefaultButton(fConnect);
	fSSID->MakeFocus(true);
	_UpdateControls();
	CenterOnScreen();
}


void
JoinWiFiWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgChanged:
			_UpdateControls();
			break;

		case kMsgConnect:
		{
			BMessage request(kMsgJoinHiddenWiFi);
			request.AddString("device", _Device());
			request.AddString("ssid", fSSID->Text());
			request.AddString("password", fPassword->IsEnabled()
				? fPassword->Text() : "");
			request.AddString("security", _Security());
			request.AddBool("remember", fRemember->Value() == B_CONTROL_ON);
			fTarget.SendMessage(&request);
			Quit();
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


void
JoinWiFiWindow::_UpdateControls()
{
	bool open = strcmp(_Security(), "none") == 0;
	fPassword->SetEnabled(!open);
	fConnect->SetEnabled(fSSID->Text()[0] != '\0' && _Device()[0] != '\0'
		&& (open || fPassword->Text()[0] != '\0'));
}


const char*
JoinWiFiWindow::_Device() const
{
	BMenuItem* item = fAdapterMenu != NULL ? fAdapterMenu->FindMarked() : NULL;
	return item != NULL ? item->Message()->GetString("device", "")
		: fDevice.String();
}


const char*
JoinWiFiWindow::_Security() const
{
	BMenuItem* item = fSecurityMenu->FindMarked();
	return item != NULL ? item->Message()->GetString("security", "wpa")
		: "wpa";
}
