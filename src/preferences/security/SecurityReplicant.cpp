/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "SecurityReplicant.h"

#include "FirewallBackend.h"
#include "FirewallRunner.h"
#include "FirewallService.h"
#include "SecurityMessages.h"

#include <Catalog.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Shape.h>
#include <Window.h>

#include <stdio.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Security"


const char* kSecurityReplicantName = "Security";
const char* kSecuritySignature = "application/x-vnd.Vitruvian-Security";

static const bigtime_t kPollInterval = 30000000;


extern "C" _EXPORT BView* instantiate_deskbar_item(float maxWidth,
	float maxHeight);


SecurityReplicant::SecurityReplicant(BRect frame)
	:
	BView(frame, kSecurityReplicantName, B_FOLLOW_ALL,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND),
	fRunner(NULL),
	fWorking(false),
	fPending(false)
{
	_Init();
}


SecurityReplicant::SecurityReplicant(BMessage* archive)
	:
	BView(archive),
	fRunner(NULL),
	fWorking(false),
	fPending(false)
{
	_Init();
}


SecurityReplicant::~SecurityReplicant()
{
	delete fRunner;
}


void
SecurityReplicant::_Init()
{
	fStatus.firewallValid = false;
	fStatus.firewallEnabled = false;
	fStatus.profile = FIREWALL_PROFILE_HOME;
	SetToolTip("Firewall");
}


BArchivable*
SecurityReplicant::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "SecurityReplicant"))
		return NULL;

	return new SecurityReplicant(archive);
}


status_t
SecurityReplicant::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status == B_OK)
		status = archive->AddString("add_on", kSecuritySignature);
	if (status == B_OK)
		status = archive->AddString("class", "SecurityReplicant");
	return status;
}


void
SecurityReplicant::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewColor(B_TRANSPARENT_COLOR);
	fRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgReplicantPoll), kPollInterval);
	FirewallWatchStamp(BMessenger(this));
	_StartWorker(false);
}


void
SecurityReplicant::DetachedFromWindow()
{
	FirewallStopWatchingStamp(BMessenger(this));
	delete fRunner;
	fRunner = NULL;
	BView::DetachedFromWindow();
}


void
SecurityReplicant::Draw(BRect updateRect)
{
	BRect bounds = Bounds().InsetByCopy(2, 1);

	rgb_color fill;
	if (!fStatus.firewallValid)
		fill = { 200, 170, 40, 255 };
	else if (fStatus.firewallEnabled)
		fill = { 40, 160, 70, 255 };
	else
		fill = { 130, 130, 130, 255 };

	// Shield: flat shoulders, tapered point.
	BShape shield;
	float x = bounds.left;
	float y = bounds.top;
	float width = bounds.Width();
	float height = bounds.Height();
	shield.MoveTo(BPoint(x + width * 0.5f, y));
	shield.LineTo(BPoint(x + width, y + height * 0.22f));
	shield.LineTo(BPoint(x + width, y + height * 0.58f));
	shield.LineTo(BPoint(x + width * 0.5f, y + height));
	shield.LineTo(BPoint(x, y + height * 0.58f));
	shield.LineTo(BPoint(x, y + height * 0.22f));
	shield.Close();

	SetHighColor(fill);
	FillShape(&shield);
}


void
SecurityReplicant::MouseDown(BPoint where)
{
	if (Window() == NULL)
		return;

	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	Window()->CurrentMessage()->FindInt32("buttons", &buttons);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
		be_roster->Launch(kSecuritySignature);
		return;
	}

	BPopUpMenu* menu = _BuildMenu();
	menu->Go(ConvertToScreen(where), true, false, true);
}


static BMessage*
_ProfileMessage(int32 profile)
{
	BMessage* message = new BMessage(kMsgReplicantProfile);
	message->AddInt32("profile", profile);
	return message;
}


BPopUpMenu*
SecurityReplicant::_BuildMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("securityMenu", false, false);

	BString label(B_TRANSLATE("Firewall unavailable"));
	if (fStatus.firewallValid) {
		label = fStatus.firewallEnabled ? B_TRANSLATE("Turn firewall off")
			: B_TRANSLATE("Turn firewall on");
	}

	BMenuItem* toggle = new BMenuItem(label.String(),
		new BMessage(kMsgReplicantToggle));
	toggle->SetEnabled(fStatus.firewallValid && !fWorking);
	menu->AddItem(toggle);
	menu->AddSeparatorItem();
	bool known = fStatus.firewallValid && !fWorking;
	BMenuItem* home = new BMenuItem(B_TRANSLATE("Home"), _ProfileMessage(
		FIREWALL_PROFILE_HOME));
	home->SetMarked(fStatus.profile != FIREWALL_PROFILE_PUBLIC);
	home->SetEnabled(known);
	menu->AddItem(home);
	BMenuItem* pub = new BMenuItem(B_TRANSLATE("Public"), _ProfileMessage(
		FIREWALL_PROFILE_PUBLIC));
	pub->SetMarked(fStatus.profile == FIREWALL_PROFILE_PUBLIC);
	pub->SetEnabled(known);
	menu->AddItem(pub);
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Open Security" B_UTF8_ELLIPSIS),
		new BMessage(kMsgOpenSecurity)));
	menu->SetTargetForItems(this);
	return menu;
}


void
SecurityReplicant::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgReplicantState:
		{
			SecurityStatus status;
			status.firewallValid = message->FindBool("valid");
			status.firewallEnabled = message->FindBool("enabled");
			fWorking = false;
			status.profile = message->GetInt32("profile",
				FIREWALL_PROFILE_HOME);
			fBackend = message->GetString("backend", "");
			_UpdateStatus(status);
			if (fPending) {
				fPending = false;
				_StartWorker(false);
			}
			break;
		}

		case kMsgReplicantPoll:
			_StartWorker(false);
			break;

		case B_NODE_MONITOR:
			FirewallWatchStamp(BMessenger(this));
			if (fWorking)
				fPending = true;
			else
				_StartWorker(false);
			break;

		case kMsgReplicantProfile:
		{
			int32 profile = FIREWALL_PROFILE_HOME;
			message->FindInt32("profile", &profile);
			_StartWorker(false, profile);
			break;
		}

		case kMsgReplicantToggle:
			_StartWorker(true);
			break;

		case kMsgOpenSecurity:
			be_roster->Launch(kSecuritySignature);
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
SecurityReplicant::_UpdateStatus(const SecurityStatus& status)
{
	fStatus = status;

	BString tip(B_TRANSLATE("Firewall: unknown"));
	if (fStatus.firewallValid) {
		tip = fStatus.firewallEnabled ? B_TRANSLATE("Firewall: on")
			: B_TRANSLATE("Firewall: off");
		if (fStatus.firewallEnabled) {
			tip << " (" << (fStatus.profile == FIREWALL_PROFILE_PUBLIC
				? B_TRANSLATE("Public") : B_TRANSLATE("Home")) << ")";
		}
	}
	SetToolTip(tip.String());
	Invalidate();
}


void
SecurityReplicant::_StartWorker(bool toggle, int32 setProfile)
{
	if (fWorking)
		return;

	WorkerArgs* args = new WorkerArgs;
	args->target = BMessenger(this);
	args->toggle = toggle;
	args->setProfile = setProfile;
	args->prefer = fBackend;

	thread_id thread = spawn_thread(_WorkerEntry, "firewall status",
		B_LOW_PRIORITY, args);
	if (thread < 0) {
		delete args;
		return;
	}
	fWorking = true;
	resume_thread(thread);
}


// The worker only holds a messenger, so the Deskbar can drop the item at
// any time.
int32
SecurityReplicant::_WorkerEntry(void* data)
{
	WorkerArgs* args = static_cast<WorkerArgs*>(data);

	bool valid = false;
	bool enabled = false;

	// The stamp carries the profile the shield and the preflet share;
	// the settings file keeps the list and the profile across boots.
	FirewallState saved;
	bool seeded = false;
	FirewallLoadSettings(saved, seeded);
	bool stampEnabled;
	int32 profile = saved.profile;
	FirewallReadStamp(stampEnabled, profile);
	if (args->setProfile >= 0)
		profile = args->setProfile;

	PrivilegedFirewallRunner runner;
	FirewallBackend* backend = create_firewall_backend(&runner,
		args->prefer.String());
	BString name;
	if (backend != NULL) {
		name = backend->Name();
		FirewallState state;
		if (backend->FetchState(state) == B_OK) {
			bool changed = args->toggle || args->setProfile >= 0;
			if (args->toggle)
				state.enabled = !state.enabled;
			state.profile = profile;
			if (changed) {
				// Home restores the kept list; the system carries none
				// while the firewall is off or Public.
				if (state.enabled && state.rules.empty())
					state.rules = saved.rules;
				if (state.enabled)
					state.inPolicy = FIREWALL_POLICY_DROP;
				FirewallState applied = state;
				FirewallEffectiveState(applied);
				BString error;
				bool ok = backend->ApplyState(applied, error) == B_OK;
				if (ok) {
					FirewallPublishState(&runner, state);
					saved.profile = profile;
					FirewallSaveSettings(saved, seeded);
				}
				FirewallState readBack;
				if (backend->FetchState(readBack) == B_OK)
					state.enabled = readBack.enabled;
				else if (!ok)
					state.enabled = !state.enabled;
			}
			valid = true;
			enabled = state.enabled;
		}
		delete backend;
	}

	BMessage message(kMsgReplicantState);
	message.AddBool("valid", valid);
	message.AddBool("enabled", enabled);
	message.AddInt32("profile", profile);
	message.AddString("backend", name.String());
	args->target.SendMessage(&message);
	delete args;
	return 0;
}


extern "C" BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	float size = maxHeight < maxWidth ? maxHeight : maxWidth;
	return new SecurityReplicant(BRect(0, 0, size - 1, size - 1));
}
