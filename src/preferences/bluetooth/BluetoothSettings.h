/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Client-side-only Bluetooth preflet settings. BlueZ has no daemon-side
 * concept of "default inquiry time" (StartDiscovery is open-ended) or a
 * remembered adapter choice, so both live here instead of round-tripping
 * through the kit.
 */
#ifndef BLUETOOTH_SETTINGS_H
#define BLUETOOTH_SETTINGS_H

#include <String.h>
#include <SettingsMessage.h>


class BluetoothSettings {
public:
								BluetoothSettings();

			const BString&		PickedAdapterPath() const
									{ return fPickedAdapterPath; }
			int32				InquiryTime() const { return fInquiryTime; }

			void				SetPickedAdapterPath(const BString& path);
			void				SetInquiryTime(int32 seconds);

			void				LoadSettings();
			void				SaveSettings();

private:
			SettingsMessage		fSettingsMessage;

			BString				fPickedAdapterPath;
			int32				fInquiryTime;
};

#endif // BLUETOOTH_SETTINGS_H
