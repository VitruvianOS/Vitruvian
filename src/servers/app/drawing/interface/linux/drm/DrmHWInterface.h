/*
 * Copyright 2021-2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */
#ifndef DRM_INTERFACE_H
#define DRM_INTERFACE_H

#include <xf86drm.h>
#include <xf86drmMode.h>
extern "C" {
#include <libseat.h>
}
#include <libudev.h>

#include <OS.h>
#include <Region.h>
#include <atomic>

#include "DrmBuffer.h"
#include "HWInterface.h"
#include "MallocBuffer.h"
#include "TTy.h"

#ifdef HAVE_GBM
extern "C" {
#include <gbm.h>
}
#endif

#include <libbacklight.h>


#if DEBUG
	#define CALLED() 			printf("CALLED %s\n",__PRETTY_FUNCTION__)
#else
  	#define CALLED() 			((void)0)
#endif


struct PlaneProps {
	uint32_t fb_id;
	uint32_t crtc_id;
	uint32_t src_x, src_y, src_w, src_h;
	uint32_t crtc_x, crtc_y, crtc_w, crtc_h;
	uint32_t fb_damage_clips;
	uint32_t rotation;
};

struct CrtcProps {
	uint32_t active;
	uint32_t mode_id;
	uint32_t vrr_enabled;
};

struct ConnProps {
	uint32_t crtc_id;
	uint32_t dpms;
};

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
	virtual	status_t			GetPreferredMode(display_mode* mode);

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

	virtual	void				SetCursor(ServerCursor* cursor);
	virtual	void				SetCursorVisible(bool visible);
	virtual	void				MoveCursorTo(float x, float y);
	virtual	void				_DrawCursor(IntRect area) const;

			void				_OnSessionEnable();
			void				_OnSessionDisable();

			status_t			CreateLease(uint32_t* connectors, int connCount,
									uint32_t* crtcs, int crtcCount,
									int* leaseFd);
			void				RevokeLease(int leaseFd);

			status_t			InitCheck() const;

private:
	static	int32				_EventThreadEntry(void* data);
			void				_EventThreadMain();
			void				_RestoreDisplay();
			void				_HandleHotplug();

	static	void				_PageFlipHandler(int fd, unsigned int frame,
									unsigned int sec, unsigned int usec,
									void* data);

			void				_BlitRect(RenderingBuffer* src,
									RenderingBuffer* dst,
									const BRect& frame);

			int					_CrtcIndex(uint32_t crtc_id);
			void				_ProbeAtomic();
			void				_DiscoverProperties();
			void				_DiscoverPlaneProps(uint32_t plane_id,
									PlaneProps& props);
			void				_DiscoverCrtcProps(uint32_t crtc_id);
			void				_DiscoverConnProps(uint32_t conn_id);

			status_t			_AtomicModeset(uint32_t fb_id,
									drmModeModeInfo* mode);
			status_t			_AtomicFlip(uint32_t fb_id,
									const BRect* dirty_rects,
									uint32_t nrects);
			status_t			_AtomicSetCursor(uint32_t fb_id,
									uint32_t crtc_id,
									int32 x, int32 y,
									uint32_t w, uint32_t h);

			static int			fFd;

			DrmBuffer*			fFrontBuffer;
			DrmBuffer*			fBackBuffer;
			DrmBuffer*			fWriteTarget;

			display_mode		fDisplayMode;

			struct libseat*		fSeat;
			int					fDeviceId;
			std::atomic<bool>	fSessionActive;
			bool				fInitialized;
			std::atomic<bool>	fRunning;

			thread_id			fEventThread;
			sem_id				fSessionSem;

			struct udev*		fUdev;
			struct udev_monitor* fUdevMonitor;
			int					fUdevFd;

#ifdef HAVE_GBM
			struct gbm_device*  fGbmDevice;
			bool				fUseGbm;
#endif

			MallocBuffer*		fRenderBuffer;

			bool				fPageFlipEnabled;
			bool				fPageFlipPending;
			static const int	kMaxDirtyRects = 32;
			BRect				fFlipDirtyRects[kMaxDirtyRects];
			int32				fFlipDirtyCount;

			uint32				fDpmsState;

			struct backlight*	fBacklight;

			bool				fAtomicSupported;
			uint32_t			fPrimaryPlaneId;
			uint32_t			fCursorPlaneId;
			uint32_t			fModeBlobId;
			struct PlaneProps	fPlaneProps;
			struct PlaneProps	fCursorPlaneProps;
			struct CrtcProps	fCrtcProps;
			struct ConnProps	fConnProps;

			bool				fVRRSupported;
			bool				fVRREnabled;
};

#endif
