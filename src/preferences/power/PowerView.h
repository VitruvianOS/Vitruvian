/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef POWER_VIEW_H
#define POWER_VIEW_H


#include <View.h>

#include "DriverInterface.h"


class BCheckBox;
class BMenuField;
class BStringView;


class PowerView : public BView {
public:
								PowerView();
	virtual						~PowerView();

	static PowerView*			Create();

	virtual void				AttachedToWindow();
	virtual void				MessageReceived(BMessage* message);

private:
	void						_UpdateBatteryDisplay();
	void						_LoadSettings();
	void						_SaveSettings();

	PowerStatusDriverInterface*	fDriverInterface;

	// Battery status
	BStringView*				fBatteryStateLabel;
	BStringView*				fBatteryPercentLabel;
	BStringView*				fBatteryTimeLabel;

	// General controls
	BMenuField*					fPowerButtonMenu;
	BMenuField*					fSleepButtonMenu;
	BMenuField*					fHibernateButtonMenu;
	BMenuField*					fBatteryButtonMenu;
	BCheckBox*					fStatusNotificationsCheckBox;
	BCheckBox*					fSystemTrayIconCheckBox;

	// System controls
	BMenuField*					fSystemSleepModeMenu;
	BMenuField*					fIdleTimeoutMenu;
	BCheckBox*					fLockScreenOnSleepCheckBox;

	// Display controls
	BCheckBox*					fDisplayPowerManagementCheckBox;
	BMenuField*					fDisplaySleepAfterMenu;
	BMenuField*					fDisplaySwitchOffMenu;

	// Lid / auto-hibernate (Phase IV)
	BMenuField*					fLidCloseActionMenu;
	BCheckBox*					fAutoHibernateCheckBox;

	static const uint32		kMsgPowerButtonChanged = 'pBch';
	static const uint32		kMsgSleepButtonChanged = 'sBch';
	static const uint32		kMsgHibernateButtonChanged = 'hBch';
	static const uint32		kMsgBatteryButtonChanged = 'bBch';
	static const uint32		kMsgStatusNotificationsToggled = 'sNtf';
	static const uint32		kMsgSystemTrayIconToggled = 'sTrI';
	static const uint32		kMsgSystemSleepModeChanged = 'sSlM';
	static const uint32		kMsgIdleTimeoutChanged = 'iTmc';
	static const uint32		kMsgLockScreenOnSleepToggled = 'lSsl';
	static const uint32		kMsgDisplayPowerManagementToggled = 'dPwr';
	static const uint32		kMsgDisplaySleepAfterChanged = 'dSlp';
	static const uint32		kMsgDisplaySwitchOffChanged = 'dSwo';
	static const uint32		kMsgLidCloseChanged = 'lCch';
	static const uint32		kMsgAutoHibernateToggled = 'aHib';
	static const uint32		kMsgUpdateBattery = 'uBat';
};


#endif	// POWER_VIEW_H
