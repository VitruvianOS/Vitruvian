/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PowerView.h"

#include <string.h>

#include <Box.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <Path.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>

#include "ACPIDriverInterface.h"
#include "APMDriverInterface.h"
#include "SysFSDriverInterface.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Power"


// Settings keys — must match Registrar.cpp exactly.
static const char* kSettingsPowerButtonAction = "power:power_button_action";
static const char* kSettingsLidCloseAction = "power:lid_close_action";
static const char* kSettingsAutoHibernateCritical = "power:auto_hibernate_critical";

// Additional settings keys (preflet UI only — stored for future registrar
// integration; the registrar does not read these yet).
static const char* kSettingsSleepButtonAction = "power:sleep_button_action";
static const char* kSettingsHibernateButtonAction = "power:hibernate_button_action";
static const char* kSettingsBatteryButtonAction = "power:battery_button_action";
static const char* kSettingsStatusNotifications = "power:status_notifications";
static const char* kSettingsSystemTrayIcon = "power:system_tray_icon";
static const char* kSettingsSystemSleepMode = "power:system_sleep_mode";
static const char* kSettingsIdleTimeout = "power:idle_timeout";
static const char* kSettingsLockScreenOnSleep = "power:lock_screen_on_sleep";
static const char* kSettingsDisplayPowerManagement = "power:display_power_management";
static const char* kSettingsDisplaySleepAfter = "power:display_sleep_after";
static const char* kSettingsDisplaySwitchOff = "power:display_switch_off";


//	#pragma mark - Helpers





//	#pragma mark - Construction


PowerView::PowerView()
	:
	BView("Power", B_WILL_DRAW),
	fDriverInterface(NULL),
	fBatteryStateLabel(NULL),
	fBatteryPercentLabel(NULL),
	fBatteryTimeLabel(NULL),
	fPowerButtonMenu(NULL),
	fSleepButtonMenu(NULL),
	fHibernateButtonMenu(NULL),
	fBatteryButtonMenu(NULL),
	fStatusNotificationsCheckBox(NULL),
	fSystemTrayIconCheckBox(NULL),
	fSystemSleepModeMenu(NULL),
	fIdleTimeoutMenu(NULL),
	fLockScreenOnSleepCheckBox(NULL),
	fDisplayPowerManagementCheckBox(NULL),
	fDisplaySleepAfterMenu(NULL),
	fDisplaySwitchOffMenu(NULL),
	fLidCloseActionMenu(NULL),
	fAutoHibernateCheckBox(NULL)
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

	// ---- Battery status box ----
	BBox* batteryBox = new BBox("Battery status");
	batteryBox->SetLabel(B_TRANSLATE("Battery"));

	BLayoutBuilder::Group<>(batteryBox, B_VERTICAL, 0)
		.Add(view->fBatteryStateLabel = new BStringView("state",
			B_TRANSLATE("State: detecting...")))
		.Add(view->fBatteryPercentLabel = new BStringView("percent",
			B_TRANSLATE("Charge: --")))
		.Add(view->fBatteryTimeLabel = new BStringView("time",
			B_TRANSLATE("Time remaining: --")))
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	// ---- General tab controls ----
	BBox* generalBox = new BBox("General");
	generalBox->SetLabel(B_TRANSLATE("General"));

	// Power button action
	BMenu* pwrMenu = new BMenu("power_button");
	pwrMenu->AddItem(new BMenuItem("Show shutdown dialog",
		new BMessage(PowerView::kMsgPowerButtonChanged)));
	pwrMenu->AddItem(new BMenuItem("Suspend",
		new BMessage(PowerView::kMsgPowerButtonChanged)));
	pwrMenu->AddItem(new BMenuItem("Hibernate",
		new BMessage(PowerView::kMsgPowerButtonChanged)));
	pwrMenu->AddItem(new BMenuItem("Power off",
		new BMessage(PowerView::kMsgPowerButtonChanged)));
	pwrMenu->AddItem(new BMenuItem("Do nothing",
		new BMessage(PowerView::kMsgPowerButtonChanged)));
	pwrMenu->SetLabelFromMarked(true);
	pwrMenu->ItemAt(0)->SetMarked(true);

	view->fPowerButtonMenu = new BMenuField("power_button",
		B_TRANSLATE("When pressing the power button:"), pwrMenu);

	// Sleep button action
	BMenu* sleepMenu = new BMenu("sleep_button");
	sleepMenu->AddItem(new BMenuItem("Suspend",
		new BMessage(PowerView::kMsgSleepButtonChanged)));
	sleepMenu->AddItem(new BMenuItem("Hibernate",
		new BMessage(PowerView::kMsgSleepButtonChanged)));
	sleepMenu->AddItem(new BMenuItem("Do nothing",
		new BMessage(PowerView::kMsgSleepButtonChanged)));
	sleepMenu->SetLabelFromMarked(true);
	sleepMenu->ItemAt(0)->SetMarked(true);

	view->fSleepButtonMenu = new BMenuField("sleep_button",
		B_TRANSLATE("When pressing the sleep button:"), sleepMenu);

	// Hibernate button action
	BMenu* hibMenu = new BMenu("hibernate_button");
	hibMenu->AddItem(new BMenuItem("Hibernate",
		new BMessage(PowerView::kMsgHibernateButtonChanged)));
	hibMenu->AddItem(new BMenuItem("Suspend",
		new BMessage(PowerView::kMsgHibernateButtonChanged)));
	hibMenu->AddItem(new BMenuItem("Do nothing",
		new BMessage(PowerView::kMsgHibernateButtonChanged)));
	hibMenu->SetLabelFromMarked(true);
	hibMenu->ItemAt(0)->SetMarked(true);

	view->fHibernateButtonMenu = new BMenuField("hibernate_button",
		B_TRANSLATE("When pressing the hibernate button:"), hibMenu);

	// Battery button action
	BMenu* batMenu = new BMenu("battery_button");
	batMenu->AddItem(new BMenuItem("Hibernate",
		new BMessage(PowerView::kMsgBatteryButtonChanged)));
	batMenu->AddItem(new BMenuItem("Suspend",
		new BMessage(PowerView::kMsgBatteryButtonChanged)));
	batMenu->AddItem(new BMenuItem("Shutdown",
		new BMessage(PowerView::kMsgBatteryButtonChanged)));
	batMenu->AddItem(new BMenuItem("Do nothing",
		new BMessage(PowerView::kMsgBatteryButtonChanged)));
	batMenu->SetLabelFromMarked(true);
	batMenu->ItemAt(0)->SetMarked(true);

	view->fBatteryButtonMenu = new BMenuField("battery_button",
		B_TRANSLATE("When battery critical:"), batMenu);

	// Status notifications toggle
	view->fStatusNotificationsCheckBox = new BCheckBox("status_notifications",
		B_TRANSLATE("Show battery status notifications"),
		new BMessage(PowerView::kMsgStatusNotificationsToggled));
	view->fStatusNotificationsCheckBox->SetValue(1);  // ON by default

	// System tray icon toggle
	view->fSystemTrayIconCheckBox = new BCheckBox("system_tray_icon",
		B_TRANSLATE("Show system tray icon"),
		new BMessage(PowerView::kMsgSystemTrayIconToggled));
	view->fSystemTrayIconCheckBox->SetValue(0);  // OFF by default

	BLayoutBuilder::Group<>(generalBox, B_VERTICAL, 0)
		.Add(view->fPowerButtonMenu)
		.Add(view->fSleepButtonMenu)
		.Add(view->fHibernateButtonMenu)
		.Add(view->fBatteryButtonMenu)
		.Add(view->fStatusNotificationsCheckBox)
		.Add(view->fSystemTrayIconCheckBox)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	// ---- System tab controls ----
	BBox* systemBox = new BBox("System");
	systemBox->SetLabel(B_TRANSLATE("System"));

	// System sleep mode
	BMenu* sleepModeMenu = new BMenu("sleep_mode");
	sleepModeMenu->AddItem(new BMenuItem("Suspend",
		new BMessage(PowerView::kMsgSystemSleepModeChanged)));
	sleepModeMenu->AddItem(new BMenuItem("Hibernate",
		new BMessage(PowerView::kMsgSystemSleepModeChanged)));
	sleepModeMenu->SetLabelFromMarked(true);
	sleepModeMenu->ItemAt(0)->SetMarked(true);

	view->fSystemSleepModeMenu = new BMenuField("sleep_mode",
		B_TRANSLATE("System sleep mode:"), sleepModeMenu);

	// Idle timeout
	BMenu* idleMenu = new BMenu("idle_timeout");
	idleMenu->AddItem(new BMenuItem("Never",
		new BMessage(PowerView::kMsgIdleTimeoutChanged)));
	idleMenu->AddItem(new BMenuItem("5 minutes",
		new BMessage(PowerView::kMsgIdleTimeoutChanged)));
	idleMenu->AddItem(new BMenuItem("10 minutes",
		new BMessage(PowerView::kMsgIdleTimeoutChanged)));
	idleMenu->AddItem(new BMenuItem("15 minutes",
		new BMessage(PowerView::kMsgIdleTimeoutChanged)));
	idleMenu->AddItem(new BMenuItem("30 minutes",
		new BMessage(PowerView::kMsgIdleTimeoutChanged)));
	idleMenu->AddItem(new BMenuItem("1 hour",
		new BMessage(PowerView::kMsgIdleTimeoutChanged)));
	idleMenu->AddItem(new BMenuItem("2 hours",
		new BMessage(PowerView::kMsgIdleTimeoutChanged)));
	idleMenu->SetLabelFromMarked(true);
	idleMenu->ItemAt(0)->SetMarked(true);

	view->fIdleTimeoutMenu = new BMenuField("idle_timeout",
		B_TRANSLATE("When inactive for:"), idleMenu);

	// Lock screen on sleep
	view->fLockScreenOnSleepCheckBox = new BCheckBox("lock_screen_on_sleep",
		B_TRANSLATE("Lock screen on sleep"),
		new BMessage(PowerView::kMsgLockScreenOnSleepToggled));
	view->fLockScreenOnSleepCheckBox->SetValue(1);  // ON by default

	BLayoutBuilder::Group<>(systemBox, B_VERTICAL, 0)
		.Add(view->fSystemSleepModeMenu)
		.Add(view->fIdleTimeoutMenu)
		.Add(view->fLockScreenOnSleepCheckBox)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	// ---- Display tab controls ----
	BBox* displayBox = new BBox("Display");
	displayBox->SetLabel(B_TRANSLATE("Display"));

	// Display power management toggle
	view->fDisplayPowerManagementCheckBox = new BCheckBox(
		"display_power_management",
		B_TRANSLATE("Enable display power management"),
		new BMessage(PowerView::kMsgDisplayPowerManagementToggled));
	view->fDisplayPowerManagementCheckBox->SetValue(1);  // ON by default

	// Display sleep after
	BMenu* displaySleepMenu = new BMenu("display_sleep");
	displaySleepMenu->AddItem(new BMenuItem("Never",
		new BMessage(PowerView::kMsgDisplaySleepAfterChanged)));
	displaySleepMenu->AddItem(new BMenuItem("5 minutes",
		new BMessage(PowerView::kMsgDisplaySleepAfterChanged)));
	displaySleepMenu->AddItem(new BMenuItem("10 minutes",
		new BMessage(PowerView::kMsgDisplaySleepAfterChanged)));
	displaySleepMenu->AddItem(new BMenuItem("15 minutes",
		new BMessage(PowerView::kMsgDisplaySleepAfterChanged)));
	displaySleepMenu->AddItem(new BMenuItem("30 minutes",
		new BMessage(PowerView::kMsgDisplaySleepAfterChanged)));
	displaySleepMenu->SetLabelFromMarked(true);
	displaySleepMenu->ItemAt(2)->SetMarked(true);  // Default: 10 minutes

	view->fDisplaySleepAfterMenu = new BMenuField("display_sleep_after",
		B_TRANSLATE("Put display to sleep after:"), displaySleepMenu);

	// Display switch off
	BMenu* displaySwitchOffMenu = new BMenu("display_switch_off");
	displaySwitchOffMenu->AddItem(new BMenuItem("Never",
		new BMessage(PowerView::kMsgDisplaySwitchOffChanged)));
	displaySwitchOffMenu->AddItem(new BMenuItem("5 minutes",
		new BMessage(PowerView::kMsgDisplaySwitchOffChanged)));
	displaySwitchOffMenu->AddItem(new BMenuItem("10 minutes",
		new BMessage(PowerView::kMsgDisplaySwitchOffChanged)));
	displaySwitchOffMenu->AddItem(new BMenuItem("15 minutes",
		new BMessage(PowerView::kMsgDisplaySwitchOffChanged)));
	displaySwitchOffMenu->AddItem(new BMenuItem("30 minutes",
		new BMessage(PowerView::kMsgDisplaySwitchOffChanged)));
	displaySwitchOffMenu->SetLabelFromMarked(true);
	displaySwitchOffMenu->ItemAt(3)->SetMarked(true);  // Default: 15 minutes

	view->fDisplaySwitchOffMenu = new BMenuField("display_switch_off",
		B_TRANSLATE("Switch off display after:"), displaySwitchOffMenu);

	BLayoutBuilder::Group<>(displayBox, B_VERTICAL, 0)
		.Add(view->fDisplayPowerManagementCheckBox)
		.Add(view->fDisplaySleepAfterMenu)
		.Add(view->fDisplaySwitchOffMenu)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	// ---- Lid close / auto-hibernate (Phase IV) ----
	BBox* lidBox = new BBox("Lid and battery");
	lidBox->SetLabel(B_TRANSLATE("Lid and battery actions"));

	BMenu* lidMenu = new BMenu("lid_close");
	lidMenu->AddItem(new BMenuItem("Suspend",
		new BMessage(PowerView::kMsgLidCloseChanged)));
	lidMenu->AddItem(new BMenuItem("Hibernate",
		new BMessage(PowerView::kMsgLidCloseChanged)));
	lidMenu->AddItem(new BMenuItem("Do nothing",
		new BMessage(PowerView::kMsgLidCloseChanged)));
	lidMenu->SetLabelFromMarked(true);
	lidMenu->ItemAt(0)->SetMarked(true);

	view->fLidCloseActionMenu = new BMenuField("lid_close",
		B_TRANSLATE("When lid is closed:"), lidMenu);

	view->fAutoHibernateCheckBox = new BCheckBox("auto_hibernate",
		B_TRANSLATE("Hibernate automatically at critical battery"),
		new BMessage(PowerView::kMsgAutoHibernateToggled));

	BLayoutBuilder::Group<>(lidBox, B_VERTICAL, 0)
		.Add(view->fLidCloseActionMenu)
		.Add(view->fAutoHibernateCheckBox)
		.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
			B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING);

	// ---- Assemble ----
	BLayoutBuilder::Group<>(view, B_VERTICAL, 0)
		.Add(batteryBox)
		.Add(BSpaceLayoutItem::CreateVerticalStrut(8))
		.Add(generalBox)
		.Add(BSpaceLayoutItem::CreateVerticalStrut(8))
		.Add(systemBox)
		.Add(BSpaceLayoutItem::CreateVerticalStrut(8))
		.Add(displayBox)
		.Add(BSpaceLayoutItem::CreateVerticalStrut(8))
		.Add(lidBox);

	view->_LoadSettings();

	return view;
}


//	#pragma mark - BView overrides


void
PowerView::AttachedToWindow()
{
	BView::AttachedToWindow();

	// Connect to battery status
	fDriverInterface = new ACPIDriverInterface;
	if (fDriverInterface->Connect() != B_OK) {
		delete fDriverInterface;
		fDriverInterface = new APMDriverInterface;
		if (fDriverInterface->Connect() != B_OK) {
			delete fDriverInterface;
			fDriverInterface = new SysFSDriverInterface;
			if (fDriverInterface->Connect() != B_OK) {
				fprintf(stderr, "Power preferences: no battery interface.\n");
				delete fDriverInterface;
				fDriverInterface = NULL;
			}
		}
	}

	if (fDriverInterface != NULL) {
		fDriverInterface->StartWatching(this);
		_UpdateBatteryDisplay();
	}

	// Wire up control targets — target each menu item individually.
	auto _wireMenuItems = [](BMenuField* field, BMessenger target) {
		if (field == NULL)
			return;
		BMenu* menu = field->Menu();
		for (int32 i = 0; i < menu->CountItems(); i++)
			menu->ItemAt(i)->SetTarget(target);
	};

	_wireMenuItems(fPowerButtonMenu, this);
	_wireMenuItems(fSleepButtonMenu, this);
	_wireMenuItems(fHibernateButtonMenu, this);
	_wireMenuItems(fBatteryButtonMenu, this);
	fStatusNotificationsCheckBox->SetTarget(this);
	fSystemTrayIconCheckBox->SetTarget(this);
	_wireMenuItems(fSystemSleepModeMenu, this);
	_wireMenuItems(fIdleTimeoutMenu, this);
	fLockScreenOnSleepCheckBox->SetTarget(this);
	fDisplayPowerManagementCheckBox->SetTarget(this);
	_wireMenuItems(fDisplaySleepAfterMenu, this);
	_wireMenuItems(fDisplaySwitchOffMenu, this);
	_wireMenuItems(fLidCloseActionMenu, this);
	fAutoHibernateCheckBox->SetTarget(this);
}


void
PowerView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgUpdate:
			_UpdateBatteryDisplay();
			break;

		case kMsgPowerButtonChanged:
		case kMsgSleepButtonChanged:
		case kMsgHibernateButtonChanged:
		case kMsgBatteryButtonChanged:
		case kMsgStatusNotificationsToggled:
		case kMsgSystemTrayIconToggled:
		case kMsgSystemSleepModeChanged:
		case kMsgIdleTimeoutChanged:
		case kMsgLockScreenOnSleepToggled:
		case kMsgDisplayPowerManagementToggled:
		case kMsgDisplaySleepAfterChanged:
		case kMsgDisplaySwitchOffChanged:
		case kMsgLidCloseChanged:
		case kMsgAutoHibernateToggled:
			_SaveSettings();
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


//	#pragma mark - Private


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
	status_t status = fDriverInterface->GetBatteryInfo(0, &info);
	if (status != B_OK) {
		fBatteryStateLabel->SetText(B_TRANSLATE("State: error reading battery"));
		fBatteryPercentLabel->SetText(B_TRANSLATE("Charge: --"));
		fBatteryTimeLabel->SetText(B_TRANSLATE("Time remaining: --"));
		return;
	}

	// State
	const char* stateStr = B_TRANSLATE("unknown");
	if ((info.state & BATTERY_CHARGING) != 0)
		stateStr = B_TRANSLATE("Charging");
	else if ((info.state & BATTERY_DISCHARGING) != 0)
		stateStr = B_TRANSLATE("Discharging");
	else if ((info.state & BATTERY_NOT_CHARGING) != 0)
		stateStr = B_TRANSLATE("Not charging");
	else if ((info.state & BATTERY_CRITICAL_STATE) != 0)
		stateStr = B_TRANSLATE("Critical");
	else
		stateStr = B_TRANSLATE("Full");

	BString stateLabel(B_TRANSLATE("State: %state%"));
	stateLabel.ReplaceFirst("%state%", stateStr);
	fBatteryStateLabel->SetText(stateLabel.String());

	// Percentage
	if (info.full_capacity > 0) {
		double percent = (double)info.capacity / info.full_capacity * 100.0;
		BString percentLabel;
		percentLabel.SetToFormat("Charge: %.0f%%", percent);
		fBatteryPercentLabel->SetText(percentLabel.String());
	} else {
		fBatteryPercentLabel->SetText(B_TRANSLATE("Charge: --"));
	}

	// Time remaining
	if (info.time_left >= 0) {
		int32 hours = info.time_left / 3600;
		int32 minutes = (info.time_left / 60) % 60;
		BString timeLabel;
		timeLabel.SetToFormat(B_TRANSLATE("Time remaining: %d:%02d"),
			hours, minutes);
		fBatteryTimeLabel->SetText(timeLabel.String());
	} else {
		fBatteryTimeLabel->SetText(B_TRANSLATE("Time remaining: --"));
	}
}


void
PowerView::_LoadSettings()
{
	BFile file;
	BPath path;

	// Try to find settings in user settings directory
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status != B_OK)
		return;

	path.Append("Power settings");

	if (file.SetTo(path.Path(), B_READ_ONLY) != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;

	// Power button action
	int32 pwrAction = 0;
	if (settings.FindInt32(kSettingsPowerButtonAction, &pwrAction) == B_OK) {
		BMenu* menu = fPowerButtonMenu->Menu();
		if (pwrAction >= 0 && pwrAction < menu->CountItems())
			menu->ItemAt(pwrAction)->SetMarked(true);
	}

	// Sleep button action
	int32 sleepAction = 0;
	if (settings.FindInt32(kSettingsSleepButtonAction, &sleepAction) == B_OK) {
		BMenu* menu = fSleepButtonMenu->Menu();
		if (sleepAction >= 0 && sleepAction < menu->CountItems())
			menu->ItemAt(sleepAction)->SetMarked(true);
	}

	// Hibernate button action
	int32 hibAction = 0;
	if (settings.FindInt32(kSettingsHibernateButtonAction, &hibAction) == B_OK) {
		BMenu* menu = fHibernateButtonMenu->Menu();
		if (hibAction >= 0 && hibAction < menu->CountItems())
			menu->ItemAt(hibAction)->SetMarked(true);
	}

	// Battery button action
	int32 batAction = 0;
	if (settings.FindInt32(kSettingsBatteryButtonAction, &batAction) == B_OK) {
		BMenu* menu = fBatteryButtonMenu->Menu();
		if (batAction >= 0 && batAction < menu->CountItems())
			menu->ItemAt(batAction)->SetMarked(true);
	}

	// Status notifications
	bool statusNotif = true;
	if (settings.FindBool(kSettingsStatusNotifications, &statusNotif) == B_OK)
		fStatusNotificationsCheckBox->SetValue(statusNotif ? 1 : 0);

	// System tray icon
	bool trayIcon = false;
	if (settings.FindBool(kSettingsSystemTrayIcon, &trayIcon) == B_OK)
		fSystemTrayIconCheckBox->SetValue(trayIcon ? 1 : 0);

	// System sleep mode
	int32 sleepMode = 0;
	if (settings.FindInt32(kSettingsSystemSleepMode, &sleepMode) == B_OK) {
		BMenu* menu = fSystemSleepModeMenu->Menu();
		if (sleepMode >= 0 && sleepMode < menu->CountItems())
			menu->ItemAt(sleepMode)->SetMarked(true);
	}

	// Idle timeout (stored as minutes; -1 = Never)
	int32 idleTimeout = -1;
	if (settings.FindInt32(kSettingsIdleTimeout, &idleTimeout) == B_OK) {
		BMenu* menu = fIdleTimeoutMenu->Menu();
		int32 index = -1;
		switch (idleTimeout) {
			case -1: index = 0; break;  // Never
			case 5: index = 1; break;
			case 10: index = 2; break;
			case 15: index = 3; break;
			case 30: index = 4; break;
			case 60: index = 5; break;
			case 120: index = 6; break;
		}
		if (index >= 0 && index < menu->CountItems())
			menu->ItemAt(index)->SetMarked(true);
	}

	// Lock screen on sleep
	bool lockOnSleep = true;
	if (settings.FindBool(kSettingsLockScreenOnSleep, &lockOnSleep) == B_OK)
		fLockScreenOnSleepCheckBox->SetValue(lockOnSleep ? 1 : 0);

	// Display power management
	bool displayPwr = true;
	if (settings.FindBool(kSettingsDisplayPowerManagement, &displayPwr) == B_OK)
		fDisplayPowerManagementCheckBox->SetValue(displayPwr ? 1 : 0);

	// Display sleep after (stored as minutes; -1 = Never)
	int32 displaySleep = 10;
	if (settings.FindInt32(kSettingsDisplaySleepAfter, &displaySleep) == B_OK) {
		BMenu* menu = fDisplaySleepAfterMenu->Menu();
		int32 index = -1;
		switch (displaySleep) {
			case -1: index = 0; break;  // Never
			case 5: index = 1; break;
			case 10: index = 2; break;
			case 15: index = 3; break;
			case 30: index = 4; break;
		}
		if (index >= 0 && index < menu->CountItems())
			menu->ItemAt(index)->SetMarked(true);
	}

	// Display switch off (stored as minutes; -1 = Never)
	int32 displayOff = 15;
	if (settings.FindInt32(kSettingsDisplaySwitchOff, &displayOff) == B_OK) {
		BMenu* menu = fDisplaySwitchOffMenu->Menu();
		int32 index = -1;
		switch (displayOff) {
			case -1: index = 0; break;  // Never
			case 5: index = 1; break;
			case 10: index = 2; break;
			case 15: index = 3; break;
			case 30: index = 4; break;
		}
		if (index >= 0 && index < menu->CountItems())
			menu->ItemAt(index)->SetMarked(true);
	}

	// Lid close action
	int32 lidAction = 0;
	if (settings.FindInt32(kSettingsLidCloseAction, &lidAction) == B_OK) {
		BMenu* menu = fLidCloseActionMenu->Menu();
		if (lidAction >= 0 && lidAction < menu->CountItems())
			menu->ItemAt(lidAction)->SetMarked(true);
	}

	// Auto-hibernate at critical battery
	bool autoHibernate = false;
	if (settings.FindBool(kSettingsAutoHibernateCritical, &autoHibernate) == B_OK)
		fAutoHibernateCheckBox->SetValue(autoHibernate ? 1 : 0);
}


void
PowerView::_SaveSettings()
{
	BFile file;
	BPath path;

	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path, true);
	if (status != B_OK)
		return;

	path.Append("Power settings");

	if (file.SetTo(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE)
			!= B_OK)
		return;

	BMessage settings('pwrP');

	// Power button action (index)
	BMenuItem* pwrMarked = fPowerButtonMenu->Menu()->FindMarked();
	int32 pwrAction = 0;
	if (pwrMarked != NULL)
		pwrAction = fPowerButtonMenu->Menu()->IndexOf(pwrMarked);
	settings.AddInt32(kSettingsPowerButtonAction, pwrAction);

	// Sleep button action (index)
	BMenuItem* sleepMarked = fSleepButtonMenu->Menu()->FindMarked();
	int32 sleepAction = 0;
	if (sleepMarked != NULL)
		sleepAction = fSleepButtonMenu->Menu()->IndexOf(sleepMarked);
	settings.AddInt32(kSettingsSleepButtonAction, sleepAction);

	// Hibernate button action (index)
	BMenuItem* hibMarked = fHibernateButtonMenu->Menu()->FindMarked();
	int32 hibAction = 0;
	if (hibMarked != NULL)
		hibAction = fHibernateButtonMenu->Menu()->IndexOf(hibMarked);
	settings.AddInt32(kSettingsHibernateButtonAction, hibAction);

	// Battery button action (index)
	BMenuItem* batMarked = fBatteryButtonMenu->Menu()->FindMarked();
	int32 batAction = 0;
	if (batMarked != NULL)
		batAction = fBatteryButtonMenu->Menu()->IndexOf(batMarked);
	settings.AddInt32(kSettingsBatteryButtonAction, batAction);

	// Status notifications
	settings.AddBool(kSettingsStatusNotifications,
		fStatusNotificationsCheckBox->Value() != 0);

	// System tray icon
	settings.AddBool(kSettingsSystemTrayIcon,
		fSystemTrayIconCheckBox->Value() != 0);

	// System sleep mode (index)
	BMenuItem* sleepModeMarked = fSystemSleepModeMenu->Menu()->FindMarked();
	int32 sleepMode = 0;
	if (sleepModeMarked != NULL)
		sleepMode = fSystemSleepModeMenu->Menu()->IndexOf(sleepModeMarked);
	settings.AddInt32(kSettingsSystemSleepMode, sleepMode);

	// Idle timeout (index to minutes; 0 = Never = -1)
	BMenuItem* idleMarked = fIdleTimeoutMenu->Menu()->FindMarked();
	int32 idleMinutes = -1;
	if (idleMarked != NULL) {
		if (strcmp(idleMarked->Label(), "Never") == 0) idleMinutes = -1;
		else if (strcmp(idleMarked->Label(), "5 minutes") == 0) idleMinutes = 5;
		else if (strcmp(idleMarked->Label(), "10 minutes") == 0) idleMinutes = 10;
		else if (strcmp(idleMarked->Label(), "15 minutes") == 0) idleMinutes = 15;
		else if (strcmp(idleMarked->Label(), "30 minutes") == 0) idleMinutes = 30;
		else if (strcmp(idleMarked->Label(), "1 hour") == 0) idleMinutes = 60;
		else if (strcmp(idleMarked->Label(), "2 hours") == 0) idleMinutes = 120;
	}
	settings.AddInt32(kSettingsIdleTimeout, idleMinutes);

	// Lock screen on sleep
	settings.AddBool(kSettingsLockScreenOnSleep,
		fLockScreenOnSleepCheckBox->Value() != 0);

	// Display power management
	settings.AddBool(kSettingsDisplayPowerManagement,
		fDisplayPowerManagementCheckBox->Value() != 0);

	// Display sleep after (index to minutes; 0 = Never = -1)
	BMenuItem* dSleepMarked = fDisplaySleepAfterMenu->Menu()->FindMarked();
	int32 displaySleep = 10;
	if (dSleepMarked != NULL) {
		if (strcmp(dSleepMarked->Label(), "Never") == 0) displaySleep = -1;
		else if (strcmp(dSleepMarked->Label(), "5 minutes") == 0) displaySleep = 5;
		else if (strcmp(dSleepMarked->Label(), "10 minutes") == 0) displaySleep = 10;
		else if (strcmp(dSleepMarked->Label(), "15 minutes") == 0) displaySleep = 15;
		else if (strcmp(dSleepMarked->Label(), "30 minutes") == 0) displaySleep = 30;
	}
	settings.AddInt32(kSettingsDisplaySleepAfter, displaySleep);

	// Display switch off (index to minutes; 0 = Never = -1)
	BMenuItem* dOffMarked = fDisplaySwitchOffMenu->Menu()->FindMarked();
	int32 displayOff = 15;
	if (dOffMarked != NULL) {
		if (strcmp(dOffMarked->Label(), "Never") == 0) displayOff = -1;
		else if (strcmp(dOffMarked->Label(), "5 minutes") == 0) displayOff = 5;
		else if (strcmp(dOffMarked->Label(), "10 minutes") == 0) displayOff = 10;
		else if (strcmp(dOffMarked->Label(), "15 minutes") == 0) displayOff = 15;
		else if (strcmp(dOffMarked->Label(), "30 minutes") == 0) displayOff = 30;
	}
	settings.AddInt32(kSettingsDisplaySwitchOff, displayOff);

	// Lid close action (index)
	BMenuItem* lidMarked = fLidCloseActionMenu->Menu()->FindMarked();
	int32 lidAction = 0;
	if (lidMarked != NULL)
		lidAction = fLidCloseActionMenu->Menu()->IndexOf(lidMarked);
	settings.AddInt32(kSettingsLidCloseAction, lidAction);

	// Auto-hibernate at critical battery
	settings.AddBool(kSettingsAutoHibernateCritical,
		fAutoHibernateCheckBox->Value() != 0);

	ssize_t size = 0;
	settings.Flatten(&file, &size);
}
