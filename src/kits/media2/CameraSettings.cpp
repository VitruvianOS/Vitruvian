/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <media2/CameraSettings.h>

#include <Message.h>
#include <String.h>

#include <SettingsMessage.h>


static const char* kSettingsFile = "Camera_settings";


BCameraSettings::BCameraSettings()
	:
	fDefaultCameraId(""),
	fDevices(),
	fSettingsMessage(new SettingsMessage(B_USER_SETTINGS_DIRECTORY,
		kSettingsFile))
{
}


status_t
BCameraSettings::Load()
{
	SettingsMessage* settings = (SettingsMessage*)fSettingsMessage;
	if (settings == NULL)
		return B_NO_MEMORY;
	if (settings->Load() != B_OK)
		return B_ERROR;

	fDefaultCameraId = settings->GetValue("default_camera_id", BString());
	fDevices = settings->GetValue("devices", BMessage());
	return B_OK;
}


status_t
BCameraSettings::Save() const
{
	SettingsMessage* settings = (SettingsMessage*)fSettingsMessage;
	if (settings == NULL)
		return B_NO_MEMORY;

	settings->SetValue("default_camera_id", fDefaultCameraId);
	settings->SetValue("devices", fDevices);
	return settings->Save();
}


void
BCameraSettings::SetDefaultCameraId(const BString& id)
{
	fDefaultCameraId = id;
}


bool
BCameraSettings::HasDevice(const BString& stableId) const
{
	if (stableId.IsEmpty())
		return false;
	BMessage device;
	return fDevices.FindMessage(stableId.String(), &device) == B_OK;
}


status_t
BCameraSettings::GetDevice(const BString& stableId, camera_device_info* outInfo,
	BMessage* outControls) const
{
	if (stableId.IsEmpty() || outInfo == NULL)
		return B_BAD_VALUE;

	BMessage device;
	if (fDevices.FindMessage(stableId.String(), &device) != B_OK)
		return B_ENTRY_NOT_FOUND;

	outInfo->stableId = stableId;
	int32 location = B_CAMERA_LOCATION_UNKNOWN;
	device.FindInt32("location", &location);
	outInfo->location = (camera_location)location;
	BString name;
	if (device.FindString("name", &name) == B_OK)
		outInfo->name = name;
	outInfo->id = 0;
	outInfo->nodeName = "";
	outInfo->isDefault = (fDefaultCameraId == stableId);

	if (outControls != NULL)
		*outControls = device;
	return B_OK;
}


status_t
BCameraSettings::SetDevice(const camera_device_info& info,
	const BMessage* controls)
{
	if (info.stableId.IsEmpty())
		return B_BAD_VALUE;

	BMessage device;
	int32 location = info.location;
	device.AddInt32("location", location);
	if (!info.name.IsEmpty())
		device.AddString("name", info.name);

	if (controls != NULL) {
		float value = 0;
		bool flag = false;
		if (controls->FindFloat("brightness", &value) == B_OK)
			device.AddFloat("brightness", value);
		if (controls->FindFloat("contrast", &value) == B_OK)
			device.AddFloat("contrast", value);
		if (controls->FindBool("exposure_auto", &flag) == B_OK)
			device.AddBool("exposure_auto", flag);
		if (controls->FindFloat("exposure", &value) == B_OK)
			device.AddFloat("exposure", value);
		if (controls->FindBool("white_balance_auto", &flag) == B_OK)
			device.AddBool("white_balance_auto", flag);
		if (controls->FindFloat("white_balance", &value) == B_OK)
			device.AddFloat("white_balance", value);
		if (controls->FindBool("focus_auto", &flag) == B_OK)
			device.AddBool("focus_auto", flag);
		if (controls->FindFloat("focus", &value) == B_OK)
			device.AddFloat("focus", value);
		if (controls->FindFloat("power_line_freq", &value) == B_OK)
			device.AddFloat("power_line_freq", value);
		if (controls->FindBool("mirror", &flag) == B_OK)
			device.AddBool("mirror", flag);
	}

	fDevices.RemoveName(info.stableId.String());
	fDevices.AddMessage(info.stableId.String(), &device);
	return B_OK;
}
