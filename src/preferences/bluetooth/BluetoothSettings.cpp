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
	fInquiryTime(kDefaultInquiryTime),
	fReceiveFiles(false)
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
BluetoothSettings::SetReceiveFiles(bool enable)
{
	fReceiveFiles = enable;
}


bool
BluetoothSettings::AlwaysAccept(const BString& address) const
{
	if (address.IsEmpty())
		return false;

	const char* found = NULL;
	int32 index = 0;
	while (fAlwaysAccept.FindString("address", index, &found) == B_OK) {
		if (found != NULL && address == found)
			return true;
		index++;
	}
	return false;
}


void
BluetoothSettings::SetAlwaysAccept(const BString& address, bool enable)
{
	if (address.IsEmpty())
		return;

	BMessage updated;
	const char* found = NULL;
	int32 index = 0;
	while (fAlwaysAccept.FindString("address", index, &found) == B_OK) {
		if (found != NULL && address != found)
			updated.AddString("address", found);
		index++;
	}
	if (enable)
		updated.AddString("address", address.String());
	fAlwaysAccept = updated;
}


void
BluetoothSettings::LoadSettings()
{
	SetPickedAdapterPath(fSettingsMessage.GetValue("AdapterPath", BString()));
	SetInquiryTime(fSettingsMessage.GetValue("InquiryTime",
		kDefaultInquiryTime));
	SetReceiveFiles(fSettingsMessage.GetValue("ReceiveFiles", false));
	fAlwaysAccept = fSettingsMessage.GetValue("AlwaysAcceptDevices",
		BMessage());
}


void
BluetoothSettings::SaveSettings()
{
	fSettingsMessage.SetValue("AdapterPath", fPickedAdapterPath);
	fSettingsMessage.SetValue("InquiryTime", fInquiryTime);
	fSettingsMessage.SetValue("ReceiveFiles", fReceiveFiles);
	fSettingsMessage.SetValue("AlwaysAcceptDevices", fAlwaysAccept);

	fSettingsMessage.Save();
}
