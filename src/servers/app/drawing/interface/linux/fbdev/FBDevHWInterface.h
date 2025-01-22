/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */
#ifndef FBDEV_GRAPHICS_CARD_H
#define FBDEV_GRAPHICS_CARD_H


#include "HWInterface.h"

#include "LibInputEventStream.h"

#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <termios.h>


class FBDevBuffer;

class FBDevHWInterface : public HWInterface {
public:
								FBDevHWInterface();
	virtual						~FBDevHWInterface();

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

	virtual status_t			SetBrightness(float);
	virtual status_t			GetBrightness(float*);

	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			CopyBackToFront(const BRect& frame);

private:
			status_t			_InitTTy(int ttyNumber);
			int					_OpenTTy(int ttyNumber);
			void				_DeinitTTy();
			static void			_SwitchVt(int sig);

			LibInputEventStream* fEventStream;

			FBDevBuffer*		fBackBuffer;
			FBDevBuffer*		fFrontBuffer;

			display_mode		fDisplayMode;

			int					fFrameBuffer;

			struct fb_fix_screeninfo	fInfo;
			struct fb_var_screeninfo	fVInfo;

			struct sigaction	fSigUsr1;
			struct sigaction	fSigUsr2;

			struct vt_mode		fVtMode;
			struct termios		fTermios;              
};

#endif // VIEW_GRAPHICS_CARD_H
