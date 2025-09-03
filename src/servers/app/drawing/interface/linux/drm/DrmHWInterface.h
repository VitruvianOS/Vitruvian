/*
 * Copyright 2021, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */
#ifndef DRM_INTERFACE_H
#define DRM_INTERFACE_H


#include <xf86drm.h>
#include <xf86drmMode.h>

#include "DrmBuffer.h"
#include "HWInterface.h"
#include "LibInputEventStream.h"
#include "TTy.h"


#if DEBUG
	#define CALLED() 			printf("CALLED %s\n",__PRETTY_FUNCTION__)
#else
  	#define CALLED() 			((void)0)
#endif


class DrmBuffer;

class DrmHWInterface : public HWInterface {
public:
								DrmHWInterface();
	virtual						~DrmHWInterface();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();

	virtual	EventStream*		CreateEventStream();

	virtual	status_t			SetMode(const display_mode& mode);
	virtual	void				GetMode(display_mode* mode);

	virtual status_t			GetDeviceInfo(accelerant_device_info* info);
	virtual status_t			GetFrameBufferConfig(
									frame_buffer_config& config);

	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	virtual status_t			GetTimingConstraints(display_timing_constraints*
									constraints);
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);

	virtual sem_id				RetraceSemaphore();
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	virtual status_t			SetDPMSMode(uint32 state);
	virtual uint32				DPMSMode();
	virtual uint32				DPMSCapabilities();

	virtual status_t			SetBrightness(float brightness);
	virtual status_t			GetBrightness(float* brightness);

	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			CopyBackToFront(const BRect& frame);

private:
			static void		 	SwitchVt(int sig);

			static int			fFd;

			DrmBuffer*			fFrontBuffer;
			DrmBuffer*			fBackBuffer;

			display_mode		fDisplayMode;

			LibInputEventStream* fEventStream;
};

#endif
