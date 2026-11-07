/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Client-side-only Location preflet settings. gpsd has no daemon-side
 * concept of "user turned location off" or "prefer city accuracy", so
 * those live here rather than round-tripping through a service.
 */
#ifndef LOCATION_SETTINGS_H
#define LOCATION_SETTINGS_H

#include <String.h>
#include <SettingsMessage.h>


class LocationSettings {
public:
								LocationSettings();

			bool				Enabled() const { return fEnabled; }
			const BString&		Host() const { return fHost; }
			uint16				Port() const { return fPort; }
			int32				AccuracyLevel() const { return fAccuracyLevel; }
			bool				SetTimeZoneAutomatically() const
									{ return fSetTimeZoneAutomatically; }
			bool				ShowDeskbarIndicator() const
									{ return fShowDeskbarIndicator; }

			void				SetEnabled(bool enabled);
			void				SetHost(const BString& host);
			void				SetPort(uint16 port);
			void				SetAccuracyLevel(int32 level);
			void				SetSetTimeZoneAutomatically(bool enabled);
			void				SetShowDeskbarIndicator(bool enabled);

			void				LoadSettings();
			void				SaveSettings();

private:
			SettingsMessage		fSettingsMessage;

			bool				fEnabled;
			BString				fHost;
			uint16				fPort;
			int32				fAccuracyLevel;
			bool				fSetTimeZoneAutomatically;
			bool				fShowDeskbarIndicator;
};


#endif	// LOCATION_SETTINGS_H
