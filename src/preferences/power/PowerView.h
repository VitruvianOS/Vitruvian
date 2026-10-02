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


// Every control is backed by something that acts on it: logind (via vos-set-power-actions) for keys,
// lid and inactivity, app_server for display off, the registrar for battery, PowerStatus for notices.
class PowerView : public BView {
public:
								PowerView();
	virtual						~PowerView();

	static	PowerView*			Create();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_UpdateBatteryDisplay();
			void				_LoadLogindActions();
			void				_ApplyLogindAction(BMessage* message);
	static	status_t			_HelperThread(void* data);
			void				_SetLogindMenusEnabled(bool enabled);
			void				_LoadSettings();
			void				_SaveSettings();
			void				_ToggleDeskbarItem();

			PowerStatusDriverInterface* fDriverInterface;

			BStringView*		fBatteryStateLabel;
			BStringView*		fBatteryPercentLabel;
			BStringView*		fBatteryTimeLabel;

			BMenuField*			fPowerKeyMenu;
			BMenuField*			fRebootKeyMenu;
			BMenuField*			fSuspendKeyMenu;
			BMenuField*			fHibernateKeyMenu;
			BMenuField*			fLidMenu;
			BMenuField*			fIdleMenu;

			BMenuField*			fDisplayOffMenu;
			BMenuField*			fBatteryCriticalMenu;
			BCheckBox*			fNotificationsCheckBox;
			BCheckBox*			fDeskbarCheckBox;

			bool				fCanHibernate;
};


#endif	// POWER_VIEW_H
