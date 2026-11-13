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
#include <BFutexLocker.h>
#include <pthread.h>
#include <atomic>
#include <vector>

#include "BufferAgeTracker.h"
#include "DrmBuffer.h"
#include "HWInterface.h"
#include "MallocBuffer.h"
#include "OverlayPlane.h"
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
	// Captured but never committed: the reference cursor plane (amdgpu)
	// does not advertise this property. Kept for a plane class that does.
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

// A cursor-plane change held back until the next frame commit, so it
// rides that commit instead of colliding with an in-flight flip (EBUSY).
struct CursorUpdate {
	bool		valid;
	uint32_t	fb_id;
	int32_t		x, y;
	uint32_t	w, h;
};

// An overlay plane change held back until the next frame commit.
struct PlaneUpdate {
	uint32_t	planeId;
	PlaneProps	props;
	bool		enable;
	uint32_t	fb_id;
	int32_t		crtcX, crtcY;
	uint32_t	crtcW, crtcH;
	uint32_t	srcX, srcY, srcW, srcH;	// 16.16
};

class DrmHWInterface;

// DRM implementation of a claimed overlay plane; owns a dumb buffer
// bound to the plane until released.
class DrmOverlayPlane : public OverlayPlane {
public:
							DrmOverlayPlane(DrmHWInterface* interface,
								uint32_t planeId, const PlaneProps& props);
	virtual					~DrmOverlayPlane();

			status_t		Allocate(uint32 width, uint32 height,
								uint32 fourcc, uint32 bpp,
								color_space space);
			// Scans out an imported dma-buf; the fd stays with the caller.
			// sharedHandle is a GEM handle the exporter already holds on
			// this device fd and must not be closed here.
			status_t		Adopt(int dmabufFd, uint32 width, uint32 height,
								uint32 fourcc, uint32 stride,
								color_space space, uint32 sharedHandle = 0);
			void			Free();
			status_t		Reapply();

	virtual	void*			Bits() const;
	virtual	uint32			BytesPerRow() const;
	virtual	uint32			Width() const;
	virtual	uint32			Height() const;
	virtual	color_space		ColorSpace() const;
	virtual	status_t		Configure(const BRect& source,
								const BRect& destination);
	virtual	void			Disable();

			uint32_t		PlaneId() const { return fPlaneId; }

private:
			void			_UnbindNow();

			DrmHWInterface*	fInterface;
			int				fFd;
			uint32_t		fPlaneId;
			PlaneProps		fProps;
			uint32_t		fHandle;
			uint32_t		fFbId;
			bool			fAdopted;
			bool			fCloseHandle;
			uint8_t*		fBits;
			uint32			fSize;
			uint32			fStride;
			uint32			fWidth;
			uint32			fHeight;
			color_space		fColorSpace;
			bool			fArmed;
			bool			fHasGeometry;
			BRect			fSource;
			BRect			fDestination;
};

// One discovered DRM_PLANE_TYPE_OVERLAY plane plus its claim state.
struct OverlayPlaneInfo {
	uint32_t				planeId;
	PlaneProps				props;
	uint32					possibleCrtcs;
	uint32					minWidth;
	uint32					maxWidth;
	uint32					minHeight;
	uint32					maxHeight;
	std::vector<uint32_t>	formats;
	DrmOverlayPlane*		claimed;
};

// Values match the kernel's "panel orientation" connector property
// (drm_connector.h, DRM_MODE_PANEL_ORIENTATION_*) so a property read casts
// straight into this enum.
enum PanelOrientation {
	PANEL_ORIENTATION_NORMAL      = 0,
	PANEL_ORIENTATION_UPSIDE_DOWN = 1,
	PANEL_ORIENTATION_LEFT_UP     = 2,
	PANEL_ORIENTATION_RIGHT_UP    = 3
};

class DrmBuffer;


// A rectangle of another buffer: the part of the desktop one output shows.
class DrmBufferView : public RenderingBuffer {
public:
								DrmBufferView();

			void				SetTo(RenderingBuffer* buffer, int32 x,
									int32 y, uint32 width, uint32 height);

	virtual	status_t			InitCheck() const;
	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

private:
			uint8*				fBits;
			uint32				fBytesPerRow;
			uint32				fWidth;
			uint32				fHeight;
			color_space			fColorSpace;
};


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
			// Same mode policy, no connector probe; for the poll.
			status_t			GetPreferredModeCurrent(display_mode* mode);
	virtual	int32				PanelOrientation() const;
	virtual	status_t			SetPanelOrientation(int32 orientation);
	virtual	int32				PanelScanoutOrientation() const;
	virtual	int32				PanelReflection() const;
	virtual	status_t			SetPanelReflection(int32 reflection);

#ifdef HAVE_GBM
	virtual	struct gbm_device*	GbmDevice();
#endif

	virtual status_t			GetDeviceInfo(accelerant_device_info* info);
	virtual status_t			GetMonitorInfo(monitor_info* info);
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

	virtual status_t			SetTemperature(float kelvin);
	virtual status_t			GetTemperature(float* kelvin);
	virtual status_t			GetConnectorName(BString& name);

	virtual	BPoint				Origin() const;
	virtual	status_t			SetOrigin(BPoint origin);

	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			CopyBackToFront(const BRect& frame);
	virtual	status_t			CopyBackToFront(const BRegion& region);

	virtual	void				SetCursor(ServerCursor* cursor);
	virtual	void				SetCursorVisible(bool visible);
	virtual	void				MoveCursorTo(float x, float y);
	virtual	void				_DrawCursor(IntRect area) const;

	virtual	void				SetDragBitmap(const ServerBitmap* bitmap,
									const BPoint& offsetFromCursor);

			void				_OnSessionEnable();
			void				_OnSessionDisable();

	virtual	void				OnSeatEnabled();
	virtual	void				OnSeatDisabled();

			status_t			CreateLease(uint32_t* connectors, int connCount,
									uint32_t* crtcs, int crtcCount,
									int* leaseFd);
			void				RevokeLease(int leaseFd);

	virtual	overlay_token		AcquireOverlayChannel();
	virtual	void				ReleaseOverlayChannel(overlay_token token);
	virtual	status_t			AcquireOverlayPlane(color_space space,
									uint32 width, uint32 height,
									OverlayPlane** _plane);
	virtual	void				ReleaseOverlayPlane(OverlayPlane* plane);
	virtual	status_t			AcquireOverlayPlaneForBitmap(
									ServerBitmap* bitmap,
									OverlayPlane** _plane);
	virtual	status_t			GetOverlayRestrictions(const Overlay* overlay,
									overlay_restrictions* restrictions);
	virtual	bool				CheckOverlayRestrictions(int32 width,
									int32 height, color_space colorSpace);
	virtual	const overlay_buffer* AllocateOverlayBuffer(int32 width,
									int32 height, color_space space);
	virtual	void				FreeOverlayBuffer(const overlay_buffer* buffer);
	virtual	void				ConfigureOverlay(Overlay* overlay);
	virtual	void				HideOverlay(Overlay* overlay);

			status_t			InitCheck() const;

			// Outputs beside the first one. They show their part of the
			// same render buffer; ScreenManager owns a screen for each.
			void				GetConnectedOutputs(
									std::vector<uint32>& connectors);
			HWInterface*		AddOutput(uint32 connector);
			void				RemoveOutput(uint32 connector);
			// True once after the connector's preferred mode changed.
			bool				TakeOutputModeChange(uint32 connector);

			status_t			SetOutputMode(uint32 connector,
									const display_mode& mode);
			status_t			GetOutputMode(uint32 connector,
									display_mode* mode);
			status_t			GetOutputPreferredMode(uint32 connector,
									display_mode* mode);
			status_t			GetOutputModeList(uint32 connector,
									display_mode** _modeList, uint32* _count);
			status_t			GetOutputMonitorInfo(uint32 connector,
									monitor_info* info);
			status_t			GetOutputConnectorName(uint32 connector,
									BString& name);
			BPoint				OutputOrigin(uint32 connector);
			status_t			SetOutputOrigin(uint32 connector,
									BPoint origin);
			int32				OutputOrientation(uint32 connector);
			status_t			SetOutputOrientation(uint32 connector,
									int32 orientation);
			int32				OutputReflection(uint32 connector);
			status_t			SetOutputReflection(uint32 connector,
									int32 reflection);
			uint32				OutputDPMSMode(uint32 connector);
			status_t			SetOutputDPMSMode(uint32 connector,
									uint32 state);

	static	status_t			ProposeModeFromList(display_mode* modeList,
									uint32 count, display_mode* candidate,
									const display_mode* low,
									const display_mode* high);

private:
	struct FlipToken;
	struct Output;
	friend class				DrmOverlayPlane;

	static	int32				_EventThreadEntry(void* data);
			void				_EventThreadMain();
			void				_RestoreDisplay();
			void				_RepaintAfterDPMS();
			void				_HandleHotplug();
			bool				_RescanConnectors();
			bool				_CheckResume();
			void				_OnResume();
			void				_DrainPendingFlip();
			void				_ScheduleResize();
	static	int32				_ResizeThreadEntry(void* data);
			void				_ApplyResize();
			status_t			_GetPreferredMode(display_mode* mode,
									bool probe);
	static	void				_FillModeInfo(display_mode& mode,
									const drmModeModeInfo& m);

	static	void				_PageFlipHandler(int fd, unsigned int frame,
									unsigned int sec, unsigned int usec,
									void* data);
			// Front/back swap, flip event or paced flip.
			// Entered with fFlipMutex held; releases it.
			void				_FinishFrameSwap();
			// Fallback for drivers whose page flip never works: wait a
			// vblank, drmModeSetCrtc the back fb, then swap as a flip
			// completion would. Tearing possible, display usable.
			bool				_VblankPacedFlip();

			void				_SelectPresentMode();
			void				_DamageAll();
			void				_Wake();
			// Event thread, exclusive lock held: brings fWriteTarget up
			// to the render buffer over region.
			void				_ComposeFrame(const BRegion& region);
			status_t			_DirtyFlush(uint32_t fb_id,
									const BRegion& damage);
			// Logical damage as physical clips inside the framebuffer.
			uint32				_DamageClips(const BRegion& damage,
										drm_mode_rect* clips, uint32 max) const;
	static	uint32				_DamageClips(const BRegion& damage,
										drm_mode_rect* clips, uint32 max,
										int32 logicalW, int32 logicalH,
										int32 orientation, int32 reflection);

			// Direct render: the painter draws into the scanout buffers
			// and the flip completion copies only the region the recycled
			// target is missing. Opt-in with VOS_DRM_DIRECT_RENDER=1.
			void				_SetupDirectRender();
			void				_LeaveDirectRender();
			int32				_AgeSlotForFb(uint32_t fbId) const;
			void				_DirectFlipComplete();
			bigtime_t			_FramePeriod() const;
	static	bigtime_t			_FramePeriodFor(const display_mode& mode);

			void				_BlitRect(RenderingBuffer* src,
									RenderingBuffer* dst,
									const BRect& frame);
			void				_BlitRect(RenderingBuffer* src,
									RenderingBuffer* dst,
									const BRect& frame, int32 orientation,
									int32 reflection);

			// srcBg and area are the output's own; origin is where the
			// output sits on the desktop.
			void				_BlendCursor(RenderingBuffer* srcBg,
									RenderingBuffer* dst, IntRect area,
									const BPoint& origin, int32 orientation,
									int32 reflection) const;
			// The cursor's desktop frame even when a sprite shows it.
			IntRect				_SoftCursorFrame() const;

			// The first output and the others.
			struct modeset_dev*	_PickDev() const;
			BRect				_Frame() const;
			BRect				_DesktopBounds() const;
			bool				_SingleOutput() const;
			void				_RebuildRenderBuffer(uint32 width,
									uint32 height);
			void				_AddDamage(const BRect& frame);
			void				_AddDamage(const BRegion& region);
			void				_AddOutputDamage(const BRect& frame);
			// fDirtyMutex held.
			void				_SplitDamage(const BRect& frame,
									bool outputsOnly);
			void				_DamageOutputs();
			Output*				_FindOutput(uint32 connector) const;
			status_t			_SetupOutput(Output* output);
			void				_TeardownOutput(Output* output);
			bool				_OutputsWaiting();
			void				_PresentOutputs();
			void				_OutputFlipComplete(uint32 connector,
									uint64_t generation);
			void				_RestoreOutputs(bool onlyLost = false);
			void				_SetOutputDPMS(Output* output, uint32 state);

			status_t			_ModeListFor(uint32 connector,
									int32 orientation,
									display_mode** _modeList, uint32* _count);
			status_t			_PreferredModeFor(uint32 connector,
									int32 orientation, bool probe,
									display_mode* mode);
			status_t			_MonitorInfoFor(uint32 connector,
									monitor_info* info);
			status_t			_ConnectorNameFor(uint32 connector,
									BString& name);

		virtual	void				_CopyToFront(uint8* src, uint32 srcBPR,
									int32 x, int32 y,
									int32 right, int32 bottom) const;

		void				_PushCursorTrackDirty(int32 oldX, int32 oldY,
								int32 newX, int32 newY);

#if defined(__x86_64__) || defined(__i386__)
	static	void				_BlitRect_AVX2(const uint8* s, uint8* d,
									uint32 srcBpr, uint32 dstBpr,
									int32 bytes, int32 rows);
#endif

			int					_CrtcIndex(uint32_t crtc_id);
			void				_ProbeAtomic();
			void				_ProbeCursor();
			void				_DisableHardwareCursor();
			void				_DiscoverProperties();
			void				_DiscoverPlaneProps(uint32_t plane_id,
									PlaneProps& props);
			void				_DiscoverCrtcProps(uint32_t crtc_id);
			void				_DiscoverConnProps(uint32_t conn_id);
			void				_DiscoverPanelOrientation();
			void				_DiscoverPanelReflection();
			bool				_ApplyDmiOrientationQuirk(uint32_t width,
									uint32_t height);
			void				_ApplyOrientationSwap(uint32_t w, uint32_t h,
									uint32_t& outW, uint32_t& outH) const;
			// False when rotated or when we own no cursor plane.
			// VOS_HW_CURSOR forces the legacy sprite on for testing.
			bool				_HardwareCursorUsable() const;

			status_t			_AtomicModeset(uint32_t fb_id,
									drmModeModeInfo* mode);
			// One request: primary fb (+damage clips) and cursor plane.
			status_t			_AtomicCommitFrame(uint32_t fb_id,
								const BRegion* damage,
								const CursorUpdate* cursor,
								const std::vector<PlaneUpdate>* planes,
								FlipToken* token);
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

			std::atomic<bool>	fInitialized;
			std::atomic<bool>	fRunning;

			thread_id			fEventThread;
			thread_id			fResizeThread;
			std::atomic<bool>	fResizeBusy;
			std::atomic<bool>	fResizePending;
			bigtime_t			fLastModeCheck;
			// Written from SetMode() (app thread) and the event/resize
			// threads; the poll reads them without the mode lock.
			std::atomic<uint32_t>	fLastPreferredWidth;
			std::atomic<uint32_t>	fLastPreferredHeight;
			std::atomic<bool>	fUserSetMode;
			sem_id				fSessionSem;

			struct udev*		fUdev;
			struct udev_monitor* fUdevMonitor;
			int					fUdevFd;
			bigtime_t			fLastSuspendCheck;

#ifdef HAVE_GBM
			// Created on the first GBM buffer request, never at init.
			struct gbm_device*	fGbmDevice;
#endif

			// Spans every output; each one presents its own rectangle.
			MallocBuffer*		fRenderBuffer;
			// The first output: connector, place, part of the buffer.
			struct modeset_dev*	fDev;
			BPoint				fOrigin;
			DrmBufferView		fSource;
			// Changed under the exclusive lock and fDirtyMutex; read
			// under either.
			std::vector<Output*>	fOutputs;

			// Written only under fFlipMutex (SetMode, _OnSessionEnable),
			// read lock-free by the event thread's flip gate; atomic so
			// the unlocked read is well-defined. The buffer pointers it
			// gates are validated under fFlipMutex before use.
			std::atomic<bool>	fPageFlipEnabled;
			std::atomic<bool>	fPageFlipPending;

			// Serializes buffer pointer swaps (page-flip completion) against
			// the SetMode() teardown that deletes those same buffers, and
			// guards fDisplayMode's virtual size while a flip blits. Never
			// held across a LockExclusiveAccess: the flip handler runs on
			// the event thread and must not block on the mode lock, which
			// SetMode() holds while it waits for that very handler.
			pthread_mutex_t		fFlipMutex;
			// Bumped whenever buffers are torn down or a drain gives up on
			// an in-flight flip; a completion event whose token generation
			// no longer matches is stale and must not swap anything.
			uint64_t			fFlipGeneration;
			// One heap token per issued flip, freed by the completion
			// handler. A token for an abandoned flip stays listed so the
			// eventual late event can be recognised and dropped, and the
			// destructor reaps any the kernel never delivered.
			struct FlipToken {
				DrmHWInterface*	hw;
				uint64_t		generation;
				// Connector of the output flipped; 0 is the first one.
				uint32_t		output;
			};
			std::vector<FlipToken*> fFlipTokens;
			pthread_cond_t		fFlipCond;
			// Guards the modeset_dev list against udev hotplug/rescan while
			// the event thread's flip path and the cursor ioctls hold a
			// pointer into it. Copy scalars under this lock; the long users
			// (SetMode, resize) are already serialized by the mode lock.
			pthread_mutex_t		fModesetMutex;

			// fAccumulatedDirty collects damage until the event thread
			// composes a frame. fBackStale is what fWriteTarget still
			// misses: the damage of the frame composed while it was on
			// screen. Each buffer is brought up to date only when it is
			// about to be shown.
			BRegion				fAccumulatedDirty;
			BRegion				fBackStale;
			bool				fNeedsFlip;
			BFutexLocker		fDirtyMutex;

			// One scanout buffer flushed by damage, for drivers that
			// upload on commit: they ignore damage clips across a
			// framebuffer change, so every flip would upload the screen.
			bool				fDamageFlush;
			// A wake is already in the eventfd; saves a write per draw.
			std::atomic<bool>	fWakeQueued;

			// Serialises _BlendCursor's writes to the scanout buffer when
			// several threads present under the parallel lock (no flip).
			mutable pthread_mutex_t	fCursorBlendMutex;

			// Direct render state. fAgeTracker and the slot map are only
			// touched under the exclusive lock. The pacing lock is the
			// event thread's exclusive lock, held from flip issue to
			// completion so no painter writes the scanned buffer.
			BufferAgeTracker	fAgeTracker;
			std::atomic<bool>	fDirectRender;
			int64				fFrameSerial;
			uint32_t			fAgeFbIds[2];
			int32				fAgeSlotCount;
			bool				fPacingLockHeld;
			bigtime_t			fFlipIssueTime;
			bigtime_t			fLastFlipIssue;

			int					fWakeFd;

			// Written by SetDPMSMode() on an app thread, read by the event
			// thread's flip gate; atomic so the flip path can check it
			// under fFlipMutex without the connector lock.
			std::atomic<uint32>	fDpmsState;

			struct backlight*	fBacklight;

			float				fTemperature;
			// Cleared when gamma_size is 0 or drmModeCrtcSetGamma fails.
			bool				fTemperatureSupported;

			bool				fAtomicSupported;
			uint32_t			fPrimaryPlaneId;
			uint32_t			fCursorPlaneId;
			// Which API armed the cursor plane; set once by _ProbeCursor().
			bool				fCursorUsesAtomic;
			// VOS_ATOMIC_BATCH: flip, cursor and damage ride one atomic
			// request. Off keeps the legacy page-flip and cursor ioctls.
			bool				fAtomicBatch;
			CursorUpdate		fStagedCursor;
			// Overlay plane changes for the next frame commit (fDirtyMutex).
			std::vector<PlaneUpdate>	fStagedPlanes;
			// False when the caller must apply the update itself.
			bool				_StagePlane(const PlaneUpdate& update);
			void				_DropStagedPlane(uint32_t planeId);
			void				_SetPlaneNow(const PlaneUpdate& update);
		// Armed once unsupported flip errors reach the limit: the
		// driver has dumb buffers but no working page flip. Cleared
		// on session enable; only flip-unsupported errnos count.
		bool				fVblankPacedFlip;
		uint32_t			fFlipFailures;
		// Set while a modeset is in flight; a flip EINVAL during a
		// modeset is transient, not proof the flip is broken.
		std::atomic<bool>	fModesetActive;
			uint32_t			fModeBlobId;
			struct PlaneProps	fPlaneProps;
			struct PlaneProps	fCursorPlaneProps;
			struct CrtcProps	fCrtcProps;
			struct ConnProps	fConnProps;

			// Overlay planes: discovery list, claim state, and the
			// legacy channel/buffer shims Overlay goes through.
			std::vector<OverlayPlaneInfo>	fOverlayPlanes;
			pthread_mutex_t		fOverlayMutex;
			int32				fOverlayChannelCounter;
			std::vector<overlay_token>	fOverlayChannels;
			struct DrmOverlayBuffer {
				overlay_buffer	buffer;
				DrmOverlayPlane*	plane;
			};
			std::vector<DrmOverlayBuffer*>	fOverlayBuffers;

			status_t			_ClaimOverlayPlane(uint32 width, uint32 height,
									uint32 fourcc, uint32 bpp,
									color_space space, int dmabufFd,
									uint32 stride, uint32 sharedHandle,
									OverlayPlane** _plane);
			void				_RearmOverlayPlanes();
			void				_ReleaseOverlayPlanes();
			OverlayPlaneInfo*	_FindOverlayPlaneInfo(uint32_t planeId);
			DrmOverlayBuffer*	_FindOverlayBuffer(const overlay_buffer* buffer);

			bool				fVRRSupported;
			bool				fVRREnabled;

			// Elaborated as "enum PanelOrientation": the PanelOrientation()
			// accessor above hides the bare type name inside this class.
			// What the scanout applies: fMountingOrientation composed with
			// fUserOrientation. Every present path rotates by this.
			enum PanelOrientation
								fPanelOrientation;
			// How the panel sits in the device (kernel property, DMI quirk,
			// VOS_PANEL_ORIENTATION), and the user's rotation relative to it.
			int32				fMountingOrientation;
			int32				fUserOrientation;
			// B_PANEL_REFLECTION_* from PanelOrientationTransform.h.
			int32				fPanelReflection;
};

#endif
