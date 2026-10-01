/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PowerView.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#include <Box.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Deskbar.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>
#include <StringFormat.h>
#include <StringView.h>

#include "ACPIDriverInterface.h"
#include "APMDriverInterface.h"
#include "SysFSDriverInterface.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Power"


static const uint32 kMsgLogindActionChanged = 'lgAc';
static const uint32 kMsgHelperDone = 'hlDn';
static const uint32 kMsgUserSettingChanged = 'usCh';
static const uint32 kMsgDeskbarToggled = 'dbTg';

// Read by app_server, the registrar and PowerStatus.
static const char* kSettingsFile = "Power settings";
static const char* kDisplayOffKey = "power:display_off_minutes";
static const char* kBatteryCriticalKey = "power:battery_critical_action";
static const char* kNotificationsKey = "power:status_notifications";
static const int32 kDefaultDisplayOff = 10;
static const char* kDefaultBatteryCritical = "hibernate";

static const char* kHelper = "/usr/libexec/vos-set-power-actions";
static const char* kPowerStatusSignature = "application/x-vnd.Haiku-PowerStatus";
static const char* kPowerStatusItem = "PowerStatus";

// app_server reports the session idle this long after the last input;
// logind counts IdleActionSec from there.
static const int32 kIdleHintDelay = 30;

static const char* kLogin1 = "org.freedesktop.login1";
static const char* kLogin1Path = "/org/freedesktop/login1";
static const char* kLogin1Manager = "org.freedesktop.login1.Manager";


struct Choice {
	const char*	label;
	const char*	value;
};

static const Choice kKeyChoices[] = {
	{ B_TRANSLATE_MARK("Power off"), "poweroff" },
	{ B_TRANSLATE_MARK("Suspend"), "suspend" },
	{ B_TRANSLATE_MARK("Hibernate"), "hibernate" },
	{ B_TRANSLATE_MARK("Do nothing"), "ignore" },
	{ NULL, NULL }
};

static const Choice kSleepKeyChoices[] = {
	{ B_TRANSLATE_MARK("Suspend"), "suspend" },
	{ B_TRANSLATE_MARK("Hibernate"), "hibernate" },
	{ B_TRANSLATE_MARK("Do nothing"), "ignore" },
	{ NULL, NULL }
};

static const Choice kBatteryChoices[] = {
	{ B_TRANSLATE_MARK("Hibernate"), "hibernate" },
	{ B_TRANSLATE_MARK("Suspend"), "suspend" },
	{ B_TRANSLATE_MARK("Power off"), "poweroff" },
	{ B_TRANSLATE_MARK("Do nothing"), "ignore" },
	{ NULL, NULL }
};

static const int32 kIdleMinutes[] = { 0, 5, 10, 15, 30, 60, 120, -1 };
static const int32 kDisplayMinutes[] = { 0, 1, 2, 5, 10, 15, 30, 60, -1 };


struct HelperJob {
	BMessenger	target;
	BString		arguments[2];
	int32		count;
};


static BString
minutes_label(int32 minutes)
{
	if (minutes == 0)
		return B_TRANSLATE("Never");

	static BStringFormat sFormat(B_TRANSLATE(
		"{0, plural, one{# minute} other{# minutes}}"));
	static BStringFormat sHourFormat(B_TRANSLATE(
		"{0, plural, one{# hour} other{# hours}}"));
	BString label;
	if (minutes % 60 == 0)
		sHourFormat.Format(label, minutes / 60);
	else
		sFormat.Format(label, minutes);
	return label;
}


static BMenuField*
choice_menu(const char* name, const char* label, uint32 what,
	const char* key, const Choice* choices, bool canHibernate)
{
	BMenu* menu = new BMenu(name);
	menu->SetLabelFromMarked(true);
	for (int32 i = 0; choices[i].label != NULL; i++) {
		if (!canHibernate && strcmp(choices[i].value, "hibernate") == 0)
			continue;
		BMessage* message = new BMessage(what);
		if (key != NULL)
			message->AddString("key", key);
		message->AddString("value", choices[i].value);
		menu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(choices[i].label),
			message));
	}
	return new BMenuField(name, label, menu);
}


static BMenuField*
minutes_menu(const char* name, const char* label, uint32 what,
	const char* key, const int32* minutes)
{
	BMenu* menu = new BMenu(name);
	menu->SetLabelFromMarked(true);
	for (int32 i = 0; minutes[i] >= 0; i++) {
		BMessage* message = new BMessage(what);
		if (key != NULL)
			message->AddString("key", key);
		message->AddInt32("minutes", minutes[i]);
		menu->AddItem(new BMenuItem(minutes_label(minutes[i]), message));
	}
	return new BMenuField(name, label, menu);
}


// Drops the item mark_value() adds for a value the menu does not offer.
static void
remove_other(BMenu* menu)
{
	for (int32 i = menu->CountItems() - 1; i >= 0; i--) {
		if (menu->ItemAt(i)->Message() == NULL)
			delete menu->RemoveItem(i);
	}
}


// Marks the item carrying this value. A value the menu does not offer
// (set outside V\OS, e.g. "lock") shows as a disabled item of its own.
static void
mark_value(BMenuField* field, const char* value)
{
	BMenu* menu = field->Menu();
	remove_other(menu);
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (strcmp(item->Message()->GetString("value", ""), value) == 0) {
			item->SetMarked(true);
			return;
		}
	}
	BMenuItem* other = new BMenuItem(value, NULL);
	other->SetEnabled(false);
	menu->AddItem(other);
	other->SetMarked(true);
}


static void
mark_minutes(BMenuField* field, int32 minutes)
{
	BMenu* menu = field->Menu();
	remove_other(menu);
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Message()->GetInt32("minutes", -1) == minutes) {
			item->SetMarked(true);
			return;
		}
	}
}


static BBox*
titled_box(const char* name, const char* label)
{
	BBox* box = new BBox(name);
	box->SetLabel(label);
	return box;
}


//	#pragma mark - PowerView


PowerView::PowerView()
	:
	BView("Power", B_WILL_DRAW),
	fDriverInterface(NULL),
	fBatteryStateLabel(NULL),
	fBatteryPercentLabel(NULL),
	fBatteryTimeLabel(NULL),
	fPowerKeyMenu(NULL),
	fSuspendKeyMenu(NULL),
	fHibernateKeyMenu(NULL),
	fLidMenu(NULL),
	fIdleMenu(NULL),
	fDisplayOffMenu(NULL),
	fBatteryCriticalMenu(NULL),
	fNotificationsCheckBox(NULL),
	fDeskbarCheckBox(NULL),
	fCanHibernate(false)
{
}


PowerView::~PowerView()
{
	if (fDriverInterface != NULL) {
		fDriverInterface->StopWatching(this);
		fDriverInterface->Disconnect();
		fDriverInterface->ReleaseReference();
	}
}


PowerView*
PowerView::Create()
{
	PowerView* view = new PowerView();
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Hibernate entries only where it can work (swap, resume device).
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) >= 0) {
		sd_bus_message* reply = NULL;
		const char* answer = NULL;
		if (sd_bus_call_method(bus, kLogin1, kLogin1Path, kLogin1Manager,
				"CanHibernate", NULL, &reply, "") >= 0
			&& sd_bus_message_read(reply, "s", &answer) >= 0
			&& answer != NULL)
			view->fCanHibernate = strcmp(answer, "yes") == 0
				|| strcmp(answer, "challenge") == 0;
		sd_bus_message_unref(reply);
		sd_bus_flush_close_unref(bus);
	}

	BBox* batteryBox = titled_box("battery", B_TRANSLATE("Battery"));
	BLayoutBuilder::Group<>(batteryBox, B_VERTICAL, 0)
		.Add(view->fBatteryStateLabel = new BStringView("state",
			B_TRANSLATE("State: detecting" B_UTF8_ELLIPSIS)))
		.Add(view->fBatteryPercentLabel = new BStringView("percent",
			B_TRANSLATE("Charge: --")))
		.Add(view->fBatteryTimeLabel = new BStringView("time",
			B_TRANSLATE("Time remaining: --")))
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	view->fPowerKeyMenu = choice_menu("power_key",
		B_TRANSLATE("Power button:"), kMsgLogindActionChanged,
		"HandlePowerKey", kKeyChoices, view->fCanHibernate);
	view->fSuspendKeyMenu = choice_menu("suspend_key",
		B_TRANSLATE("Sleep button:"), kMsgLogindActionChanged,
		"HandleSuspendKey", kSleepKeyChoices, view->fCanHibernate);
	view->fHibernateKeyMenu = choice_menu("hibernate_key",
		B_TRANSLATE("Hibernate button:"), kMsgLogindActionChanged,
		"HandleHibernateKey", kSleepKeyChoices, view->fCanHibernate);
	view->fLidMenu = choice_menu("lid", B_TRANSLATE("Closing the lid:"),
		kMsgLogindActionChanged, "HandleLidSwitch", kKeyChoices,
		view->fCanHibernate);
	view->fIdleMenu = minutes_menu("idle",
		B_TRANSLATE("Suspend when inactive for:"), kMsgLogindActionChanged,
		"IdleAction", kIdleMinutes);

	BBox* systemBox = titled_box("system",
		B_TRANSLATE("Buttons and lid (all users)"));
	BLayoutBuilder::Grid<>(systemBox, B_USE_DEFAULT_SPACING,
			B_USE_SMALL_SPACING)
		.AddMenuField(view->fPowerKeyMenu, 0, 0)
		.AddMenuField(view->fSuspendKeyMenu, 0, 1)
		.AddMenuField(view->fHibernateKeyMenu, 0, 2)
		.AddMenuField(view->fLidMenu, 0, 3)
		.AddMenuField(view->fIdleMenu, 0, 4)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	view->fDisplayOffMenu = minutes_menu("display_off",
		B_TRANSLATE("Turn off the display after:"), kMsgUserSettingChanged,
		NULL, kDisplayMinutes);
	view->fBatteryCriticalMenu = choice_menu("battery_critical",
		B_TRANSLATE("When the battery is critical:"), kMsgUserSettingChanged,
		NULL, kBatteryChoices, view->fCanHibernate);
	view->fNotificationsCheckBox = new BCheckBox("notifications",
		B_TRANSLATE("Notify when the battery is low"),
		new BMessage(kMsgUserSettingChanged));
	view->fDeskbarCheckBox = new BCheckBox("deskbar",
		B_TRANSLATE("Show the battery in the Deskbar"),
		new BMessage(kMsgDeskbarToggled));

	BBox* userBox = titled_box("user", B_TRANSLATE("Display and battery"));
	BLayoutBuilder::Grid<>(userBox, B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
		.AddMenuField(view->fDisplayOffMenu, 0, 0)
		.AddMenuField(view->fBatteryCriticalMenu, 0, 1)
		.Add(view->fNotificationsCheckBox, 0, 2, 2)
		.Add(view->fDeskbarCheckBox, 0, 3, 2)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	BLayoutBuilder::Group<>(view, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(batteryBox)
		.Add(systemBox)
		.Add(userBox);

	view->_LoadLogindActions();
	view->_LoadSettings();
	view->fDeskbarCheckBox->SetValue(
		BDeskbar().HasItem(kPowerStatusItem) ? B_CONTROL_ON : B_CONTROL_OFF);

	return view;
}


void
PowerView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fDriverInterface = new ACPIDriverInterface;
	if (fDriverInterface->Connect() != B_OK) {
		delete fDriverInterface;
		fDriverInterface = new APMDriverInterface;
		if (fDriverInterface->Connect() != B_OK) {
			delete fDriverInterface;
			fDriverInterface = new SysFSDriverInterface;
			if (fDriverInterface->Connect() != B_OK) {
				delete fDriverInterface;
				fDriverInterface = NULL;
			}
		}
	}
	if (fDriverInterface != NULL)
		fDriverInterface->StartWatching(this);
	_UpdateBatteryDisplay();

	bool hasBattery = fDriverInterface != NULL
		&& fDriverInterface->GetBatteryCount() > 0;
	fBatteryCriticalMenu->SetEnabled(hasBattery);
	fNotificationsCheckBox->SetEnabled(hasBattery);

	BMenuField* menus[] = { fPowerKeyMenu, fSuspendKeyMenu, fHibernateKeyMenu,
		fLidMenu, fIdleMenu, fDisplayOffMenu, fBatteryCriticalMenu };
	for (BMenuField* field : menus)
		field->Menu()->SetTargetForItems(this);
	fNotificationsCheckBox->SetTarget(this);
	fDeskbarCheckBox->SetTarget(this);
}


void
PowerView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgUpdate:
			_UpdateBatteryDisplay();
			break;

		case kMsgLogindActionChanged:
			_ApplyLogindAction(message);
			break;

		case kMsgHelperDone:
			// Show what logind does now: the new values, or the old ones
			// after a refusal or a cancelled authentication.
			_LoadLogindActions();
			_SetLogindMenusEnabled(true);
			break;

		case kMsgUserSettingChanged:
			_SaveSettings();
			break;

		case kMsgDeskbarToggled:
			_ToggleDeskbarItem();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


//	#pragma mark - logind actions


void
PowerView::_LoadLogindActions()
{
	sd_bus* bus = NULL;
	if (sd_bus_open_system(&bus) < 0)
		return;

	struct {
		BMenuField*	field;
		const char*	property;
	} keys[] = {
		{ fPowerKeyMenu, "HandlePowerKey" },
		{ fSuspendKeyMenu, "HandleSuspendKey" },
		{ fHibernateKeyMenu, "HandleHibernateKey" },
		{ fLidMenu, "HandleLidSwitch" }
	};
	for (auto& key : keys) {
		char* value = NULL;
		if (sd_bus_get_property_string(bus, kLogin1, kLogin1Path,
				kLogin1Manager, key.property, NULL, &value) >= 0) {
			mark_value(key.field, value);
			free(value);
		}
	}

	char* idleAction = NULL;
	uint64_t idleUSec = 0;
	if (sd_bus_get_property_string(bus, kLogin1, kLogin1Path, kLogin1Manager,
			"IdleAction", NULL, &idleAction) >= 0
		&& sd_bus_get_property_trivial(bus, kLogin1, kLogin1Path,
			kLogin1Manager, "IdleActionUSec", NULL, 't', &idleUSec) >= 0) {
		if (strcmp(idleAction, "ignore") == 0)
			mark_minutes(fIdleMenu, 0);
		else if (strcmp(idleAction, "suspend") == 0) {
			mark_minutes(fIdleMenu,
				(int32)((idleUSec / 1000000 + kIdleHintDelay) / 60));
		} else
			mark_value(fIdleMenu, idleAction);
	}
	free(idleAction);

	sd_bus_flush_close_unref(bus);
}


void
PowerView::_ApplyLogindAction(BMessage* message)
{
	HelperJob* job = new HelperJob;
	job->target = BMessenger(this);
	job->count = 1;

	const char* key = message->GetString("key", "");
	if (strcmp(key, "IdleAction") == 0) {
		int32 minutes = message->GetInt32("minutes", 0);
		if (minutes == 0)
			job->arguments[0] = "IdleAction=ignore";
		else {
			job->arguments[0] = "IdleAction=suspend";
			job->arguments[1].SetToFormat("IdleActionSec=%" B_PRId32,
				minutes * 60 - kIdleHintDelay);
			job->count = 2;
		}
	} else {
		job->arguments[0].SetToFormat("%s=%s", key,
			message->GetString("value", ""));
	}

	// pkexec may wait on an authentication dialog: never in the looper.
	_SetLogindMenusEnabled(false);
	thread_id thread = spawn_thread(_HelperThread, "power helper",
		B_NORMAL_PRIORITY, job);
	if (thread < 0 || resume_thread(thread) != B_OK) {
		delete job;
		_LoadLogindActions();
		_SetLogindMenusEnabled(true);
	}
}


status_t
PowerView::_HelperThread(void* data)
{
	HelperJob* job = (HelperJob*)data;

	int status = -1;
	pid_t pid = fork();
	if (pid == 0) {
		execlp("pkexec", "pkexec", kHelper, job->arguments[0].String(),
			job->count > 1 ? job->arguments[1].String() : (char*)NULL,
			(char*)NULL);
		_exit(127);
	}
	if (pid > 0) {
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		fprintf(stderr, "Power: %s failed\n", kHelper);

	job->target.SendMessage(kMsgHelperDone);
	delete job;
	return B_OK;
}


void
PowerView::_SetLogindMenusEnabled(bool enabled)
{
	fPowerKeyMenu->SetEnabled(enabled);
	fSuspendKeyMenu->SetEnabled(enabled);
	fHibernateKeyMenu->SetEnabled(enabled);
	fLidMenu->SetEnabled(enabled);
	fIdleMenu->SetEnabled(enabled);
}


//	#pragma mark - user settings


void
PowerView::_LoadSettings()
{
	BMessage settings;
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK
		&& path.Append(kSettingsFile) == B_OK) {
		BFile file(path.Path(), B_READ_ONLY);
		if (file.InitCheck() == B_OK)
			settings.Unflatten(&file);
	}

	mark_minutes(fDisplayOffMenu,
		settings.GetInt32(kDisplayOffKey, kDefaultDisplayOff));
	const char* critical = settings.GetString(kBatteryCriticalKey,
		kDefaultBatteryCritical);
	if (!fCanHibernate && strcmp(critical, "hibernate") == 0)
		critical = "poweroff";	// what the registrar falls back to
	mark_value(fBatteryCriticalMenu, critical);
	fNotificationsCheckBox->SetValue(
		settings.GetBool(kNotificationsKey, true)
			? B_CONTROL_ON : B_CONTROL_OFF);
}


void
PowerView::_SaveSettings()
{
	BMessage settings('pwrP');

	BMenuItem* item = fDisplayOffMenu->Menu()->FindMarked();
	settings.AddInt32(kDisplayOffKey, item != NULL
		? item->Message()->GetInt32("minutes", kDefaultDisplayOff)
		: kDefaultDisplayOff);

	item = fBatteryCriticalMenu->Menu()->FindMarked();
	settings.AddString(kBatteryCriticalKey,
		item != NULL && item->Message() != NULL
			? item->Message()->GetString("value", kDefaultBatteryCritical)
			: kDefaultBatteryCritical);

	settings.AddBool(kNotificationsKey,
		fNotificationsCheckBox->Value() == B_CONTROL_ON);

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK
		|| path.Append(kSettingsFile) != B_OK)
		return;
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() == B_OK)
		settings.Flatten(&file);
}


void
PowerView::_ToggleDeskbarItem()
{
	BDeskbar deskbar;
	if (fDeskbarCheckBox->Value() == B_CONTROL_OFF) {
		deskbar.RemoveItem(kPowerStatusItem);
		return;
	}

	entry_ref ref;
	if (be_roster->FindApp(kPowerStatusSignature, &ref) != B_OK
		|| deskbar.AddItem(&ref) != B_OK)
		fDeskbarCheckBox->SetValue(B_CONTROL_OFF);
}


//	#pragma mark - battery


void
PowerView::_UpdateBatteryDisplay()
{
	if (fDriverInterface == NULL || fDriverInterface->GetBatteryCount() == 0) {
		fBatteryStateLabel->SetText(B_TRANSLATE("State: no battery detected"));
		fBatteryPercentLabel->SetText(B_TRANSLATE("Charge: --"));
		fBatteryTimeLabel->SetText(B_TRANSLATE("Time remaining: --"));
		return;
	}

	battery_info info;
	if (fDriverInterface->GetBatteryInfo(0, &info) != B_OK) {
		fBatteryStateLabel->SetText(
			B_TRANSLATE("State: error reading battery"));
		fBatteryPercentLabel->SetText(B_TRANSLATE("Charge: --"));
		fBatteryTimeLabel->SetText(B_TRANSLATE("Time remaining: --"));
		return;
	}

	const char* state;
	if ((info.state & BATTERY_CHARGING) != 0)
		state = B_TRANSLATE("Charging");
	else if ((info.state & BATTERY_DISCHARGING) != 0)
		state = B_TRANSLATE("Discharging");
	else if ((info.state & BATTERY_NOT_CHARGING) != 0)
		state = B_TRANSLATE("Not charging");
	else if ((info.state & BATTERY_CRITICAL_STATE) != 0)
		state = B_TRANSLATE("Critical");
	else
		state = B_TRANSLATE("Full");

	BString label(B_TRANSLATE("State: %state%"));
	label.ReplaceFirst("%state%", state);
	fBatteryStateLabel->SetText(label);

	if (info.full_capacity > 0) {
		label = B_TRANSLATE("Charge: %percent%%");
		BString percent;
		percent << (int32)((double)info.capacity / info.full_capacity * 100.0
			+ 0.5);
		label.ReplaceFirst("%percent%", percent);
		fBatteryPercentLabel->SetText(label);
	} else
		fBatteryPercentLabel->SetText(B_TRANSLATE("Charge: --"));

	if (info.time_left >= 0) {
		label.SetToFormat(B_TRANSLATE("Time remaining: %d:%02d"),
			(int)(info.time_left / 3600), (int)((info.time_left / 60) % 60));
		fBatteryTimeLabel->SetText(label);
	} else
		fBatteryTimeLabel->SetText(B_TRANSLATE("Time remaining: --"));
}
