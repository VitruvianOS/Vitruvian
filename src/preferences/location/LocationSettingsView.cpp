/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "LocationSettingsView.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
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
#include <OS.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <StringView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Location"


static const uint32 kMsgToggleEnabled = 'LcEn';
static const uint32 kMsgHostChanged = 'LcHs';
static const uint32 kMsgAccuracyChanged = 'LcAc';
static const uint32 kMsgToggleTimeZone = 'LcTz';
static const uint32 kMsgToggleDeskbar = 'LcDb';
static const uint32 kMsgRefresh = 'LcRf';
static const uint32 kMsgDeskbarToggled = 'LcDk';
static const uint32 kMsgLocationChanged = B_LOCATION_CHANGED;
static const uint32 kMsgLocationStatus = B_LOCATION_STATUS;

// Must match LocationStatus.rdef's app_signature, and BDeskbar's lookup
// key for the replicant is the view name it archives itself under.
static const char* kLocationStatusSignature
	= "application/x-vnd.vos-LocationStatus";
static const char* kLocationStatusDeskbarItemName = "LocationStatus";


struct _DeskbarToggleJob {
	BMessenger replyTo;
	bool install;
};


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


static const char*
_NoValue()
{
	return B_TRANSLATE("-");
}


static BStringView*
_Label(const char* name, const char* text)
{
	return new BStringView(name, text);
}


LocationSettingsView::LocationSettingsView(const char* name,
	LocationSettings& settings, LocationProvider& provider)
	:
	BView(name, 0),
	fSettings(settings),
	fProvider(provider),
	fDeskbarTogglePending(false)
{
	fEnabledCheckBox = new BCheckBox("enabled",
		B_TRANSLATE("Location services"),
		new BMessage(kMsgToggleEnabled));
	fEnabledCheckBox->SetValue(fSettings.Enabled());
	fEnabledCheckBox->SetFont(be_bold_font);

	fStatusLabel = new BStringView("status", B_TRANSLATE("Off"));

	fHostMenu = new BPopUpMenu("host");
	BMenuItem* item = new BMenuItem(B_TRANSLATE("gpsd (localhost:2947)"),
		new BMessage(kMsgHostChanged));
	item->SetMarked(true);
	fHostMenu->AddItem(item);
	fHostMenu->AddItem(new BMenuItem(
		B_TRANSLATE("gpsd (localhost:2948)"),
		new BMessage(kMsgHostChanged)));
	fHostField = new BMenuField("hostField",
		B_TRANSLATE("Server:"), fHostMenu);

	fAccuracyMenu = new BPopUpMenu("accuracy");
	fAccuracyMenu->AddItem(new BMenuItem(B_TRANSLATE("Exact"),
		new BMessage(kMsgAccuracyChanged)));
	fAccuracyMenu->AddItem(new BMenuItem(B_TRANSLATE("City"),
		new BMessage(kMsgAccuracyChanged)));
	fAccuracyMenu->AddItem(new BMenuItem(B_TRANSLATE("Country"),
		new BMessage(kMsgAccuracyChanged)));
	fAccuracyMenu->ItemAt(fSettings.AccuracyLevel())->SetMarked(true);
	fAccuracyField = new BMenuField("accuracyField",
		B_TRANSLATE("Accuracy level:"), fAccuracyMenu);

	fFixValue = _ValueView("fix");
	fSatellitesValue = _ValueView("satellites");
	fAccuracyValue = _ValueView("accuracy");
	fSourceValue = _ValueView("source");
	fDeviceValue = _ValueView("device");

	fLatitudeValue = _ValueView("latitude");
	fLongitudeValue = _ValueView("longitude");
	fAltitudeValue = _ValueView("altitude");
	fSpeedValue = _ValueView("speed");
	fHeadingValue = _ValueView("heading");
	fUpdatedValue = _ValueView("updated");

	fTimeZoneCheckBox = new BCheckBox("timezone",
		B_TRANSLATE("Set time zone automatically"),
		new BMessage(kMsgToggleTimeZone));
	fTimeZoneCheckBox->SetValue(fSettings.SetTimeZoneAutomatically());

	fDeskbarCheckBox = new BCheckBox("deskbar",
		B_TRANSLATE("Show location indicator in Deskbar"),
		new BMessage(kMsgToggleDeskbar));
	fDeskbarCheckBox->SetValue(_IsDeskbarIndicatorInstalled()
		? B_CONTROL_ON : B_CONTROL_OFF);

	fRefreshButton = new BButton("refresh",
		B_TRANSLATE("Refresh"), new BMessage(kMsgRefresh));

	BStringView* statusHeader = new BStringView("statusHeader",
		B_TRANSLATE("Status"));
	statusHeader->SetFont(be_bold_font);

	BStringView* positionHeader = new BStringView("positionHeader",
		B_TRANSLATE("Last position"));
	positionHeader->SetFont(be_bold_font);

	BStringView* privacyHeader = new BStringView("privacyHeader",
		B_TRANSLATE("Privacy"));
	privacyHeader->SetFont(be_bold_font);

	BStringView* sourceHeader = new BStringView("sourceHeader",
		B_TRANSLATE("Source"));
	sourceHeader->SetFont(be_bold_font);

	// Per-app access has no backend yet, so it is not shown at all: a
	// placeholder list would read as a working permission manager.
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fEnabledCheckBox)
			.AddGlue()
			.Add(fStatusLabel)
		.End()
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			// one grid keeps the status and position values aligned
			.Add(statusHeader, 0, 0, 2, 1)
			.Add(_Label("fixLabel", B_TRANSLATE("Fix:")), 0, 1)
			.Add(fFixValue, 1, 1)
			.Add(_Label("satsLabel", B_TRANSLATE("Satellites:")), 0, 2)
			.Add(fSatellitesValue, 1, 2)
			.Add(_Label("accuracyLabel", B_TRANSLATE("Accuracy:")), 0, 3)
			.Add(fAccuracyValue, 1, 3)
			.Add(_Label("sourceLabel", B_TRANSLATE("Source:")), 0, 4)
			.Add(fSourceValue, 1, 4)
			.Add(_Label("deviceLabel", B_TRANSLATE("Device:")), 0, 5)
			.Add(fDeviceValue, 1, 5)
			.Add(positionHeader, 0, 6, 2, 1)
			.Add(_Label("latitudeLabel", B_TRANSLATE("Latitude:")), 0, 7)
			.Add(fLatitudeValue, 1, 7)
			.Add(_Label("longitudeLabel", B_TRANSLATE("Longitude:")), 0, 8)
			.Add(fLongitudeValue, 1, 8)
			.Add(_Label("altitudeLabel", B_TRANSLATE("Altitude:")), 0, 9)
			.Add(fAltitudeValue, 1, 9)
			.Add(_Label("speedLabel", B_TRANSLATE("Speed:")), 0, 10)
			.Add(fSpeedValue, 1, 10)
			.Add(_Label("headingLabel", B_TRANSLATE("Heading:")), 0, 11)
			.Add(fHeadingValue, 1, 11)
			.Add(_Label("updatedLabel", B_TRANSLATE("Updated:")), 0, 12)
			.Add(fUpdatedValue, 1, 12)
			.SetColumnWeight(1, 1.0f)
		.End()
		.Add(privacyHeader)
		.AddGrid(B_USE_SMALL_SPACING)
			.Add(fAccuracyField->CreateLabelLayoutItem(), 0, 0)
			.Add(fAccuracyField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(fTimeZoneCheckBox, 0, 1, 2, 1)
			.Add(fDeskbarCheckBox, 0, 2, 2, 1)
		.End()
		.Add(sourceHeader)
		.Add(fHostField)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fRefreshButton)
		.End()
	.End();
}


LocationSettingsView::~LocationSettingsView()
{
	fProvider.Unwatch(BMessenger(this));
}


BStringView*
LocationSettingsView::_ValueView(const char* name)
{
	BStringView* view = new BStringView(name, _NoValue());
	return view;
}


void
LocationSettingsView::AttachedToWindow()
{
	if (Parent() != NULL)
		SetViewColor(Parent()->ViewColor());
	else
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Items resolve their target when the menu is installed: a messenger
	// built before this view has a looper never delivers.
	fHostMenu->SetTargetForItems(this);
	fAccuracyMenu->SetTargetForItems(this);

	fEnabledCheckBox->SetTarget(this);
	fTimeZoneCheckBox->SetTarget(this);
	fRefreshButton->SetTarget(this);

	// The Deskbar is the source of truth: a setting that says "shown" with
	// no replicant in the tray is a lie the checkbox would repeat.
	fSettings.SetShowDeskbarIndicator(_IsDeskbarIndicatorInstalled());
	fDeskbarCheckBox->SetValue(_IsDeskbarIndicatorInstalled()
		? B_CONTROL_ON : B_CONTROL_OFF);
	fDeskbarCheckBox->SetTarget(this);

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
			_UpdatePositionViews();
			break;
		}
		case kMsgHostChanged:
		{
			int32 index = fHostMenu->IndexOf(fHostMenu->FindMarked());
			fSettings.SetHost("127.0.0.1");
			fSettings.SetPort(index == 1 ? 2948 : 2947);
			fSettings.SaveSettings();
			// Without this the menu only took effect on the next launch.
			fProvider.SetEndpoint(fSettings.Host().String(),
				fSettings.Port());
			_UpdateStatusViews();
			break;
		}
		case kMsgAccuracyChanged:
		{
			int32 index = fAccuracyMenu->IndexOf(fAccuracyMenu->FindMarked());
			fSettings.SetAccuracyLevel(index);
			fProvider.SetAccuracyLevel(index);
			fSettings.SaveSettings();
			// The coarsened coordinates are what the user asked to see.
			_UpdateStatusViews();
			_UpdatePositionViews();
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
			// Saved from the reply in _ApplyDeskbarIndicator(), so a failed
			// Deskbar round trip cannot leave the setting out of sync.
			_ToggleDeskbarIndicator(
				fDeskbarCheckBox->Value() == B_CONTROL_ON);
			break;
		case kMsgDeskbarToggled:
			_ApplyDeskbarIndicator(message);
			break;
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

	fStatusLabel->SetText(_StatusLabel(status));

	fFixValue->SetText(_FixLabel(fProvider.FixType()));

	// Without a fix the source reports no satellites in use, so a stale
	// "8 used" from the last fix would be misleading.
	BString sats;
	if (fProvider.FixType() > 0)
		sats << fProvider.SatellitesInUse() << B_TRANSLATE(" used / ")
			<< fProvider.SatellitesInView() << B_TRANSLATE(" in view");
	else
		sats.SetTo(_NoValue());
	fSatellitesValue->SetText(sats.String());

	double latitude = 0.0;
	double longitude = 0.0;
	double accuracy = 0.0;
	int64 timestamp = 0;
	fProvider.LastLocation(&latitude, &longitude, NULL, &accuracy, NULL,
		NULL, &timestamp);

	BString text;
	if (timestamp > 0)
		text.SetToFormat("±%.1f m", accuracy);
	else
		text.SetTo(_NoValue());
	fAccuracyValue->SetText(text.String());

	BString source;
	if (fProvider.SourceName()[0] != '\0')
		source << fProvider.SourceName();
	else
		source << B_TRANSLATE("(unknown)");
	source << " " << fProvider.Host() << ":" << fProvider.Port();
	fSourceValue->SetText(source.String());

	BString device;
	if (fProvider.DevicePath()[0] != '\0')
		device << fProvider.DevicePath();
	else
		device << B_TRANSLATE("(unknown)");
	fDeviceValue->SetText(device.String());
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

	// No fix yet: keep the rows empty instead of showing null island.
	if (timestamp <= 0) {
		fLatitudeValue->SetText(_NoValue());
		fLongitudeValue->SetText(_NoValue());
		fAltitudeValue->SetText(_NoValue());
		fSpeedValue->SetText(_NoValue());
		fHeadingValue->SetText(_NoValue());
		fUpdatedValue->SetText(_NoValue());
		return;
	}

	BString text;

	text.SetToFormat("%.6f° %s",
		latitude, latitude >= 0.0 ? "N" : "S");
	fLatitudeValue->SetText(text.String());

	text.SetToFormat("%.6f° %s",
		longitude, longitude >= 0.0 ? "E" : "W");
	fLongitudeValue->SetText(text.String());

	text.SetToFormat("%.1f m", altitude);
	fAltitudeValue->SetText(text.String());

	text.SetToFormat("%.1f km/h", speed * 3.6);
	fSpeedValue->SetText(text.String());

	if (heading > 0.0)
		text.SetToFormat("%.0f°", heading);
	else
		text.SetTo(_NoValue());
	fHeadingValue->SetText(text.String());

	time_t seconds = (time_t)(timestamp / 1000000LL);
	struct tm* utc = gmtime(&seconds);
	char buffer[64];
	strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", utc);
	fUpdatedValue->SetText(buffer);
}


bool
LocationSettingsView::_IsDeskbarIndicatorInstalled()
{
	BDeskbar deskbar;
	return deskbar.IsRunning()
		&& deskbar.HasItem(kLocationStatusDeskbarItemName);
}


void
LocationSettingsView::_ToggleDeskbarIndicator(bool install)
{
	// Ignore further clicks while one Deskbar round trip is in flight;
	// BDeskbar calls target Deskbar's own team and freeze when done inline.
	if (fDeskbarTogglePending)
		return;

	fDeskbarTogglePending = true;
	fDeskbarCheckBox->SetEnabled(false);

	_DeskbarToggleJob* job = new _DeskbarToggleJob;
	job->replyTo = BMessenger(this);
	job->install = install;

	thread_id thread = spawn_thread(_ToggleDeskbarThread,
		"toggle_location_indicator", B_NORMAL_PRIORITY, job);
	if (thread >= B_OK) {
		resume_thread(thread);
	} else {
		delete job;
		fDeskbarTogglePending = false;
		fDeskbarCheckBox->SetEnabled(true);
	}
}


int32
LocationSettingsView::_ToggleDeskbarThread(void* data)
{
	_DeskbarToggleJob* job = (_DeskbarToggleJob*)data;

	BDeskbar deskbar;
	status_t status = B_OK;
	if (job->install) {
		entry_ref ref;
		status = be_roster->FindApp(kLocationStatusSignature, &ref);
		if (status == B_OK)
			status = deskbar.AddItem(&ref);
	} else
		status = deskbar.RemoveItem(kLocationStatusDeskbarItemName);

	BMessage reply(kMsgDeskbarToggled);
	reply.AddInt32("status", status);
	reply.AddBool("installed",
		deskbar.HasItem(kLocationStatusDeskbarItemName));
	job->replyTo.SendMessage(&reply);

	delete job;
	return 0;
}


void
LocationSettingsView::_ApplyDeskbarIndicator(BMessage* message)
{
	fDeskbarTogglePending = false;
	fDeskbarCheckBox->SetEnabled(true);

	bool installed = false;
	message->FindBool("installed", &installed);
	fDeskbarCheckBox->SetValue(installed ? B_CONTROL_ON : B_CONTROL_OFF);
	fSettings.SetShowDeskbarIndicator(installed);
	fSettings.SaveSettings();

	int32 status = B_OK;
	message->FindInt32("status", &status);
	if (status == B_OK)
		return;

	BString text(B_TRANSLATE(
		"Couldn't update the location indicator in the Deskbar: %error%"));
	text.ReplaceFirst("%error%",
		status == B_TIMED_OUT ? B_TRANSLATE("Deskbar did not reply in "
			"time.") : strerror(status));
	BAlert* alert = new BAlert(B_TRANSLATE("Error"), text,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->Go();
}
