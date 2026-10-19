/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "CalibrationSettings.h"

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>
#include <String.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <math.h>


static const uint32 kSettingsMagic = 'JoyC';
static const char* kSettingsFile = "Joystick settings";


static BString
_DeviceKey(const char* devicePath)
{
	// device nodes are unique; the key just has to be a flat name
	BString key(devicePath);
	key.ReplaceAll('/', '_');
	return key;
}


CalibrationSettings::CalibrationSettings()
	:
	deadZone(0.05f),
	curve(0.3f)
{
}


status_t
CalibrationSettings::Load(const char* devicePath)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;
	path.Append(kSettingsFile);

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return B_FILE_NOT_FOUND;

	BMessage archive;
	if (archive.Unflatten(&file) != B_OK || archive.what != kSettingsMagic)
		return B_ERROR;

	BMessage device;
	if (archive.FindMessage(_DeviceKey(devicePath).String(), &device)
		!= B_OK)
		return B_ENTRY_NOT_FOUND;

	float value;
	if (device.FindFloat("dead zone", &value) == B_OK)
		deadZone = std::min(std::max(value, 0.0f), 0.5f);
	if (device.FindFloat("curve", &value) == B_OK)
		curve = std::min(std::max(value, 0.0f), 1.0f);
	return B_OK;
}


status_t
CalibrationSettings::Save(const char* devicePath) const
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return B_ERROR;
	path.Append(kSettingsFile);

	// keep the other devices' entries
	BMessage archive(kSettingsMagic);
	BFile existing(path.Path(), B_READ_ONLY);
	if (existing.InitCheck() == B_OK) {
		BMessage old;
		if (old.Unflatten(&existing) == B_OK && old.what == kSettingsMagic)
			archive = old;
	}

	BString key = _DeviceKey(devicePath);
	archive.RemoveName(key.String());

	BMessage device;
	device.AddFloat("dead zone", deadZone);
	device.AddFloat("curve", curve);
	if (archive.AddMessage(key.String(), &device) != B_OK)
		return B_NO_MEMORY;

	BFile file(path.Path(),
		B_READ_ONLY | B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status == B_OK)
		status = archive.Flatten(&file);
	return status;
}


float
CalibrationSettings::Apply(int16 raw) const
{
	float value = raw / 32768.0f;
	if (value > 1.0f)
		value = 1.0f;

	float magnitude = fabsf(value);
	if (magnitude <= deadZone)
		return 0.0f;

	// rescale past the dead zone, then blend linear with cubic
	magnitude = (magnitude - deadZone) / (1.0f - deadZone);
	float shaped = (1.0f - curve) * magnitude
		+ curve * magnitude * magnitude * magnitude;
	return value < 0.0f ? -shaped : shaped;
}
