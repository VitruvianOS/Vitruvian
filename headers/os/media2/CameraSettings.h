/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_CAMERA_SETTINGS_H
#define _MEDIA2_CAMERA_SETTINGS_H


#include <Message.h>
#include <String.h>

#include <media2/CameraSource.h>


// Persists the default camera and per-device preferences in Camera_settings.
// The settings file wins over PipeWire metadata; metadata is a mirror.
class BCameraSettings {
public:
								BCameraSettings();

			status_t			Load();
			status_t			Save() const;

			BString				DefaultCameraId() const { return fDefaultCameraId; }
			void				SetDefaultCameraId(const BString& id);

			bool				HasDevice(const BString& stableId) const;
			status_t			GetDevice(const BString& stableId,
									camera_device_info* outInfo,
									BMessage* outControls = NULL) const;
			status_t			SetDevice(const camera_device_info& info,
									const BMessage* controls = NULL);

private:
			BString				fDefaultCameraId;
			BMessage			fDevices;
			void*				fSettingsMessage;
};


#endif
