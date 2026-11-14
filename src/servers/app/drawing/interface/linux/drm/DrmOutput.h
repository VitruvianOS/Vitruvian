/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */
#ifndef DRM_OUTPUT_H
#define DRM_OUTPUT_H


#include "HWInterface.h"


class DrmHWInterface;


// The screen of an output beside the first one. The owner draws and
// presents; this answers for the connector's mode and place.
class DrmOutput : public HWInterface {
public:
								DrmOutput(DrmHWInterface* owner,
									uint32 connector);
	virtual						~DrmOutput();

			uint32				Connector() const { return fConnector; }

	virtual	status_t			Shutdown();

	virtual	status_t			SetMode(const display_mode& mode);
	virtual	void				GetMode(display_mode* mode);
	virtual	status_t			GetPreferredMode(display_mode* mode);

	virtual status_t			GetDeviceInfo(accelerant_device_info* info);
	virtual status_t			GetMonitorInfo(monitor_info* info);
	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	virtual status_t			GetTimingConstraints(
									display_timing_constraints* constraints);
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);

	virtual	int32				PanelOrientation() const;
	virtual	status_t			SetPanelOrientation(int32 orientation);
	virtual	int32				PanelReflection() const;
	virtual	status_t			SetPanelReflection(int32 reflection);

	virtual sem_id				RetraceSemaphore();
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	virtual status_t			SetDPMSMode(uint32 state);
	virtual uint32				DPMSMode();
	virtual uint32				DPMSCapabilities();

	virtual status_t			SetBrightness(float brightness);
	virtual status_t			GetBrightness(float* brightness);

	virtual status_t			GetConnectorName(BString& name);

	virtual	bool				IsInternalPanel() const;
	virtual	BPoint				Origin() const;
	virtual	status_t			SetOrigin(BPoint origin);

	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			CopyBackToFront(const BRect& frame);
	virtual	status_t			CopyBackToFront(const BRegion& region);

private:
			DrmHWInterface*		fOwner;
			uint32				fConnector;
};


#endif	// DRM_OUTPUT_H
