/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _MEDIA2_CAMERA_SOURCE_H
#define _MEDIA2_CAMERA_SOURCE_H


#include <Locker.h>
#include <Messenger.h>
#include <String.h>

#include <vector>

#include <media2/MediaClientDefs.h>
#include <media2/MediaDefs.h>
#include <media2/MediaNode.h>


enum camera_location {
	B_CAMERA_LOCATION_UNKNOWN  = 0,
	B_CAMERA_LOCATION_FRONT    = 1,
	B_CAMERA_LOCATION_BACK     = 2,
	B_CAMERA_LOCATION_EXTERNAL = 3
};


enum {
	// Posted to BCameraSource::SetFrameTarget() recipients.
	B_CAMERA_FRAME = 'Cfrm'
};


struct camera_device_info {
	media_client_id	id;
	BString			name;
	BString			stableId;
	BString			nodeName;
	camera_location	location;
	bool			isDefault;
};


// Parameter ids owned by BCameraSource's control web.
enum {
	B_CAMERA_PARAM_BRIGHTNESS         = 1,
	B_CAMERA_PARAM_CONTRAST           = 2,
	B_CAMERA_PARAM_EXPOSURE_AUTO      = 3,
	B_CAMERA_PARAM_EXPOSURE           = 4,
	B_CAMERA_PARAM_WHITE_BALANCE_AUTO = 5,
	B_CAMERA_PARAM_WHITE_BALANCE      = 6,
	B_CAMERA_PARAM_FOCUS_AUTO         = 7,
	B_CAMERA_PARAM_FOCUS              = 8,
	B_CAMERA_PARAM_POWER_LINE_FREQ    = 9,
	B_CAMERA_PARAM_MIRROR             = 10
};


class BCameraSource : public BMediaNode {
public:
								BCameraSource(const char* name = NULL);
	virtual						~BCameraSource();

			status_t			SetToDefault();
			status_t			SetToDevice(media_client_id id);
			status_t			SetToDevice(const camera_device_info& info);

			const char*			StableId() const { return fStableId.String(); }
			camera_location		Location() const { return fLocation; }
			media_client_id		DeviceId() const { return fDeviceId; }

			status_t			SetFrameTarget(const BMessenger& target);
			media_raw_video_format VideoFormat() const;

			// CPU-mapped capture buffer; size is one frame.
			status_t			CopyLastFrame(void* buffer, size_t size,
									media_raw_video_format* outFormat = NULL) const;

			status_t			GetControl(int32 id, float* outValue) const;
			status_t			SetControl(int32 id, float value);
			status_t			ResetControls();

			// Built with MakeContinuousParameter; preflet reads values via
			// GetControl and writes via SetControl.
			void*				ParameterWebHandle() const { return fParameterWeb; }

private:
	virtual	status_t			_StartConnections(void* backend) override;
	virtual	void				_StopConnections() override;

			void				_BuildParameterWeb();
			void				_SaveControls();
			void				_OnCaptureFrame(void* data, size_t size,
									const media_raw_video_format& format);

	static	void				_OnProcess(void* userdata);
	static	void				_OnParamChanged(void* data, uint32 id,
									const struct spa_pod* param);

			BLocker				fFrameLock;
			std::vector<uint8>	fLastFrame;
			media_raw_video_format	fLastFormat;
			bool				fHaveFrame;
			BMessenger			fFrameTarget;

			BLocker				fControlLock;
			float				fBrightness;
			float				fContrast;
			bool				fExposureAuto;
			float				fExposure;
			bool				fWhiteBalanceAuto;
			float				fWhiteBalance;
			bool				fFocusAuto;
			float				fFocus;
			float				fPowerLineFreq;
			bool				fMirror;

			media_client_id		fDeviceId;
			BString				fStableId;
			BString				fNodeName;
			camera_location		fLocation;

			void*				fCaptureStream;
			void*				fParameterWeb;
};


#endif
