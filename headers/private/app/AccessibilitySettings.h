/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Shared accessibility settings file used by the preflet, Deskbar,
 * input_server filter, notification_server and app_server.
 */
#ifndef ACCESSIBILITY_SETTINGS_H
#define ACCESSIBILITY_SETTINGS_H


#include <FindDirectory.h>
#include <File.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <media2/MediaGraph.h>
#include <Message.h>
#include <Path.h>
#include <SupportDefs.h>


#define accessibility_settings_file "Accessibility settings"


typedef struct {
	bool		high_contrast;
	bool		cursor_override;
	float		cursor_scale;
	bool		reduce_motion;

	bool		sticky_keys;
	bool		slow_keys;
	float		slow_keys_delay;
	bool		mouse_keys;
	int32		mouse_keys_speed;

	bool		visual_alerts;
	bool		mono_audio;

	bool		temp_schedule_enabled;
	float		temp_day_kelvin;
	float		temp_night_kelvin;
	int32		temp_day_start;
	int32		temp_night_start;
} accessibility_settings;


static const accessibility_settings kDefaultAccessibilitySettings = {
	false,		// high_contrast
	false,		// cursor_override
	1.0f,		// cursor_scale
	false,		// reduce_motion

	false,		// sticky_keys
	false,		// slow_keys
	0.5f,		// slow_keys_delay
	false,		// mouse_keys
	2,			// mouse_keys_speed (0 slow .. 4 fast)

	false,		// visual_alerts
	false,		// mono_audio

	false,		// temp_schedule_enabled
	6500.0f,	// temp_day_kelvin
	4500.0f,	// temp_night_kelvin
	8 * 60,		// temp_day_start
	20 * 60		// temp_night_start
};


inline status_t
accessibility_settings_path(BPath& path)
{
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return B_ERROR;
	return path.Append(accessibility_settings_file);
}


inline void
accessibility_load_settings(accessibility_settings& settings)
{
	settings = kDefaultAccessibilitySettings;

	BPath path;
	if (accessibility_settings_path(path) != B_OK)
		return;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage message;
	if (message.Unflatten(&file) != B_OK)
		return;

	bool flag;
	float number;
	int32 integer;

	if (message.FindBool("high_contrast", &flag) == B_OK)
		settings.high_contrast = flag;
	if (message.FindBool("cursor_override", &flag) == B_OK)
		settings.cursor_override = flag;
	if (message.FindFloat("cursor_scale", &number) == B_OK)
		settings.cursor_scale = number < 0.5f ? 0.5f
			: (number > 4.0f ? 4.0f : number);
	if (message.FindBool("reduce_motion", &flag) == B_OK)
		settings.reduce_motion = flag;

	if (message.FindBool("sticky_keys", &flag) == B_OK)
		settings.sticky_keys = flag;
	if (message.FindBool("slow_keys", &flag) == B_OK)
		settings.slow_keys = flag;
	if (message.FindFloat("slow_keys_delay", &number) == B_OK)
		settings.slow_keys_delay = number < 0.1f ? 0.1f
			: (number > 2.0f ? 2.0f : number);
	if (message.FindBool("mouse_keys", &flag) == B_OK)
		settings.mouse_keys = flag;
	if (message.FindInt32("mouse_keys_speed", &integer) == B_OK)
		settings.mouse_keys_speed = integer < 0 ? 0
			: (integer > 4 ? 4 : integer);

	if (message.FindBool("visual_alerts", &flag) == B_OK)
		settings.visual_alerts = flag;
	if (message.FindBool("mono_audio", &flag) == B_OK)
		settings.mono_audio = flag;

	if (message.FindBool("temp_schedule_enabled", &flag) == B_OK)
		settings.temp_schedule_enabled = flag;
	if (message.FindFloat("temp_day_kelvin", &number) == B_OK)
		settings.temp_day_kelvin = number;
	if (message.FindFloat("temp_night_kelvin", &number) == B_OK)
		settings.temp_night_kelvin = number;
	if (message.FindInt32("temp_day_start", &integer) == B_OK)
		settings.temp_day_start = integer;
	if (message.FindInt32("temp_night_start", &integer) == B_OK)
		settings.temp_night_start = integer;
}


// Save settings. highContrastColors NULL keeps any snapshot already in
// the file; non-NULL replaces it (empty message clears).
inline status_t
accessibility_save_settings(const accessibility_settings& settings,
	const BMessage* highContrastColors = NULL)
{
	BMessage message('a11y');

	message.AddBool("high_contrast", settings.high_contrast);
	message.AddBool("cursor_override", settings.cursor_override);
	message.AddFloat("cursor_scale", settings.cursor_scale);
	message.AddBool("reduce_motion", settings.reduce_motion);

	message.AddBool("sticky_keys", settings.sticky_keys);
	message.AddBool("slow_keys", settings.slow_keys);
	message.AddFloat("slow_keys_delay", settings.slow_keys_delay);
	message.AddBool("mouse_keys", settings.mouse_keys);
	message.AddInt32("mouse_keys_speed", settings.mouse_keys_speed);

	message.AddBool("visual_alerts", settings.visual_alerts);
	message.AddBool("mono_audio", settings.mono_audio);

	message.AddBool("temp_schedule_enabled", settings.temp_schedule_enabled);
	message.AddFloat("temp_day_kelvin", settings.temp_day_kelvin);
	message.AddFloat("temp_night_kelvin", settings.temp_night_kelvin);
	message.AddInt32("temp_day_start", settings.temp_day_start);
	message.AddInt32("temp_night_start", settings.temp_night_start);

	BPath path;
	if (accessibility_settings_path(path) != B_OK)
		return B_ERROR;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path, true) != B_OK)
		return B_ERROR;
	if (path.Append(accessibility_settings_file) != B_OK)
		return B_ERROR;

	if (highContrastColors != NULL) {
		message.AddMessage("high_contrast_colors", highContrastColors);
	} else {
		BFile oldFile(path.Path(), B_READ_ONLY);
		BMessage old;
		if (oldFile.InitCheck() == B_OK && old.Unflatten(&oldFile) == B_OK) {
			BMessage saved;
			if (old.FindMessage("high_contrast_colors", &saved) == B_OK)
				message.AddMessage("high_contrast_colors", &saved);
		}
	}

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();
	return message.Flatten(&file);
}


// User colour snapshot taken before high contrast is turned on.
inline status_t
accessibility_load_high_contrast_colors(BMessage& colors)
{
	colors.MakeEmpty();

	BPath path;
	if (accessibility_settings_path(path) != B_OK)
		return B_ERROR;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return B_ERROR;

	BMessage message;
	if (message.Unflatten(&file) != B_OK)
		return B_ERROR;

	BMessage saved;
	if (message.FindMessage("high_contrast_colors", &saved) != B_OK)
		return B_ENTRY_NOT_FOUND;
	if (saved.CountNames(B_RGB_32_BIT_TYPE) == 0)
		return B_ENTRY_NOT_FOUND;
	colors = saved;
	return B_OK;
}


// Shared apply path for the preflet and the Deskbar replicant.
inline void
accessibility_apply_mono_audio(bool enabled)
{
	BMediaGraph* graph = BMediaGraph::Instance();
	if (graph == NULL)
		return;

	media_client_id id = 0;
	if (graph->GetDefaultAudioOutput(&id) != B_OK || id == 0)
		return;

	graph->SetDeviceMono(id, enabled);
}


inline bool
accessibility_visual_alerts_enabled()
{
	accessibility_settings settings;
	accessibility_load_settings(settings);
	return settings.visual_alerts;
}


inline bool
accessibility_reduce_motion_enabled()
{
	accessibility_settings settings;
	accessibility_load_settings(settings);
	return settings.reduce_motion;
}


// Minutes since midnight; returns false when the schedule cannot apply.
inline bool
accessibility_temperature_for_now(const accessibility_settings& settings,
	time_t now, float& kelvin)
{
	if (!settings.temp_schedule_enabled)
		return false;

	struct tm local;
	if (localtime_r(&now, &local) == NULL)
		return false;

	int32 minutes = local.tm_hour * 60 + local.tm_min;
	int32 dayStart = settings.temp_day_start;
	int32 nightStart = settings.temp_night_start;

	if (dayStart == nightStart) {
		kelvin = settings.temp_day_kelvin;
		return true;
	}

	// Night wraps past midnight when night starts before day.
	if (nightStart < dayStart) {
		if (minutes >= dayStart || minutes < nightStart)
			kelvin = settings.temp_day_kelvin;
		else
			kelvin = settings.temp_night_kelvin;
	} else {
		if (minutes >= nightStart)
			kelvin = settings.temp_night_kelvin;
		else
			kelvin = settings.temp_day_kelvin;
	}
	return true;
}


#endif	// ACCESSIBILITY_SETTINGS_H
