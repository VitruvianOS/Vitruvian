/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */
#ifndef FBDEV_GRAPHICS_CARD_H
#define FBDEV_GRAPHICS_CARD_H


#include "HWInterface.h"

#include <linux/fb.h>


class FBDevBuffer;

class FBDevHWInterface : public HWInterface {
public:
								FBDevHWInterface();
	virtual						~FBDevHWInterface();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();

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

	virtual status_t			SetBrightness(float);
	virtual status_t			GetBrightness(float*);

	// frame buffer access
	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			CopyBackToFront(const BRect& frame);

private:
			FBDevBuffer*		fBackBuffer;
			FBDevBuffer*		fFrontBuffer;

			display_mode		fDisplayMode;

			int					fFrameBuffer;

			struct fb_fix_screeninfo	fInfo;
			struct fb_var_screeninfo	fVInfo;

};

#endif // VIEW_GRAPHICS_CARD_H
