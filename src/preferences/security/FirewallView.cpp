/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "FirewallView.h"

#include "AddRuleWindow.h"
#include "FirewallBackend.h"
#include "FirewallService.h"
#include "FirewallServices.h"
#include "SecurityMessages.h"
#include "SecurityReplicant.h"

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <StringView.h>

#include <stdio.h>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Security"


// A list that says so when it is empty.
class RuleListView : public BListView {
public:
	RuleListView(const char* name)
		:
		BListView(name, B_SINGLE_SELECTION_LIST)
	{
	}

	virtual void Draw(BRect updateRect)
	{
		BListView::Draw(updateRect);
		if (CountItems() > 0)
			return;

		const char* text = B_TRANSLATE("No exceptions. All incoming "
			"connections are blocked.");
		BRect bounds = Bounds();
		font_height height;
		GetFontHeight(&height);
		SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);
		SetLowUIColor(ViewUIColor());
		DrawString(text, BPoint((bounds.Width() - StringWidth(text)) / 2,
			(bounds.Height() + height.ascent - height.descent) / 2));
	}
};


static FirewallState
_DefaultsState()
{
	FirewallState state;
	state.enabled = false;
	state.inPolicy = FIREWALL_POLICY_DROP;
	state.outPolicy = FIREWALL_POLICY_ACCEPT;
	state.logging = false;
	return state;
}


static BString
_RuleText(const FirewallRule& rule)
{
	const FirewallService* service = FirewallServiceFor(rule.label.String());

	BString text;
	if (service != NULL)
		text = B_TRANSLATE_NOCOLLECT(service->name);
	else
		text = B_TRANSLATE("Port");

	char port[32];
	if (rule.port != 0 && rule.protocol.Length() > 0)
		snprintf(port, sizeof(port), "%u/%s", (unsigned)rule.port,
			rule.protocol.String());
	else
		snprintf(port, sizeof(port), "%u", (unsigned)rule.port);

	if (service == NULL)
		text << " " << port;
	else
		text << " (" << port << ")";

	if (!rule.allow) {
		BString blocked(B_TRANSLATE("Blocked:"));
		blocked << " " << text;
		return blocked;
	}
	return text;
}


FirewallView::FirewallView()
	:
	BView("firewall", B_WILL_DRAW),
	fPopulating(false),
	fBusy(false),
	fHasBackend(true),
	fSeeded(false),
	fFetchPending(false),
	fKeepStatus(false)
{
	fState = _DefaultsState();

	fStateLabel = new BStringView("state", "");
	fStateLabel->SetFont(be_bold_font);
	fHintLabel = new BStringView("hint", "");

	fEnabledCheck = new BCheckBox("enabled", B_TRANSLATE("Firewall on"),
		new BMessage(kMsgFirewallEnabled));

	fProfileMenu = new BPopUpMenu("profile");
	fProfileMenu->AddItem(new BMenuItem(B_TRANSLATE("Home"),
		new BMessage(kMsgProfile)));
	fProfileMenu->AddItem(new BMenuItem(B_TRANSLATE("Public"),
		new BMessage(kMsgProfile)));
	fProfileField = new BMenuField("profileField", B_TRANSLATE("Network:"),
		fProfileMenu);

	fRuleList = new RuleListView("rules");
	BScrollView* listScroll = new BScrollView("rulesScroll", fRuleList,
		B_FRAME_EVENTS | B_WILL_DRAW, false, true, B_FANCY_BORDER);
	float fontSize = be_plain_font->Size();
	fRuleList->SetExplicitMinSize(BSize(fontSize * 22, fontSize * 8));

	fAddButton = new BButton("add", B_TRANSLATE("Add" B_UTF8_ELLIPSIS),
		new BMessage(kMsgAddRule));
	fRemoveButton = new BButton("remove", B_TRANSLATE("Remove"),
		new BMessage(kMsgRuleRemove));

	fDeskbarCheck = new BCheckBox("deskbar",
		B_TRANSLATE("Show firewall status in the Deskbar"),
		new BMessage(kMsgDeskbar));

	fAdvancedButton = new BButton("advancedButton",
		B_TRANSLATE("Show advanced settings"), new BMessage(kMsgAdvanced));

	fOutPolicyMenu = new BPopUpMenu("outPolicy");
	fOutPolicyMenu->AddItem(new BMenuItem(B_TRANSLATE("Allow"),
		new BMessage(kMsgOutPolicy)));
	fOutPolicyMenu->AddItem(new BMenuItem(B_TRANSLATE("Block"),
		new BMessage(kMsgOutPolicy)));
	fOutPolicyField = new BMenuField("outField",
		B_TRANSLATE("Outgoing connections:"), fOutPolicyMenu);

	fLoggingCheck = new BCheckBox("logging",
		B_TRANSLATE("Log blocked connections"),
		new BMessage(kMsgLogging));
	fRestoreButton = new BButton("restore",
		B_TRANSLATE("Restore defaults"), new BMessage(kMsgRestoreDefaults));

	fBackendLabel = new BStringView("backend", "");
	fStatusLabel = new BStringView("status", "");

	fAdvancedGroup = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(fAdvancedGroup)
		.Add(fOutPolicyField)
		.Add(fLoggingCheck)
		.Add(fBackendLabel)
		.AddGroup(B_HORIZONTAL)
			.Add(fRestoreButton)
			.AddGlue()
		.End();
	fAdvancedGroup->Hide();

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL, 0)
				.Add(fStateLabel)
				.Add(fHintLabel)
			.End()
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fEnabledCheck)
			.AddGlue()
			.Add(fProfileField)
		.End()
		.Add(new BStringView("rulesLabel",
			B_TRANSLATE("Allowed incoming connections:")))
		.Add(listScroll, 1.0f)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fAddButton)
			.Add(fRemoveButton)
			.AddGlue()
		.End()
		.Add(fDeskbarCheck)
		.AddGroup(B_HORIZONTAL)
			.Add(fAdvancedButton)
			.AddGlue()
		.End()
		.Add(fAdvancedGroup)
		.Add(fStatusLabel);
}


FirewallView::~FirewallView()
{
}


void
FirewallView::AttachedToWindow()
{
	BView::AttachedToWindow();

	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fEnabledCheck->SetTarget(this);
	fProfileMenu->SetTargetForItems(this);
	fAddButton->SetTarget(this);
	fRemoveButton->SetTarget(this);
	fDeskbarCheck->SetTarget(this);
	fAdvancedButton->SetTarget(this);
	fOutPolicyMenu->SetTargetForItems(this);
	fLoggingCheck->SetTarget(this);
	fRestoreButton->SetTarget(this);

	_LoadSettings();
	_ReadStamp();
	_Populate(fState);
	FirewallWatchStamp(BMessenger(this));

	BDeskbar deskbar;
	fPopulating = true;
	fDeskbarCheck->SetValue(deskbar.HasItem(kSecurityReplicantName)
		? B_CONTROL_ON : B_CONTROL_OFF);
	fPopulating = false;

	_Submit(kFirewallOpFetch, fState);
}


void
FirewallView::DetachedFromWindow()
{
	FirewallStopWatchingStamp(BMessenger(this));
	BView::DetachedFromWindow();
}


void
FirewallView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kFirewallResultWhat:
			_ApplyResult(message);
			break;

		case B_NODE_MONITOR:
			// The shield or another preflet changed the firewall.
			FirewallWatchStamp(BMessenger(this));
			_ReadStamp();
			if (fBusy)
				fFetchPending = true;
			else
				_Submit(kFirewallOpFetch, fState);
			break;

		case kMsgProfile:
			if (!fPopulating && !fBusy) {
				fState.profile = fProfileMenu->IndexOf(
					fProfileMenu->FindMarked());
				_Submit(kFirewallOpApply, fState);
			}
			break;

		case kMsgFirewallEnabled:
			if (!fPopulating && !fBusy) {
				fState.enabled = fEnabledCheck->Value() == B_CONTROL_ON;
				if (fState.enabled) {
					fState.inPolicy = FIREWALL_POLICY_DROP;
					if (fState.rules.empty() && !fSeeded) {
						FirewallListeningRules(fState.rules);
						fSeeded = true;
					}
				}
				_Submit(kFirewallOpApply, fState);
			}
			break;

		case kMsgOutPolicy:
			if (!fPopulating && !fBusy) {
				int32 index = fOutPolicyMenu->IndexOf(
					fOutPolicyMenu->FindMarked());
				fState.outPolicy = index == 1 ? FIREWALL_POLICY_DROP
					: FIREWALL_POLICY_ACCEPT;
				_Submit(kFirewallOpApply, fState);
			}
			break;

		case kMsgLogging:
			if (!fPopulating && !fBusy) {
				fState.logging = fLoggingCheck->Value() == B_CONTROL_ON;
				_Submit(kFirewallOpApply, fState);
			}
			break;

		case kMsgAddRule:
			if (!fBusy)
				new AddRuleWindow(Window(), BMessenger(this));
			break;

		case kMsgRulesChosen:
			if (!fBusy)
				_AddRules(message);
			break;

		case kMsgRuleRemove:
			if (!fBusy && fRuleList->CurrentSelection() >= 0) {
				fState.rules.erase(fState.rules.begin()
					+ fRuleList->CurrentSelection());
				_Submit(kFirewallOpApply, fState);
			}
			break;

		case kMsgRestoreDefaults:
			if (!fBusy) {
				fSeeded = false;
				_Submit(kFirewallOpApply, _DefaultsState());
			}
			break;

		case kMsgAdvanced:
			_ToggleAdvanced();
			break;

		case kMsgDeskbar:
			if (!fPopulating)
				_ToggleDeskbar(fDeskbarCheck->Value() == B_CONTROL_ON);
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
FirewallView::_LoadSettings()
{
	FirewallLoadSettings(fState, fSeeded);
}


void
FirewallView::_SaveSettings()
{
	FirewallSaveSettings(fState, fSeeded);
}


// The stamp is the truth for the profile, since the shield can change it.
void
FirewallView::_ReadStamp()
{
	bool enabled;
	int32 profile;
	if (FirewallReadStamp(enabled, profile))
		fState.profile = profile;
}


void
FirewallView::_Populate(const FirewallState& state)
{
	// The system carries no list in Public; ours is kept for Home.
	std::vector<FirewallRule> kept = fState.rules;
	fState = state;
	if (state.profile == FIREWALL_PROFILE_PUBLIC)
		fState.rules = kept;

	fPopulating = true;
	fEnabledCheck->SetValue(state.enabled ? B_CONTROL_ON : B_CONTROL_OFF);
	fProfileMenu->ItemAt(state.profile == FIREWALL_PROFILE_PUBLIC ? 1 : 0)
		->SetMarked(true);
	fOutPolicyMenu->ItemAt(state.outPolicy == FIREWALL_POLICY_DROP ? 1 : 0)
		->SetMarked(true);
	fLoggingCheck->SetValue(state.logging ? B_CONTROL_ON : B_CONTROL_OFF);
	fPopulating = false;

	_RefreshRuleList();
	_UpdateHeader();
	_UpdateControls();
}


void
FirewallView::_Submit(int32 op, const FirewallState& state)
{
	BMessage args;
	args.AddInt32("op", op);
	args.AddString("prefer", fBackend.String());
	args.AddInt32("profile", state.profile);
	if (op == kFirewallOpApply)
		FirewallPutState(args, state);

	fBusy = true;
	_UpdateControls();
	if (op == kFirewallOpApply)
		fStatusLabel->SetText(B_TRANSLATE("Applying" B_UTF8_ELLIPSIS));
	else if (op == kFirewallOpFetch && !fKeepStatus)
		fStatusLabel->SetText(B_TRANSLATE("Reading state" B_UTF8_ELLIPSIS));
	FirewallAsyncRun(op, args, BMessenger(this));
}


void
FirewallView::_RefreshRuleList()
{
	fRuleList->MakeEmpty();
	for (size_t i = 0; i < fState.rules.size(); i++) {
		BStringItem* item = new BStringItem(_RuleText(fState.rules[i]));
		item->SetEnabled(fState.profile != FIREWALL_PROFILE_PUBLIC);
		fRuleList->AddItem(item);
	}
}


void
FirewallView::_UpdateControls()
{
	bool edit = !fBusy && fHasBackend;

	fEnabledCheck->SetEnabled(edit);
	fProfileField->SetEnabled(edit);
	bool home = fState.profile != FIREWALL_PROFILE_PUBLIC;
	fAddButton->SetEnabled(edit && home);
	fRemoveButton->SetEnabled(edit && home && fState.rules.size() > 0);
	fOutPolicyField->SetEnabled(edit);
	fLoggingCheck->SetEnabled(edit);
	fRestoreButton->SetEnabled(edit);
}


void
FirewallView::_UpdateHeader()
{
	if (!fHasBackend) {
		fStateLabel->SetText(B_TRANSLATE("Firewall unavailable"));
		fHintLabel->SetText(B_TRANSLATE("No firewall tool is installed."));
		return;
	}

	fStateLabel->SetText(fState.enabled ? B_TRANSLATE("Firewall is on")
		: B_TRANSLATE("Firewall is off"));

	if (!fState.enabled) {
		fHintLabel->SetText(B_TRANSLATE("Turn the firewall on to block "
			"incoming connections."));
	} else if (fState.profile == FIREWALL_PROFILE_PUBLIC) {
		fHintLabel->SetText(B_TRANSLATE("Public network: all incoming "
			"connections are blocked."));
	} else if (fState.inPolicy == FIREWALL_POLICY_DROP) {
		fHintLabel->SetText(B_TRANSLATE("Incoming connections are blocked, "
			"except the ones you allow below."));
	} else
		fHintLabel->SetText(B_TRANSLATE("All incoming connections are "
			"allowed."));
}


void
FirewallView::_ApplyResult(BMessage* message)
{
	int32 status = B_ERROR;
	message->FindInt32("status", &status);
	int32 op = kFirewallOpFetch;
	message->FindInt32("op", &op);

	fBusy = false;

	const char* error = NULL;
	message->FindString("error", &error);

	if (status != B_OK) {
		fKeepStatus = false;
		if (message->FindBool("noBackend")) {
			fHasBackend = false;
			_UpdateHeader();
			fStatusLabel->SetText("");
		} else {
			// A plain cancel stays silent; either way show the real state.
			if (message->FindBool("canceled"))
				fStatusLabel->SetText("");
			else {
				BString text(B_TRANSLATE("Firewall error"));
				if (error != NULL && error[0] != '\0')
					text << ": " << error;
				fStatusLabel->SetText(text.String());
			}
			if (op == kFirewallOpApply)
				fFetchPending = true;
		}
		_UpdateControls();
		if (fFetchPending && fHasBackend) {
			fFetchPending = false;
			fKeepStatus = true;
			_ReadStamp();
			_Submit(kFirewallOpFetch, fState);
		}
		return;
	}

	const char* backend = NULL;
	if (message->FindString("backend", &backend) == B_OK) {
		fBackend = backend;
		BString text(B_TRANSLATE("Using"));
		text << " " << backend << ".";
		fBackendLabel->SetText(text.String());
	}

	if (message->HasString("enabled")) {
		FirewallState state;
		FirewallGetState(*message, state);
		_Populate(state);
	} else {
		_UpdateHeader();
		_UpdateControls();
	}

	_SaveSettings();
	if (!fKeepStatus)
		fStatusLabel->SetText("");
	fKeepStatus = false;

	if (fFetchPending) {
		fFetchPending = false;
		_ReadStamp();
		_Submit(kFirewallOpFetch, fState);
	}
}


void
FirewallView::_ToggleDeskbar(bool show)
{
	BDeskbar deskbar;
	if (show) {
		entry_ref ref;
		status_t status = be_roster->FindApp(kSecuritySignature, &ref);
		if (status == B_OK)
			status = deskbar.AddItem(&ref);
		if (status != B_OK) {
			fPopulating = true;
			fDeskbarCheck->SetValue(B_CONTROL_OFF);
			fPopulating = false;
			fStatusLabel->SetText(B_TRANSLATE(
				"Could not add the Deskbar item."));
		}
	} else
		deskbar.RemoveItem(kSecurityReplicantName);
}


void
FirewallView::_ToggleAdvanced()
{
	bool show = fAdvancedGroup->IsHidden();
	if (show)
		fAdvancedGroup->Show();
	else
		fAdvancedGroup->Hide();
	fAdvancedButton->SetLabel(show ? B_TRANSLATE("Hide advanced settings")
		: B_TRANSLATE("Show advanced settings"));

	BWindow* window = Window();
	if (window == NULL)
		return;
	window->UpdateIfNeeded();
	BSize preferred = window->GetLayout()->PreferredSize();
	BRect bounds = window->Bounds();
	window->ResizeTo(max_c(bounds.Width(), preferred.width), preferred.height);
}


void
FirewallView::_AddRules(BMessage* message)
{
	bool custom = message->HasInt32("custom");
	int32 count = FirewallCountRules(*message);
	for (int32 i = 0; i < count; i++) {
		FirewallRule rule;
		if (!FirewallGetRule(*message, i, rule))
			continue;
		if (custom)
			rule.label = "";
		fState.rules.push_back(rule);
	}
	if (count > 0)
		_Submit(kFirewallOpApply, fState);
}
