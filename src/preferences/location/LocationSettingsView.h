/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef LOCATION_SETTINGS_VIEW_H
#define LOCATION_SETTINGS_VIEW_H

#include <CheckBox.h>
#include <Handler.h>
#include <StringView.h>
#include <View.h>

#include "LocationProvider.h"
#include "LocationSettings.h"


class BMenuField;
class BMessage;
class BPopUpMenu;
class BButton;


class LocationSettingsView : public BView {
public:
								LocationSettingsView(const char* name,
									LocationSettings& settings,
									LocationProvider& provider);
	virtual						~LocationSettingsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				RefreshFromProvider();
			LocationSettings&	Settings() const { return fSettings; }

private:
			void				_UpdateStatusViews();
			void				_UpdatePositionViews();

private:
			LocationSettings&	fSettings;
			LocationProvider&	fProvider;

			BCheckBox*			fEnabledCheckBox;
			BMenuField*			fHostField;
			BPopUpMenu*			fHostMenu;
			BPopUpMenu*			fAccuracyMenu;

			BStringView*		fStatusView;
			BStringView*		fDeviceView;
			BStringView*		fFixView;
			BStringView*		fSatellitesView;
			BStringView*		fLatitudeView;
			BStringView*		fLongitudeView;
			BStringView*		fAltitudeView;
			BStringView*		fAccuracyView;
			BStringView*		fSpeedView;
			BStringView*		fHeadingView;
			BStringView*		fUpdatedView;

			BCheckBox*			fTimeZoneCheckBox;
			BCheckBox*			fDeskbarCheckBox;
			BButton*			fRefreshButton;
};


#endif	// LOCATION_SETTINGS_VIEW_H
