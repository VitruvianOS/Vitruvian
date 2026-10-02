/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef NETWORK_HOTSPOT_SETTINGS_H
#define NETWORK_HOTSPOT_SETTINGS_H


#include <String.h>
#include <SettingsMessage.h>


// The preflet owns the hotspot profile UUID so every start reuses one
// saved profile named "Hotspot".
class NetworkHotspotSettings {
public:
								NetworkHotspotSettings();

			const BString&		ProfileUUID() const { return fProfileUUID; }
			void				SetProfileUUID(const BString& uuid);

			void				Load();
			void				Save();

private:
			SettingsMessage		fSettingsMessage;
			BString				fProfileUUID;
};


#endif // NETWORK_HOTSPOT_SETTINGS_H
