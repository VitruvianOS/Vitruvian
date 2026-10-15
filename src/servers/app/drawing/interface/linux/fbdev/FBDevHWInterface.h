/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */
#ifndef FBDEV_GRAPHICS_CARD_H
#define FBDEV_GRAPHICS_CARD_H


#include "HWInterface.h"

#include "FBDevBuffer.h"
#include "TTy.h"

#include <linux/fb.h>


class FBDevHWInterface : public HWInterface {
public:
								FBDevHWInterface();
	virtual						~FBDevHWInterface();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();
	virtual status_t			InitCheck() const;

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

	virtual status_t			GetPreferredMode(display_mode* mode);

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

	virtual	void				OnSeatEnabled();
	virtual	void				OnSeatDisabled();

	virtual	status_t			CopyBackToFront(const BRect& frame);
	virtual	status_t			CopyBackToFront(const BRegion& region);

private:
	enum { kMaxPanningPages = 3 };

			void				_EnterGraphicsMode();
			void				_LeaveGraphicsMode();
			void				_AdoptVar(const struct fb_var_screeninfo& var);

			void				_ProbePanning();
			void				_DropPanning(const char* reason);
			void				_RestoreScanoutOrigin();
			bool				_PanTo(uint32 page);
			void				_PresentRegion(BRegion& region);
			void				_BlitToPage(uint32 page,
									const BRegion& region);

	static	int					_GetVarOp(void* context,
									struct fb_var_screeninfo* var);
	static	int					_GetFixOp(void* context,
									struct fb_fix_screeninfo* fix);
	static	int					_SetVarOp(void* context,
									struct fb_var_screeninfo* var);
	static	int					_PanOp(void* context,
									const struct fb_var_screeninfo* var);
	static	int					_WaitVsyncOp(void* context);

			FBDevBuffer*		fFrontBuffer;
			FBDevPage			fFrontPageView;
			RenderingBuffer*	fMemBackBuffer;

			display_mode		fDisplayMode;

			int					fFrameBuffer;
			int					fTTY;

			struct fb_fix_screeninfo	fInfo;
			struct fb_var_screeninfo	fVInfo;

			bool				fModeSetSupported;
			bool				fBlankSupported;
			bool				fPanningSupported;
			bool				fVsyncSupported;
			uint32				fPageCount;
			uint32				fFrontPage;
			BRegion				fPageDirty[kMaxPanningPages];
			uint32				fDPMSState;

			EventStream*		fEventStream;
};

#endif // VIEW_GRAPHICS_CARD_H
