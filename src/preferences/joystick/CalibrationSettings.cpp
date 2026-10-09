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
static const int32 kSettingsVersion = 1;


static BString
_SettingsFileName(const char* devicePath)
{
	// one file per device node; the node number is stable across boots
	BString name(devicePath);
	name.ReplaceAll('/', '_');

	BString file("joysticks/");
	file << name << ".settings";
	return file;
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
	path.Append(_SettingsFileName(devicePath).String());

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return B_FILE_NOT_FOUND;

	BMessage archive;
	if (archive.Unflatten(&file) != B_OK
		|| archive.what != kSettingsMagic)
		return B_ERROR;

	float value;
	if (archive.FindFloat("dead zone", &value) == B_OK)
		deadZone = std::min(std::max(value, 0.0f), 0.5f);
	if (archive.FindFloat("curve", &value) == B_OK)
		curve = std::min(std::max(value, 0.0f), 1.0f);
	return B_OK;
}


status_t
CalibrationSettings::Save(const char* devicePath) const
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return B_ERROR;
	path.Append(_SettingsFileName(devicePath).String());

	BMessage archive(kSettingsMagic);
	archive.AddFloat("dead zone", deadZone);
	archive.AddFloat("curve", curve);

	BFile file(path.Path(), B_READ_ONLY | B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
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
	float shaped = (1.0f - curve) * magnitude + curve * magnitude * magnitude * magnitude;
	return value < 0.0f ? -shaped : shaped;
}
