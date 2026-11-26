/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SHARE_SETTINGS_H
#define _SHARE_SETTINGS_H


#include <SettingsMessage.h>
#include <String.h>


// Session-side preferences for the Network shares preflet. The share
// definitions themselves live under /etc/vos/network-shares/.
class ShareSettings {
public:
					ShareSettings();

			bool	AutoRefresh() const { return fAutoRefresh; }
			void	SetAutoRefresh(bool value) { fAutoRefresh = value; }

			void	Load();
			void	Save();

private:
			SettingsMessage	fSettingsMessage;
			bool		fAutoRefresh;
};


#endif	// _SHARE_SETTINGS_H
