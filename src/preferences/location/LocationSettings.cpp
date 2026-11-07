/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "LocationSettings.h"


static const char* kDefaultHost = "127.0.0.1";
static const uint16 kDefaultPort = 2947;


LocationSettings::LocationSettings()
	:
	fSettingsMessage(B_USER_SETTINGS_DIRECTORY, "Location_settings"),
	fEnabled(false),
	fHost(kDefaultHost),
	fPort(kDefaultPort),
	fAccuracyLevel(0),
	fSetTimeZoneAutomatically(false),
	fShowDeskbarIndicator(true)
{
}


void
LocationSettings::SetEnabled(bool enabled)
{
	fEnabled = enabled;
}


void
LocationSettings::SetHost(const BString& host)
{
	fHost = host.IsEmpty() ? BString(kDefaultHost) : host;
}


void
LocationSettings::SetPort(uint16 port)
{
	fPort = port != 0 ? port : kDefaultPort;
}


void
LocationSettings::SetAccuracyLevel(int32 level)
{
	if (level < 0 || level > 2)
		level = 0;
	fAccuracyLevel = level;
}


void
LocationSettings::SetSetTimeZoneAutomatically(bool enabled)
{
	fSetTimeZoneAutomatically = enabled;
}


void
LocationSettings::SetShowDeskbarIndicator(bool enabled)
{
	fShowDeskbarIndicator = enabled;
}


void
LocationSettings::LoadSettings()
{
	// Re-read from disk: other processes (the preflet and the indicator
	// share this file) change values while we hold a copy.
	fSettingsMessage.Load();

	SetEnabled(fSettingsMessage.GetValue("Enabled", false));
	SetHost(fSettingsMessage.GetValue("Host", BString(kDefaultHost)));
	SetPort((uint16)fSettingsMessage.GetValue("Port", (int32)kDefaultPort));
	SetAccuracyLevel(fSettingsMessage.GetValue("AccuracyLevel", 0));
	SetSetTimeZoneAutomatically(
		fSettingsMessage.GetValue("SetTimeZoneAutomatically", false));
	SetShowDeskbarIndicator(
		fSettingsMessage.GetValue("ShowDeskbarIndicator", true));
}


void
LocationSettings::SaveSettings()
{
	fSettingsMessage.SetValue("Enabled", fEnabled);
	fSettingsMessage.SetValue("Host", fHost);
	fSettingsMessage.SetValue("Port", (int32)fPort);
	fSettingsMessage.SetValue("AccuracyLevel", fAccuracyLevel);
	fSettingsMessage.SetValue("SetTimeZoneAutomatically",
		fSetTimeZoneAutomatically);
	fSettingsMessage.SetValue("ShowDeskbarIndicator", fShowDeskbarIndicator);

	fSettingsMessage.Save();
}
