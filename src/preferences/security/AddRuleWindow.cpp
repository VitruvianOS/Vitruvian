/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AddRuleWindow.h"

#include "FirewallServices.h"
#include "FirewallService.h"
#include "SecurityMessages.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>

#include <stdlib.h>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Security"


static const uint32 kMsgServiceChanged = 'asvc';
static const uint32 kMsgAccept = 'aacc';


AddRuleWindow::AddRuleWindow(BWindow* parent, const BMessenger& target)
	:
	BWindow(BRect(0, 0, 10, 10), B_TRANSLATE("Allow incoming connections"),
		B_FLOATING_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fTarget(target)
{
	fServiceMenu = new BPopUpMenu("service");
	for (int32 i = 0; FirewallServiceAt(i) != NULL; i++) {
		const FirewallService* service = FirewallServiceAt(i);
		fServiceMenu->AddItem(new BMenuItem(
			B_TRANSLATE_NOCOLLECT(service->name),
			new BMessage(kMsgServiceChanged)));
	}
	fServiceMenu->AddSeparatorItem();
	fServiceMenu->AddItem(new BMenuItem(B_TRANSLATE("Custom port"),
		new BMessage(kMsgServiceChanged)));
	fServiceMenu->ItemAt(0)->SetMarked(true);
	fServiceMenu->SetTargetForItems(this);
	BMenuField* serviceField = new BMenuField("serviceField",
		B_TRANSLATE("Service:"), fServiceMenu);

	fProtocolMenu = new BPopUpMenu("protocol");
	fProtocolMenu->AddItem(new BMenuItem(B_TRANSLATE("TCP"), NULL));
	fProtocolMenu->AddItem(new BMenuItem(B_TRANSLATE("UDP"), NULL));
	fProtocolMenu->ItemAt(0)->SetMarked(true);
	fProtocolField = new BMenuField("protocolField",
		B_TRANSLATE("Protocol:"), fProtocolMenu);

	fPortControl = new BTextControl("port", B_TRANSLATE("Port:"), "", NULL);

	fAddButton = new BButton("add", B_TRANSLATE("Allow"),
		new BMessage(kMsgAccept));
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(serviceField->CreateLabelLayoutItem(), 0, 0)
			.Add(serviceField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(fProtocolField->CreateLabelLayoutItem(), 0, 1)
			.Add(fProtocolField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(fPortControl->CreateLabelLayoutItem(), 0, 2)
			.Add(fPortControl->CreateTextViewLayoutItem(), 1, 2)
		.End()
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(fAddButton)
		.End();

	fAddButton->MakeDefault(true);
	_UpdateCustom();
	AddToSubset(parent);
	CenterIn(parent->Frame());
	Show();
}


void
AddRuleWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgServiceChanged:
			_UpdateCustom();
			break;

		case kMsgAccept:
			_Accept();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
AddRuleWindow::_UpdateCustom()
{
	bool custom = fServiceMenu->IndexOf(fServiceMenu->FindMarked())
		> fServiceMenu->CountItems() - 2;
	fProtocolField->SetEnabled(custom);
	fPortControl->SetEnabled(custom);
}


void
AddRuleWindow::_Accept()
{
	int32 index = fServiceMenu->IndexOf(fServiceMenu->FindMarked());
	BMessage result(kMsgRulesChosen);

	const FirewallService* service = FirewallServiceAt(index);
	if (service != NULL) {
		for (int32 p = 0; p < 2 && service->ports[p] != 0; p++) {
			const char* protocols[] = { "tcp", "udp" };
			for (int32 i = 0; i < 2; i++) {
				if (strstr(service->protocols, protocols[i]) == NULL)
					continue;
				FirewallRule rule;
				rule.allow = true;
				rule.protocol = protocols[i];
				rule.port = service->ports[p];
				rule.label = service->label;
				FirewallPutRule(result, rule);
			}
		}
	} else {
		int32 port = atoi(fPortControl->Text());
		if (port < 1 || port > 65535) {
			fPortControl->MakeFocus(true);
			return;
		}
		FirewallRule rule;
		rule.allow = true;
		rule.protocol = fProtocolMenu->IndexOf(fProtocolMenu->FindMarked())
			== 1 ? "udp" : "tcp";
		rule.port = (uint16)port;
		result.AddInt32("custom", 1);
		FirewallPutRule(result, rule);
	}

	fTarget.SendMessage(&result);
	PostMessage(B_QUIT_REQUESTED);
}
