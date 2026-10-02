/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NetworkHotspotSettings.h"


NetworkHotspotSettings::NetworkHotspotSettings()
	:
	fSettingsMessage(B_USER_SETTINGS_DIRECTORY, "Network_hotspot")
{
	Load();
}


void
NetworkHotspotSettings::SetProfileUUID(const BString& uuid)
{
	fProfileUUID = uuid;
	Save();
}


void
NetworkHotspotSettings::Load()
{
	fProfileUUID = fSettingsMessage.GetValue("ProfileUUID", BString());
}


void
NetworkHotspotSettings::Save()
{
	fSettingsMessage.SetValue("ProfileUUID", fProfileUUID);
	fSettingsMessage.Save();
}
