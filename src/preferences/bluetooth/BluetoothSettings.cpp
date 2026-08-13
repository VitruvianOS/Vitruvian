/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "BluetoothSettings.h"


// Matches the slider's lower bound (BluetoothSettingsView), not
// DiscoveryAgent's own BT_DEFAULT_INQUIRY_TIME_SECS -- that default is what
// InquiryPanel falls back to when the preflet has never set a preference.
static const int32 kDefaultInquiryTime = 15;


BluetoothSettings::BluetoothSettings()
	:
	fSettingsMessage(B_USER_SETTINGS_DIRECTORY, "Bluetooth_settings"),
	fInquiryTime(kDefaultInquiryTime)
{
}


void
BluetoothSettings::SetPickedAdapterPath(const BString& path)
{
	fPickedAdapterPath = path;
}


void
BluetoothSettings::SetInquiryTime(int32 seconds)
{
	fInquiryTime = seconds;
}


void
BluetoothSettings::LoadSettings()
{
	SetPickedAdapterPath(fSettingsMessage.GetValue("AdapterPath", BString()));
	SetInquiryTime(fSettingsMessage.GetValue("InquiryTime",
		kDefaultInquiryTime));
}


void
BluetoothSettings::SaveSettings()
{
	fSettingsMessage.SetValue("AdapterPath", fPickedAdapterPath);
	fSettingsMessage.SetValue("InquiryTime", fInquiryTime);

	fSettingsMessage.Save();
}
