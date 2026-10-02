/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Client-side Bluetooth preflet settings. Receive-files and always-accept
 * are read by the Deskbar BluetoothStatus receive agent independently.
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

			// Off by default: never receive Bluetooth pushes unless the
			// user turns this on in the preflet.
			bool				ReceiveFiles() const { return fReceiveFiles; }

			bool				AlwaysAccept(const BString& address) const;

			void				SetPickedAdapterPath(const BString& path);
			void				SetInquiryTime(int32 seconds);
			void				SetReceiveFiles(bool enable);
			void				SetAlwaysAccept(const BString& address,
									bool enable);

			void				LoadSettings();
			void				SaveSettings();

private:
			SettingsMessage		fSettingsMessage;

			BString				fPickedAdapterPath;
			int32				fInquiryTime;
			bool				fReceiveFiles;
			// Flat list of addresses under "AlwaysAcceptDevices".
			BMessage			fAlwaysAccept;
};

#endif // BLUETOOTH_SETTINGS_H
