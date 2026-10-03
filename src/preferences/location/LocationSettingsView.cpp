/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "LocationSettingsView.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#include <Button.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Entry.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Location"


static const uint32 kMsgToggleEnabled = 'LcEn';
static const uint32 kMsgHostChanged = 'LcHs';
static const uint32 kMsgAccuracyChanged = 'LcAc';
static const uint32 kMsgToggleTimeZone = 'LcTz';
static const uint32 kMsgToggleDeskbar = 'LcDb';
static const uint32 kMsgRefresh = 'LcRf';
static const uint32 kMsgLocationChanged = B_LOCATION_CHANGED;
static const uint32 kMsgLocationStatus = B_LOCATION_STATUS;


static const char*
_FixLabel(int32 fix)
{
	switch (fix) {
		case 2:
			return B_TRANSLATE("2D fix");
		case 3:
			return B_TRANSLATE("3D fix");
		default:
			return B_TRANSLATE("No fix");
	}
}


static const char*
_StatusLabel(int32 status)
{
	switch (status) {
		case B_LOCATION_CONNECTING:
			return B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS);
		case B_LOCATION_NO_FIX:
			return B_TRANSLATE("Connected, no fix yet");
		case B_LOCATION_2D_FIX:
			return B_TRANSLATE("Connected, 2D fix");
		case B_LOCATION_3D_FIX:
			return B_TRANSLATE("Connected, 3D fix");
		default:
			return B_TRANSLATE("Off");
	}
}


LocationSettingsView::LocationSettingsView(const char* name,
	LocationSettings& settings, LocationProvider& provider)
	:
	BView(name, 0),
	fSettings(settings),
	fProvider(provider)
{
	fEnabledCheckBox = new BCheckBox("enabled",
		B_TRANSLATE("Location services"),
		new BMessage(kMsgToggleEnabled));
	fEnabledCheckBox->SetValue(fSettings.Enabled());

	fHostMenu = new BPopUpMenu("host");
	BMenuItem* item = new BMenuItem(B_TRANSLATE("gpsd (localhost:2947)"),
		new BMessage(kMsgHostChanged));
	item->SetMarked(true);
	fHostMenu->AddItem(item);
	fHostMenu->AddItem(new BMenuItem(
		B_TRANSLATE("gpsd (localhost:2948)"),
		new BMessage(kMsgHostChanged)));
	fHostMenu->SetTargetForItems(this);
	fHostField = new BMenuField("hostField",
		B_TRANSLATE("Source:"), fHostMenu);

	fAccuracyMenu = new BPopUpMenu("accuracy");
	fAccuracyMenu->AddItem(new BMenuItem(B_TRANSLATE("Exact"),
		new BMessage(kMsgAccuracyChanged)));
	fAccuracyMenu->AddItem(new BMenuItem(B_TRANSLATE("City"),
		new BMessage(kMsgAccuracyChanged)));
	fAccuracyMenu->AddItem(new BMenuItem(B_TRANSLATE("Country"),
		new BMessage(kMsgAccuracyChanged)));
	fAccuracyMenu->ItemAt(fSettings.AccuracyLevel())->SetMarked(true);
	fAccuracyMenu->SetTargetForItems(this);
	BMenuField* accuracyField = new BMenuField("accuracyField",
		B_TRANSLATE("Accuracy level:"), fAccuracyMenu);

	fStatusView = new BStringView("status", B_TRANSLATE("Off"));
	fDeviceView = new BStringView("device", "");
	fDeviceView->SetFont(be_fixed_font);
	fFixView = new BStringView("fix", "");
	fSatellitesView = new BStringView("satellites", "");

	fLatitudeView = new BStringView("latitude", B_TRANSLATE("Latitude:  —"));
	fLongitudeView = new BStringView("longitude",
		B_TRANSLATE("Longitude: —"));
	fAltitudeView = new BStringView("altitude", B_TRANSLATE("Altitude:  —"));
	fAccuracyView = new BStringView("accuracy", B_TRANSLATE("Accuracy:  —"));
	fSpeedView = new BStringView("speed", B_TRANSLATE("Speed:     —"));
	fHeadingView = new BStringView("heading", B_TRANSLATE("Heading:   —"));
	fUpdatedView = new BStringView("updated", B_TRANSLATE("Updated:   —"));

	fTimeZoneCheckBox = new BCheckBox("timezone",
		B_TRANSLATE("Set time zone automatically"),
		new BMessage(kMsgToggleTimeZone));
	fTimeZoneCheckBox->SetValue(fSettings.SetTimeZoneAutomatically());

	fDeskbarCheckBox = new BCheckBox("deskbar",
		B_TRANSLATE("Show location indicator in Deskbar"),
		new BMessage(kMsgToggleDeskbar));
	fDeskbarCheckBox->SetValue(fSettings.ShowDeskbarIndicator());

	fRefreshButton = new BButton("refresh",
		B_TRANSLATE("Refresh"), new BMessage(kMsgRefresh));

	BStringView* positionHeader = new BStringView("positionHeader",
		B_TRANSLATE("Last position"));
	positionHeader->SetFont(be_bold_font);

	BView* statusBox = BLayoutBuilder::Group<>(B_VERTICAL,
			B_USE_SMALL_SPACING)
		.Add(new BStringView("statusHeader", B_TRANSLATE("Source status")))
		.Add(fStatusView)
		.Add(fDeviceView)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fFixView)
			.Add(fSatellitesView)
		.End()
	.View();
	statusBox->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BView* positionBox = BLayoutBuilder::Group<>(B_VERTICAL,
			B_USE_SMALL_SPACING)
		.Add(positionHeader)
		.Add(fLatitudeView)
		.Add(fLongitudeView)
		.Add(fAltitudeView)
		.Add(fAccuracyView)
		.Add(fSpeedView)
		.Add(fHeadingView)
		.Add(fUpdatedView)
	.View();
	positionBox->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Privacy controls are stubs until a location service enforces them.
	BView* privacyBox = BLayoutBuilder::Group<>(B_VERTICAL,
			B_USE_SMALL_SPACING)
		.Add(new BStringView("privacyHeader", B_TRANSLATE("Privacy")))
		.Add(new BStringView("appsHeader",
			B_TRANSLATE("Apps that used location recently:")))
		.Add(new BStringView("appsEmpty",
			B_TRANSLATE("(none recorded yet — per-app permission is a stub)")))
		.Add(fTimeZoneCheckBox)
		.Add(fDeskbarCheckBox)
		.AddGlue()
		.Add(fRefreshButton)
	.View();
	privacyBox->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BLayoutBuilder::Grid<>(this)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fEnabledCheckBox, 0, 0, 2, 1)
		.Add(fHostField->CreateLabelLayoutItem(), 0, 1)
		.Add(fHostField->CreateMenuBarLayoutItem(), 1, 1)
		.Add(accuracyField->CreateLabelLayoutItem(), 0, 2)
		.Add(accuracyField->CreateMenuBarLayoutItem(), 1, 2)
		.Add(statusBox, 0, 3, 2, 1)
		.Add(positionBox, 0, 4, 2, 1)
		.Add(privacyBox, 0, 5, 2, 1)
	.End();
}


LocationSettingsView::~LocationSettingsView()
{
	fProvider.Unwatch(BMessenger(this));
}


void
LocationSettingsView::AttachedToWindow()
{
	if (Parent() != NULL)
		SetViewColor(Parent()->ViewColor());
	else
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fEnabledCheckBox->SetTarget(this);
	fTimeZoneCheckBox->SetTarget(this);
	fDeskbarCheckBox->SetTarget(this);
	fRefreshButton->SetTarget(this);

	fProvider.Watch(BMessenger(this));

	// Start is owned by LocationWindow; only refresh from whatever the
	// provider already knows.
	RefreshFromProvider();
}


void
LocationSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleEnabled:
		{
			bool enabled = fEnabledCheckBox->Value() == B_CONTROL_ON;
			fSettings.SetEnabled(enabled);
			fSettings.SaveSettings();
			fProvider.SetAccuracyLevel(fSettings.AccuracyLevel());
			if (enabled)
				fProvider.Start();
			else
				fProvider.Stop();
			_UpdateStatusViews();
			break;
		}
		case kMsgHostChanged:
		{
			int32 index = fHostMenu->IndexOf(fHostMenu->FindMarked());
			fSettings.SetHost("127.0.0.1");
			fSettings.SetPort(index == 1 ? 2948 : 2947);
			fSettings.SaveSettings();
			break;
		}
		case kMsgAccuracyChanged:
		{
			int32 index = fAccuracyMenu->IndexOf(fAccuracyMenu->FindMarked());
			fSettings.SetAccuracyLevel(index);
			fProvider.SetAccuracyLevel(index);
			fSettings.SaveSettings();
			break;
		}
		case kMsgToggleTimeZone:
		{
			fSettings.SetSetTimeZoneAutomatically(
				fTimeZoneCheckBox->Value() == B_CONTROL_ON);
			fSettings.SaveSettings();
			break;
		}
		case kMsgToggleDeskbar:
		{
			fSettings.SetShowDeskbarIndicator(
				fDeskbarCheckBox->Value() == B_CONTROL_ON);
			fSettings.SaveSettings();
			if (fSettings.ShowDeskbarIndicator())
				_EnsureDeskbarIndicator();
			break;
		}
		case kMsgRefresh:
			RefreshFromProvider();
			break;
		case kMsgLocationStatus:
			_UpdateStatusViews();
			break;
		case kMsgLocationChanged:
		{
			_UpdatePositionViews();
			_UpdateStatusViews();
			break;
		}
		default:
			BView::MessageReceived(message);
			break;
	}
}


void
LocationSettingsView::RefreshFromProvider()
{
	_UpdateStatusViews();
	_UpdatePositionViews();
}


void
LocationSettingsView::_UpdateStatusViews()
{
	int32 status = fSettings.Enabled()
		? (fProvider.IsConnected()
			? (fProvider.FixType() >= 3 ? B_LOCATION_3D_FIX
				: fProvider.FixType() == 2 ? B_LOCATION_2D_FIX
				: B_LOCATION_NO_FIX)
			: B_LOCATION_CONNECTING)
		: B_LOCATION_OFFLINE;

	fStatusView->SetText(_StatusLabel(status));

	BString device(B_TRANSLATE("Device: "));
	if (fProvider.DevicePath()[0] != '\0')
		device << fProvider.DevicePath();
	else
		device << B_TRANSLATE("(unknown)");
	device << "  |  " << fProvider.SourceName()
		<< " " << fProvider.Host() << ":" << fProvider.Port();
	fDeviceView->SetText(device.String());

	BString fix(B_TRANSLATE("Fix: "));
	fix << _FixLabel(fProvider.FixType());
	fFixView->SetText(fix.String());

	BString sats(B_TRANSLATE("Sats: "));
	sats << fProvider.SatellitesInUse() << B_TRANSLATE(" used / ")
		<< fProvider.SatellitesInView() << B_TRANSLATE(" in view");
	fSatellitesView->SetText(sats.String());
}


void
LocationSettingsView::_UpdatePositionViews()
{
	double latitude = 0.0;
	double longitude = 0.0;
	double altitude = 0.0;
	double accuracy = 0.0;
	double speed = 0.0;
	double heading = 0.0;
	int64 timestamp = 0;
	fProvider.LastLocation(&latitude, &longitude, &altitude, &accuracy,
		&speed, &heading, &timestamp);

	BString text;

	text.SetToFormat("%s %.6f° %s",
		B_TRANSLATE("Latitude: "),
		latitude, latitude >= 0.0 ? "N" : "S");
	fLatitudeView->SetText(text.String());

	text.SetToFormat("%s %.6f° %s",
		B_TRANSLATE("Longitude: "),
		longitude, longitude >= 0.0 ? "E" : "W");
	fLongitudeView->SetText(text.String());

	text.SetToFormat("%s %.1f m",
		B_TRANSLATE("Altitude:  "), altitude);
	fAltitudeView->SetText(text.String());

	text.SetToFormat("%s ±%.1f m",
		B_TRANSLATE("Accuracy:  "), accuracy);
	fAccuracyView->SetText(text.String());

	text.SetToFormat("%s %.1f km/h",
		B_TRANSLATE("Speed:     "), speed * 3.6);
	fSpeedView->SetText(text.String());

	if (heading > 0.0)
		text.SetToFormat("%s %.0f°", B_TRANSLATE("Heading:   "), heading);
	else
		text.SetToFormat("%s —", B_TRANSLATE("Heading:   "));
	fHeadingView->SetText(text.String());

	if (timestamp > 0) {
		time_t seconds = (time_t)(timestamp / 1000000LL);
		struct tm* utc = gmtime(&seconds);
		char buffer[64];
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", utc);
		text.SetToFormat("%s %s", B_TRANSLATE("Updated:   "), buffer);
	} else {
		text.SetToFormat("%s —", B_TRANSLATE("Updated:   "));
	}
	fUpdatedView->SetText(text.String());
}


void
LocationSettingsView::_EnsureDeskbarIndicator()
{
	// Install LocationStatus into Deskbar when the preflet checkbox turns on.
	BDeskbar deskbar;
	if (!deskbar.IsRunning()
		|| deskbar.HasItem("LocationStatus"))
		return;

	BPath path("/system/apps/LocationStatus");
	entry_ref ref;
	if (get_ref_for_path(path.Path(), &ref) == B_OK)
		deskbar.AddItem(&ref);
}
