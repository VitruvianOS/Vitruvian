/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ShareSettings.h"


ShareSettings::ShareSettings()
	:
	fSettingsMessage(B_USER_SETTINGS_DIRECTORY, "network_shares"),
	fAutoRefresh(true)
{
	Load();
}


void
ShareSettings::Load()
{
	fAutoRefresh = fSettingsMessage.GetValue("AutoRefresh", true);
}


void
ShareSettings::Save()
{
	fSettingsMessage.SetValue("AutoRefresh", fAutoRefresh);
	fSettingsMessage.Save();
}
