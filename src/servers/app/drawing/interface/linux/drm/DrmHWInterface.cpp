/*
 * Copyright 2021-2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmHWInterface.h"

#include "DrmBuffer.h"

#include <algorithm>
#include <errno.h>
#include <libdrm/drm_mode.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>

#include "modeset.h"


int DrmHWInterface::fFd = -1;

extern "C" void seat_enable_cb(struct libseat* seat, void* data)
{
	DrmHWInterface* hw = static_cast<DrmHWInterface*>(data);
	hw->_OnSessionEnable();

}

extern "C" void seat_disable_cb(struct libseat* seat, void* data)
{
	DrmHWInterface* hw = static_cast<DrmHWInterface*>(data);

	hw->_OnSessionDisable();

}

static struct libseat_seat_listener seat_listener = {
	.enable_seat = seat_enable_cb,
	.disable_seat = seat_disable_cb,
};


DrmHWInterface::DrmHWInterface()
	:
	HWInterface(),
	fFrontBuffer(NULL),
	fBackBuffer(NULL),
	fWriteTarget(NULL),
	fSeat(NULL),
	fDeviceId(-1),
	fSessionActive(false),
	fInitialized(false),
	fRunning(false),
	fEventThread(-1),
	fSessionSem(create_sem(0, "drm session sem")),
	fUdev(NULL),
	fUdevMonitor(NULL),
	fUdevFd(-1),
#ifdef HAVE_GBM
	fGbmDevice(NULL),
	fUseGbm(false),
#endif
	fRenderBuffer(NULL),
	fPageFlipEnabled(false),
	fPageFlipPending(false),
	fFlipDirtyCount(0),
	fDpmsState(B_DPMS_ON),
	fBacklight(NULL),
	fAtomicSupported(false),
	fPrimaryPlaneId(0),
	fCursorPlaneId(0),
	fModeBlobId(0),
	fPlaneProps{},
	fCursorPlaneProps{},
	fCrtcProps{},
	fConnProps{},
	fVRRSupported(false),
	fVRREnabled(false)
{
	fSeat = libseat_open_seat(&seat_listener, this);
	if (!fSeat) {
		fprintf(stderr, "Failed to open libseat session\n");
		return;
	}

	printf("libseat opened, fSeat=%p, seat_fd=%d\n", (void*)fSeat,
		libseat_get_fd(fSeat));

	while (!fSessionActive) {
		int ret = libseat_dispatch(fSeat, -1);
		if (ret < 0)
			break;
	}

	if (!fSessionActive) {
		libseat_close_seat(fSeat);
		fSeat = NULL;
		return;
	}

	fRunning = true;
	fEventThread = spawn_thread(_EventThreadEntry, "drm event thread",
		B_NORMAL_PRIORITY, this);
	if (fEventThread >= 0)
		resume_thread(fEventThread);
}


void
DrmHWInterface::_OnSessionEnable()
{
	printf("Session enabled\n");

	if (fInitialized) {
		fSessionActive = true;
		release_sem(fSessionSem);

		_RestoreDisplay();

		{
			LockExclusiveAccess();
			Invalidate(BRect(0, 0, fDisplayMode.virtual_width - 1,
					fDisplayMode.virtual_height - 1));
			UnlockExclusiveAccess();
		}
		return;
	}

	const char* janusDrmFdStr = getenv("JANUS_DRM_FD");
	if (janusDrmFdStr != NULL && janusDrmFdStr[0] != '\0') {
		fFd = atoi(janusDrmFdStr);
		fDeviceId = 0;
	} else {
		char path[B_PATH_NAME_LENGTH];
		for (int i = 0; i <= 9; ++i) {
			snprintf(path, sizeof(path), "/dev/dri/card%d", i);
			fDeviceId = libseat_open_device(fSeat, path, &fFd);
			if (fDeviceId >= 0)
				break;
		}
	}

	if (fFd < 0) {
		fprintf(stderr, "Failed to open DRM device via libseat\n");
		return;
	}

	int ret = modeset_prepare(fFd);

	if (ret) {
		libseat_close_device(fSeat, fDeviceId);
		return;
	}

	// Save current CRTC state for restoration on exit.  Do NOT set the
	// CRTC yet: iter->fb is still the dumb buffer allocated by
	// modeset_prepare.  GBM front/back buffers will replace it below, and
	// the CRTC must be configured with the same framebuffer type that page
	// flips will target, otherwise drmModePageFlip fails on tiled memory.
	struct modeset_dev *iter;
	for (iter = get_dev(); iter; iter = iter->next)
		iter->saved_crtc = drmModeGetCrtc(fFd, iter->crtc);

#ifdef HAVE_GBM
	fGbmDevice = gbm_create_device(fFd);
	fUseGbm = (fGbmDevice != NULL);
	if (!fUseGbm)
		fprintf(stderr, "GBM unavailable, using dumb buffers (Intel FBC may not work)\n");

	bool gbmOk = false;
	if (fUseGbm) {
		gbmOk = (modeset_create_gbm_fb(fFd, fGbmDevice, get_dev(), false) == 0);
	}

	if (!gbmOk) {
		if (get_dev()->front_bo) {
			drmModeRmFB(fFd, get_dev()->fb);
			gbm_bo_destroy(get_dev()->front_bo);
			get_dev()->front_bo = NULL;
		}
		modeset_create_fb(fFd, get_dev());
	}
#else
	modeset_create_fb(fFd, get_dev());
#endif

	fFrontBuffer = new DrmBuffer(fFd, get_dev(), false);
	fBackBuffer = NULL;
	fWriteTarget = fFrontBuffer;
	fPageFlipEnabled = false;
	fPageFlipPending = false;
	fFlipDirtyCount = 0;

#ifdef HAVE_GBM
	if (fUseGbm
		&& modeset_create_gbm_fb(fFd, fGbmDevice, get_dev(), true) == 0) {
		fBackBuffer = new DrmBuffer(fFd, get_dev(), true);
		fWriteTarget = fBackBuffer;
		fPageFlipEnabled = true;
	}
#endif

	fRenderBuffer = new MallocBuffer(get_dev()->width, get_dev()->height);

	// Probe atomic modesetting and discover object property IDs.
	_ProbeAtomic();
	if (fAtomicSupported)
		_DiscoverProperties();

	// Now that dev->fb reflects the actual front framebuffer (GBM or dumb),
	// configure the CRTC.  Page flips must target the same memory type.
	for (iter = get_dev(); iter; iter = iter->next) {
		if (fAtomicSupported && fPrimaryPlaneId) {
			status_t r = _AtomicModeset(iter->fb, &iter->mode);
			if (r != B_OK)
				fprintf(stderr, "atomic modeset failed for connector %u: %m\n",
					iter->conn);
		} else {
			ret = drmModeSetCrtc(fFd, iter->crtc, iter->fb, 0, 0,
			                     &iter->conn, 1, &iter->mode);
			if (ret)
				fprintf(stderr, "cannot set CRTC for connector %u (%d): %m\n",
					iter->conn, errno);
		}
	}

	modeset_create_cursor_fb(fFd, get_dev());

	// Probe hardware cursor support before any SetCursor call so that
	// IsDoubleBuffered() returns the correct value when the base class
	// creates (or skips) fCursorAreaBackup.
	{
		struct modeset_dev* dev = get_dev();
		if (dev && dev->cursor_handle) {
			// Try legacy cursor first; fall back to atomic cursor plane.
			int r = drmModeSetCursor(fFd, dev->crtc,
				dev->cursor_handle, dev->cursor_w, dev->cursor_h);
			if (r == 0) {
				fHardwareCursorEnabled = true;
				drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
			} else if (fAtomicSupported && fCursorPlaneId) {
				// Atomic-only driver — probe via cursor plane.
				r = (_AtomicSetCursor(dev->cursor_handle, dev->crtc,
					0, 0, dev->cursor_w, dev->cursor_h) == B_OK) ? 0 : -1;
				if (r == 0) {
					fHardwareCursorEnabled = true;
					// Hide until a real bitmap arrives.
					_AtomicSetCursor(0, 0, 0, 0, 0, 0);
				} else {
					fprintf(stderr, "DRM: hardware cursor not supported"
						" (legacy: %s, atomic: failed),"
						" using software cursor\n", strerror(-r));
					fHardwareCursorEnabled = false;
				}
			} else {
				fprintf(stderr, "DRM: hardware cursor not supported (%s),"
					" using software cursor\n", strerror(-r));
				fHardwareCursorEnabled = false;
			}
		}
	}

	fUdev = udev_new();
	if (fUdev) {
		struct stat st;
		if (fstat(fFd, &st) == 0) {
			struct udev_device* udevDev = udev_device_new_from_devnum(fUdev,
				'c', st.st_rdev);
			if (udevDev) {
				struct modeset_dev* dev = get_dev();
				drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
				if (conn) {
					fBacklight = backlight_init(udevDev, conn->connector_type);
					drmModeFreeConnector(conn);
				}
				udev_device_unref(udevDev);
			}
		}

		fUdevMonitor = udev_monitor_new_from_netlink(fUdev, "udev");
		if (fUdevMonitor) {
			udev_monitor_filter_add_match_subsystem_devtype(
				fUdevMonitor, "drm", NULL);
			udev_monitor_enable_receiving(fUdevMonitor);
			fUdevFd = udev_monitor_get_fd(fUdevMonitor);
		}
	}

	fDisplayMode.virtual_width = get_dev()->width;
	fDisplayMode.virtual_height = get_dev()->height;
	fDisplayMode.space = B_RGB32;

	fSessionActive = true;
	fInitialized = true;
	release_sem(fSessionSem);
}


void
DrmHWInterface::_OnSessionDisable()
{
	printf("Session disabled\n");

	fSessionActive = false;
	fPageFlipPending = false;
	fFlipDirtyCount = 0;

	libseat_disable_seat(fSeat);
}


void
DrmHWInterface::_RestoreDisplay()
{

	if (fFd < 0) {
	
		return;
	}

	struct modeset_dev *iter;
	for (iter = get_dev(); iter; iter = iter->next) {
		if (fAtomicSupported && fPrimaryPlaneId)
			_AtomicModeset(iter->fb, &iter->mode);
		else
			drmModeSetCrtc(fFd, iter->crtc, iter->fb, 0, 0,
						 &iter->conn, 1, &iter->mode);
	}
}


/*static*/ int32
DrmHWInterface::_EventThreadEntry(void* data)
{
	static_cast<DrmHWInterface*>(data)->_EventThreadMain();
	return 0;
}


/*static*/ void
DrmHWInterface::_PageFlipHandler(int fd, unsigned int frame,
	unsigned int sec, unsigned int usec, void* data)
{
	DrmHWInterface* hw = static_cast<DrmHWInterface*>(data);
	hw->fPageFlipPending = false;
	std::swap(hw->fFrontBuffer, hw->fBackBuffer);
	hw->fWriteTarget = hw->fBackBuffer;

	if (hw->fRenderBuffer != NULL) {
		for (int32 i = 0; i < hw->fFlipDirtyCount; i++)
			hw->_BlitRect(hw->fRenderBuffer, hw->fWriteTarget,
				hw->fFlipDirtyRects[i]);
	}
	hw->fFlipDirtyCount = 0;
}


void
DrmHWInterface::_EventThreadMain()
{
	int seatErrorCount = 0;

	while (fRunning) {
		int seat_fd = fSeat ? libseat_get_fd(fSeat) : -1;

		if (seat_fd < 0) {
			seatErrorCount++;
			snooze(100000);
			continue;
		}
		seatErrorCount = 0;

		bool active = fSessionActive.load();

		struct pollfd pfds[3];
		int nfds = 0;

		pfds[nfds].fd = seat_fd;
		pfds[nfds].events = POLLIN;
		pfds[nfds].revents = 0;
		int seat_idx = nfds++;

		int drm_idx  = -1;
		int udev_idx = -1;

		// DRM events (page-flip completions, etc.) are handled here.
		if (active && fFd >= 0) {
			pfds[nfds].fd = fFd;
			pfds[nfds].events = POLLIN;
			pfds[nfds].revents = 0;
			drm_idx = nfds++;
		}

		if (fUdevFd >= 0) {
			pfds[nfds].fd = fUdevFd;
			pfds[nfds].events = POLLIN;
			pfds[nfds].revents = 0;
			udev_idx = nfds++;
		}

		int ret = poll(pfds, nfds, 100);
		if (ret > 0) {
			if (pfds[seat_idx].revents & (POLLIN | POLLHUP)) {
				int dret = libseat_dispatch(fSeat, 0);
				if (dret < 0 && !active)
					printf("libseat_dispatch: error\n");
			}
			if (drm_idx >= 0 && (pfds[drm_idx].revents & POLLIN)) {
				static drmEventContext evctx = {
					.version           = DRM_EVENT_CONTEXT_VERSION,
					.page_flip_handler = DrmHWInterface::_PageFlipHandler,
				};
				drmHandleEvent(fFd, &evctx);
			}

			if (fPageFlipEnabled && !fPageFlipPending
				&& fFlipDirtyCount > 0 && fBackBuffer != NULL) {
				struct modeset_dev* dev = get_dev();
				int ret;
				if (fAtomicSupported && fPrimaryPlaneId) {
					ret = (_AtomicFlip(fWriteTarget->GetFbId(),
						fFlipDirtyRects,
						fFlipDirtyCount) == B_OK) ? 0 : -1;
				} else {
					ret = drmModePageFlip(fFd, dev->crtc,
						fWriteTarget->GetFbId(),
						DRM_MODE_PAGE_FLIP_EVENT, this);
				}
				if (ret == 0)
					fPageFlipPending = true;
				else
					fFlipDirtyCount = 0;
			}
			if (udev_idx >= 0 && (pfds[udev_idx].revents & POLLIN)) {
				_HandleHotplug();
			}
		}
	}
}


DrmHWInterface::~DrmHWInterface()
{
	CALLED();

	fRunning = false;
	release_sem(fSessionSem);

	if (fEventThread >= 0) {
		status_t exitValue;
		wait_for_thread(fEventThread, &exitValue);
	}

	delete_sem(fSessionSem);

	if (fFd >= 0 && fPageFlipPending) {
		drmEventContext evctx = {
			.version           = DRM_EVENT_CONTEXT_VERSION,
			.page_flip_handler = DrmHWInterface::_PageFlipHandler,
		};
		struct pollfd pfd = { fFd, POLLIN, 0 };
		for (int i = 0; i < 4 && fPageFlipPending; i++) {
			if (poll(&pfd, 1, 16) > 0 && (pfd.revents & POLLIN))
				drmHandleEvent(fFd, &evctx);
		}
	}

	if (fUdevMonitor)
		udev_monitor_unref(fUdevMonitor);
	if (fUdev)
		udev_unref(fUdev);

	delete fFrontBuffer;
	fFrontBuffer = NULL;
	delete fBackBuffer;
	fBackBuffer = NULL;
	delete fRenderBuffer;
	fRenderBuffer = NULL;

#ifdef HAVE_GBM
	struct modeset_dev* dev = get_dev();
	if (dev) {
		if (dev->front_bo) {
			gbm_bo_unmap(dev->front_bo, dev->front_map_data);
			gbm_bo_destroy(dev->front_bo);
			dev->front_bo = NULL;
			dev->front_map_data = NULL;
		}
		if (dev->back_bo) {
			gbm_bo_unmap(dev->back_bo, dev->back_map_data);
			gbm_bo_destroy(dev->back_bo);
			dev->back_bo = NULL;
			dev->back_map_data = NULL;
		}
	}
	if (fGbmDevice) {
		gbm_device_destroy(fGbmDevice);
		fGbmDevice = NULL;
	}
#endif

	if (fBacklight) {
		backlight_destroy(fBacklight);
		fBacklight = NULL;
	}

	if (fModeBlobId) {
		drmModeDestroyPropertyBlob(fFd, fModeBlobId);
		fModeBlobId = 0;
	}

	if (fSeat && fDeviceId > 0)
		libseat_close_device(fSeat, fDeviceId);

	if (fSeat)
		libseat_close_seat(fSeat);

	modeset_cleanup(fFd);
}


status_t
DrmHWInterface::Initialize()
{
	status_t ret = HWInterface::Initialize();
	if (ret != B_OK)
		return ret;

	if (fFrontBuffer == NULL)
		return B_ERROR;

	ret = fFrontBuffer->InitCheck();
	if (ret != B_OK)
		return ret;

	return B_OK;
}


status_t
DrmHWInterface::InitCheck() const
{
	if (fFd < 0 || fFrontBuffer == NULL)
		return B_ERROR;
	return B_OK;
}


EventStream*
DrmHWInterface::CreateEventStream()
{
	return NULL;
}


status_t
DrmHWInterface::Shutdown()
{
	CALLED();
	return B_OK;
}


status_t
DrmHWInterface::SetMode(const display_mode& mode)
{
	CALLED();

	struct modeset_dev* dev = get_dev();
	if (!dev || fFd < 0)
		return B_ERROR;

	drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
	if (!conn) return B_ERROR;

	drmModeModeInfo* found = NULL;
	for (int i = 0; i < conn->count_modes; i++) {
		if (conn->modes[i].hdisplay == mode.virtual_width &&
		    conn->modes[i].vdisplay == mode.virtual_height) {
			found = &conn->modes[i];
			break;
		}
	}

	if (!found) {
		drmModeFreeConnector(conn);
		return B_BAD_VALUE;
	}

	delete fFrontBuffer; fFrontBuffer = NULL;
	delete fBackBuffer;  fBackBuffer  = NULL;
	delete fRenderBuffer; fRenderBuffer = NULL;

#ifdef HAVE_GBM
	if (dev->front_bo) {
		gbm_bo_unmap(dev->front_bo, dev->front_map_data);
		dev->front_map_data = NULL;
		gbm_bo_destroy(dev->front_bo); dev->front_bo = NULL;
	}
	if (dev->back_bo) {
		gbm_bo_unmap(dev->back_bo, dev->back_map_data);
		dev->back_map_data = NULL;
		gbm_bo_destroy(dev->back_bo); dev->back_bo = NULL;
	}
#endif

	if (dev->fb)      { drmModeRmFB(fFd, dev->fb);      dev->fb = 0; }
	if (dev->back_fb) { drmModeRmFB(fFd, dev->back_fb); dev->back_fb = 0; }

	dev->width  = found->hdisplay;
	dev->height = found->vdisplay;
	memcpy(&dev->mode, found, sizeof(*found));
	drmModeFreeConnector(conn);

	bool ok = false;
#ifdef HAVE_GBM
	if (fUseGbm) {
		ok = (modeset_create_gbm_fb(fFd, fGbmDevice, dev, false) == 0);
	}
#endif
	if (!ok) {
		ok = (modeset_create_fb(fFd, dev) == 0);
	}

	fFrontBuffer = new DrmBuffer(fFd, dev, false);
	fBackBuffer = NULL;
	fWriteTarget = fFrontBuffer;
	fPageFlipEnabled = false;
	fPageFlipPending = false;
	fFlipDirtyCount = 0;

#ifdef HAVE_GBM
	if (fUseGbm && modeset_create_gbm_fb(fFd, fGbmDevice, dev, true) == 0) {
		fBackBuffer = new DrmBuffer(fFd, dev, true);
		fWriteTarget = fBackBuffer;
		fPageFlipEnabled = true;
	}
#endif

	fRenderBuffer = new MallocBuffer(dev->width, dev->height);

	int ret;
	if (fAtomicSupported && fPrimaryPlaneId) {
		if (_AtomicModeset(dev->fb, &dev->mode) != B_OK) return B_ERROR;
		ret = 0;
	} else {
		ret = drmModeSetCrtc(fFd, dev->crtc, dev->fb, 0, 0,
		                     &dev->conn, 1, &dev->mode);
		if (ret) return B_ERROR;
	}

	fDisplayMode.virtual_width  = dev->width;
	fDisplayMode.virtual_height = dev->height;

	return B_OK;
}


void
DrmHWInterface::GetMode(display_mode* mode)
{
	CALLED();
	*mode = fDisplayMode;
}


status_t
DrmHWInterface::GetPreferredMode(display_mode* mode)
{
	CALLED();
	*mode = fDisplayMode;
	return B_OK;
}


status_t
DrmHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
DrmHWInterface::GetFrameBufferConfig(frame_buffer_config& config)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
DrmHWInterface::GetModeList(display_mode** _modeList, uint32* _count)
{
	CALLED();

	struct modeset_dev* dev = get_dev();
	if (dev == NULL || fFd < 0)
		return B_ERROR;

	drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
	if (conn == NULL)
		return B_ERROR;

	int count = conn->count_modes;
	if (count == 0) {
		drmModeFreeConnector(conn);
		*_modeList = NULL;
		*_count = 0;
		return B_OK;
	}

	display_mode* modes = new(std::nothrow) display_mode[count];
	if (modes == NULL) {
		drmModeFreeConnector(conn);
		return B_NO_MEMORY;
	}

	for (int i = 0; i < count; i++) {
		drmModeModeInfo& m = conn->modes[i];
		display_mode& dm = modes[i];

		dm.timing.pixel_clock  = m.clock;
		dm.timing.h_display    = m.hdisplay;
		dm.timing.h_sync_start = m.hsync_start;
		dm.timing.h_sync_end   = m.hsync_end;
		dm.timing.h_total      = m.htotal;
		dm.timing.v_display    = m.vdisplay;
		dm.timing.v_sync_start = m.vsync_start;
		dm.timing.v_sync_end   = m.vsync_end;
		dm.timing.v_total      = m.vtotal;

		dm.timing.flags = 0;
		if (m.flags & DRM_MODE_FLAG_PHSYNC)
			dm.timing.flags |= B_POSITIVE_HSYNC;
		if (m.flags & DRM_MODE_FLAG_PVSYNC)
			dm.timing.flags |= B_POSITIVE_VSYNC;
		if (m.flags & DRM_MODE_FLAG_INTERLACE)
			dm.timing.flags |= B_TIMING_INTERLACED;

		dm.space        = B_RGB32;
		dm.virtual_width  = m.hdisplay;
		dm.virtual_height = m.vdisplay;
		dm.h_display_start = 0;
		dm.v_display_start = 0;
		dm.flags = 0;
	}

	drmModeFreeConnector(conn);

	*_modeList = modes;
	*_count = (uint32)count;
	return B_OK;
}


status_t
DrmHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low, uint32* _high)
{
	CALLED();

	struct modeset_dev* dev = get_dev();
	if (dev == NULL || fFd < 0)
		return B_ERROR;

	drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
	if (conn == NULL)
		return B_ERROR;

	if (conn->count_modes == 0) {
		drmModeFreeConnector(conn);
		return B_ERROR;
	}

	uint32 low  = conn->modes[0].clock;
	uint32 high = conn->modes[0].clock;
	for (int i = 1; i < conn->count_modes; i++) {
		if (conn->modes[i].clock < low)
			low = conn->modes[i].clock;
		if (conn->modes[i].clock > high)
			high = conn->modes[i].clock;
	}

	drmModeFreeConnector(conn);

	*_low  = low;
	*_high = high;
	return B_OK;
}


status_t
DrmHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
DrmHWInterface::ProposeMode(display_mode* candidate,
	const display_mode* low, const display_mode* high)
{
	CALLED();

	display_mode* modeList = NULL;
	uint32 count = 0;
	status_t err = GetModeList(&modeList, &count);
	if (err != B_OK)
		return err;
	if (count == 0)
		return B_BAD_VALUE;

	// Find the mode closest to the candidate by pixel area difference,
	// within the low/high bounds if provided.
	uint32 targetW = candidate->virtual_width;
	uint32 targetH = candidate->virtual_height;

	int best = -1;
	uint32 bestDiff = UINT32_MAX;
	for (uint32 i = 0; i < count; i++) {
		display_mode& m = modeList[i];

		if (low != NULL) {
			if (m.virtual_width  < low->virtual_width  ||
			    m.virtual_height < low->virtual_height ||
			    m.timing.pixel_clock < low->timing.pixel_clock)
				continue;
		}
		if (high != NULL) {
			if (m.virtual_width  > high->virtual_width  ||
			    m.virtual_height > high->virtual_height ||
			    m.timing.pixel_clock > high->timing.pixel_clock)
				continue;
		}

		uint32 dw = (m.virtual_width  > targetW) ? m.virtual_width  - targetW
		                                          : targetW - m.virtual_width;
		uint32 dh = (m.virtual_height > targetH) ? m.virtual_height - targetH
		                                          : targetH - m.virtual_height;
		uint32 diff = dw * dw + dh * dh;
		if (diff < bestDiff) {
			bestDiff = diff;
			best = (int)i;
		}
	}

	if (best < 0) {
		// Nothing in bounds — pick closest overall
		best = 0;
		bestDiff = UINT32_MAX;
		for (uint32 i = 0; i < count; i++) {
			display_mode& m = modeList[i];
			uint32 dw = (m.virtual_width  > targetW) ? m.virtual_width  - targetW
			                                          : targetW - m.virtual_width;
			uint32 dh = (m.virtual_height > targetH) ? m.virtual_height - targetH
			                                          : targetH - m.virtual_height;
			uint32 diff = dw * dw + dh * dh;
			if (diff < bestDiff) {
				bestDiff = diff;
				best = (int)i;
			}
		}
	}

	*candidate = modeList[best];
	delete[] modeList;

	return (bestDiff == 0) ? B_OK : B_BAD_VALUE;
}


sem_id
DrmHWInterface::RetraceSemaphore()
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
DrmHWInterface::WaitForRetrace(bigtime_t timeout)
{
	if (fFd < 0)
		return B_ERROR;

	drm_wait_vblank wait;
	memset(&wait, 0, sizeof(wait));
	wait.request.type = (drm_vblank_seq_type)DRM_VBLANK_RELATIVE;
	wait.request.sequence = 1;

	bool infinite = (timeout < 0);
	bigtime_t start_us = infinite ? 0 : system_time();

	for (;;) {
		if (ioctl(fFd, DRM_IOCTL_WAIT_VBLANK, &wait) == 0)
			return B_OK;

		if (errno == EINTR) {
			if (!infinite) {
				if (system_time() - start_us >= timeout)
					return B_TIMED_OUT;
			}
			continue;
		}
		return B_ERROR;
	}
}


status_t
DrmHWInterface::SetDPMSMode(uint32 state)
{
	struct modeset_dev* dev = get_dev();
	if (!dev || fFd < 0) return B_ERROR;

	int dpms;
	switch (state) {
		case B_DPMS_ON:      dpms = DRM_MODE_DPMS_ON;      break;
		case B_DPMS_STAND_BY: dpms = DRM_MODE_DPMS_STANDBY; break;
		case B_DPMS_SUSPEND: dpms = DRM_MODE_DPMS_SUSPEND;  break;
		case B_DPMS_OFF:     dpms = DRM_MODE_DPMS_OFF;     break;
		default: return B_BAD_VALUE;
	}

	// Fast path: use the DPMS prop ID cached during _DiscoverConnProps.
	if (fConnProps.dpms) {
		int ret = drmModeConnectorSetProperty(fFd, dev->conn,
			fConnProps.dpms, dpms);
		if (ret == 0) {
			fDpmsState = state;
			return B_OK;
		}
		return B_ERROR;
	}

	// Fallback for non-atomic path where _DiscoverConnProps was not called.
	drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
	if (!conn) return B_ERROR;

	for (int i = 0; i < conn->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fFd, conn->props[i]);
		if (prop && strcmp(prop->name, "DPMS") == 0) {
			drmModeConnectorSetProperty(fFd, dev->conn, prop->prop_id, dpms);
			drmModeFreeProperty(prop);
			drmModeFreeConnector(conn);
			fDpmsState = state;
			return B_OK;
		}
		if (prop) drmModeFreeProperty(prop);
	}

	drmModeFreeConnector(conn);
	return B_UNSUPPORTED;
}


uint32
DrmHWInterface::DPMSMode()
{
	return fDpmsState;
}


uint32
DrmHWInterface::DPMSCapabilities()
{
	return B_DPMS_ON | B_DPMS_OFF | B_DPMS_STAND_BY | B_DPMS_SUSPEND;
}


status_t
DrmHWInterface::SetBrightness(float brightness)
{
	if (!fBacklight) return B_UNSUPPORTED;
	int max = (int)backlight_get_max_brightness(fBacklight);
	int val = (int)(brightness * max + 0.5f);
	return backlight_set_brightness(fBacklight, val) == 0 ? B_OK : B_ERROR;
}


status_t
DrmHWInterface::GetBrightness(float* brightness)
{
	if (!fBacklight || !brightness) return B_UNSUPPORTED;
	int max = (int)backlight_get_max_brightness(fBacklight);
	int cur = (int)backlight_get_brightness(fBacklight);
	*brightness = (max > 0) ? (float)cur / max : 0.0f;
	return B_OK;
}


RenderingBuffer*
DrmHWInterface::FrontBuffer() const
{
	CALLED();
	return fFrontBuffer;
}


RenderingBuffer*
DrmHWInterface::BackBuffer() const
{
	CALLED();
	if (fRenderBuffer != NULL) return fRenderBuffer;
	return fFrontBuffer;
}


bool
DrmHWInterface::IsDoubleBuffered() const
{
	return fRenderBuffer != NULL;
}


void
DrmHWInterface::_BlitRect(RenderingBuffer* src, RenderingBuffer* dst,
	const BRect& frame)
{
	int32 x = (int32)frame.left,  y = (int32)frame.top;
	int32 w = (int32)(frame.right - frame.left + 1);
	int32 h = (int32)(frame.bottom - frame.top + 1);

	int32 bufW = (int32)dst->Width(), bufH = (int32)dst->Height();
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > bufW) w = bufW - x;
	if (y + h > bufH) h = bufH - y;
	if (w <= 0 || h <= 0) return;

	uint32 srcBpr = src->BytesPerRow(), dstBpr = dst->BytesPerRow();
	uint8* s = (uint8*)src->Bits() + y * srcBpr + x * 4;
	uint8* d = (uint8*)dst->Bits() + y * dstBpr + x * 4;
	for (int32 row = 0; row < h; row++, s += srcBpr, d += dstBpr)
		memcpy(d, s, w * 4);
}


status_t
DrmHWInterface::CopyBackToFront(const BRect& frame)
{
	if (fFrontBuffer == NULL)
		return B_ERROR;

	if (!fHardwareCursorEnabled)
		return HWInterface::CopyBackToFront(frame);

	if (fRenderBuffer != NULL) {
		_BlitRect(fRenderBuffer, fWriteTarget, frame);

		if (fPageFlipEnabled && fFlipDirtyCount < kMaxDirtyRects)
			fFlipDirtyRects[fFlipDirtyCount++] = frame;
	}

	return B_OK;
}


void
DrmHWInterface::SetCursor(ServerCursor* cursor)
{
	HWInterface::SetCursor(cursor);

	struct modeset_dev* dev = get_dev();
	if (!dev) return;

	if (cursor == NULL) {
		if (dev->cursor_ok) {
			drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
			fHardwareCursorEnabled = true;
		} else {
			fHardwareCursorEnabled = false;
		}
		return;
	}

	memset(dev->cursor_map, 0, dev->cursor_size);

	int32 cw = std::min((int32)cursor->Bounds().IntegerWidth() + 1, 64);
	int32 ch = std::min((int32)cursor->Bounds().IntegerHeight() + 1, 64);
	const uint8* src = (const uint8*)cursor->Bits();
	uint8* dst = dev->cursor_map;
	for (int32 row = 0; row < ch; row++) {
		memcpy(dst + row * 256, src + row * cursor->BytesPerRow(), cw * 4);
	}

	int ret;
	if (fAtomicSupported && fCursorPlaneId) {
		ret = (_AtomicSetCursor(dev->cursor_handle, dev->crtc,
			(int32)fCursorLocation.x, (int32)fCursorLocation.y,
			64, 64) == B_OK) ? 0 : -1;
	} else {
		ret = drmModeSetCursor2(fFd, dev->crtc,
			dev->cursor_handle, 64, 64,
			(int32)cursor->GetHotSpot().x,
			(int32)cursor->GetHotSpot().y);
	}
	if (ret == 0) {
		dev->cursor_ok = true;
		fHardwareCursorEnabled = true;
	} else {
		dev->cursor_ok = false;
		fHardwareCursorEnabled = false;
	}
}


void
DrmHWInterface::SetCursorVisible(bool visible)
{
	HWInterface::SetCursorVisible(visible);

	struct modeset_dev* dev = get_dev();
	if (!dev || !dev->cursor_ok) return;

	if (!visible) {
		if (fAtomicSupported && fCursorPlaneId)
			_AtomicSetCursor(0, 0, 0, 0, 0, 0);
		else
			drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
	} else {
		if (fAtomicSupported && fCursorPlaneId)
			_AtomicSetCursor(dev->cursor_handle, dev->crtc,
				(int32)fCursorLocation.x, (int32)fCursorLocation.y,
				dev->cursor_w, dev->cursor_h);
		else
			drmModeSetCursor(fFd, dev->crtc, dev->cursor_handle,
				dev->cursor_w, dev->cursor_h);
	}
}


void
DrmHWInterface::MoveCursorTo(float x, float y)
{
	HWInterface::MoveCursorTo(x, y);

	struct modeset_dev* dev = get_dev();
	if (!dev || !dev->cursor_ok) return;

	drmModeMoveCursor(fFd, dev->crtc, (int)x, (int)y);
}


void
DrmHWInterface::_DrawCursor(IntRect area) const
{
	// When the hardware cursor is active, the DRM plane handles compositing
	// independently of the framebuffer. Suppress the software cursor path
	// entirely to prevent cursor bitmaps being baked into the framebuffer.
	if (!fHardwareCursorEnabled)
		HWInterface::_DrawCursor(area);
}


void
DrmHWInterface::_HandleHotplug()
{
	struct udev_device* dev = udev_monitor_receive_device(fUdevMonitor);
	if (!dev)
		return;

	const char* hotplug = udev_device_get_property_value(dev, "HOTPLUG");
	if (hotplug != NULL && strcmp(hotplug, "1") == 0 && fFd >= 0) {
		fprintf(stderr, "DRM hotplug event\n");

		drmModeRes* res = drmModeGetResources(fFd);
		if (res) {
			for (int i = 0; i < res->count_connectors; i++) {
				drmModeConnector* conn = drmModeGetConnector(fFd,
					res->connectors[i]);
				if (conn) {
					if (conn->connection == DRM_MODE_CONNECTED)
						modeset_add_connector(fFd, conn->connector_id);
					else
						modeset_remove_connector(conn->connector_id);
					drmModeFreeConnector(conn);
				}
			}
			drmModeFreeResources(res);
		}
	}

	udev_device_unref(dev);
}


status_t
DrmHWInterface::CreateLease(uint32_t* connectors, int connCount,
	uint32_t* crtcs, int crtcCount, int* leaseFd)
{
	if (fFd < 0)
		return B_ERROR;

	int total = connCount + crtcCount;
	uint32_t* objects = new uint32_t[total];
	for (int i = 0; i < connCount; i++)
		objects[i] = connectors[i];
	for (int i = 0; i < crtcCount; i++)
		objects[connCount + i] = crtcs[i];

	int fd = drmModeCreateLease(fFd, objects, total, 0, (uint32_t*)leaseFd);
	delete[] objects;

	if (fd < 0) {
		fprintf(stderr, "drmModeCreateLease failed: %m\n");
		return B_ERROR;
	}
	return B_OK;
}


void
DrmHWInterface::RevokeLease(int leaseFd)
{
	if (leaseFd >= 0)
		close(leaseFd);
}


// ---------------------------------------------------------------------------
// Atomic modesetting
// ---------------------------------------------------------------------------

void
DrmHWInterface::_ProbeAtomic()
{
	fAtomicSupported = false;
	// Universal planes must be enabled first so primary + cursor planes appear
	// in the plane list when _DiscoverProperties() queries them.
	drmSetClientCap(fFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	fAtomicSupported = (drmSetClientCap(fFd, DRM_CLIENT_CAP_ATOMIC, 1) == 0);
	printf("DRM: atomic modesetting %s\n",
		fAtomicSupported ? "supported" : "not supported (legacy fallback)");
}


int
DrmHWInterface::_CrtcIndex(uint32_t crtc_id)
{
	drmModeRes* res = drmModeGetResources(fFd);
	if (!res) return 0;
	int idx = 0;
	for (int i = 0; i < res->count_crtcs; i++) {
		if (res->crtcs[i] == crtc_id) {
			idx = i;
			break;
		}
	}
	drmModeFreeResources(res);
	return idx;
}


void
DrmHWInterface::_DiscoverProperties()
{
	struct modeset_dev* dev = get_dev();
	if (!dev) return;

	// Find the primary plane that feeds our CRTC.
	int crtcIdx = _CrtcIndex(dev->crtc);
	drmModePlaneResPtr planes = drmModeGetPlaneResources(fFd);
	if (planes) {
		for (uint32_t i = 0; i < planes->count_planes; i++) {
			drmModePlanePtr plane = drmModeGetPlane(fFd, planes->planes[i]);
			if (!plane) continue;
			bool forOurCrtc = (plane->possible_crtcs & (1u << crtcIdx)) != 0;
			drmModeFreePlane(plane);
			if (!forOurCrtc) continue;

			drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(
				fFd, planes->planes[i], DRM_MODE_OBJECT_PLANE);
			if (!props) continue;
			for (uint32_t j = 0; j < props->count_props; j++) {
				drmModePropertyPtr prop = drmModeGetProperty(fFd, props->props[j]);
				if (!prop) continue;
				if (strcmp(prop->name, "type") == 0) {
					if (props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY
							&& !fPrimaryPlaneId) {
						fPrimaryPlaneId = planes->planes[i];
						_DiscoverPlaneProps(planes->planes[i], fPlaneProps);
					} else if (props->prop_values[j] == DRM_PLANE_TYPE_CURSOR
							&& !fCursorPlaneId) {
						fCursorPlaneId = planes->planes[i];
						_DiscoverPlaneProps(planes->planes[i], fCursorPlaneProps);
					}
				}
				drmModeFreeProperty(prop);
			}
			drmModeFreeObjectProperties(props);
		}
		drmModeFreePlaneResources(planes);
	}

	_DiscoverCrtcProps(dev->crtc);
	_DiscoverConnProps(dev->conn);

	printf("DRM: primary plane=%u cursor plane=%u crtc=%u conn=%u VRR=%s\n",
		fPrimaryPlaneId, fCursorPlaneId, dev->crtc, dev->conn,
		fVRRSupported ? "yes" : "no");
}


void
DrmHWInterface::_DiscoverPlaneProps(uint32_t plane_id, PlaneProps& props)
{
	memset(&props, 0, sizeof(props));
	drmModeObjectPropertiesPtr oprops = drmModeObjectGetProperties(
		fFd, plane_id, DRM_MODE_OBJECT_PLANE);
	if (!oprops) return;
	for (uint32_t i = 0; i < oprops->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fFd, oprops->props[i]);
		if (!prop) continue;
		if      (strcmp(prop->name, "FB_ID")           == 0)
			props.fb_id           = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_ID")         == 0)
			props.crtc_id         = prop->prop_id;
		else if (strcmp(prop->name, "SRC_X")           == 0)
			props.src_x           = prop->prop_id;
		else if (strcmp(prop->name, "SRC_Y")           == 0)
			props.src_y           = prop->prop_id;
		else if (strcmp(prop->name, "SRC_W")           == 0)
			props.src_w           = prop->prop_id;
		else if (strcmp(prop->name, "SRC_H")           == 0)
			props.src_h           = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_X")          == 0)
			props.crtc_x          = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_Y")          == 0)
			props.crtc_y          = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_W")          == 0)
			props.crtc_w          = prop->prop_id;
		else if (strcmp(prop->name, "CRTC_H")          == 0)
			props.crtc_h          = prop->prop_id;
		else if (strcmp(prop->name, "FB_DAMAGE_CLIPS") == 0)
			props.fb_damage_clips = prop->prop_id;
		else if (strcmp(prop->name, "rotation")        == 0)
			props.rotation        = prop->prop_id;
		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(oprops);
}


void
DrmHWInterface::_DiscoverCrtcProps(uint32_t crtc_id)
{
	memset(&fCrtcProps, 0, sizeof(fCrtcProps));
	drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(
		fFd, crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!props) return;
	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fFd, props->props[i]);
		if (!prop) continue;
		if      (strcmp(prop->name, "ACTIVE")      == 0)
			fCrtcProps.active      = prop->prop_id;
		else if (strcmp(prop->name, "MODE_ID")     == 0)
			fCrtcProps.mode_id     = prop->prop_id;
		else if (strcmp(prop->name, "VRR_ENABLED") == 0) {
			fCrtcProps.vrr_enabled = prop->prop_id;
			fVRRSupported = true;
			fVRREnabled   = true;  // auto-enable when available
		}
		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);
}


void
DrmHWInterface::_DiscoverConnProps(uint32_t conn_id)
{
	memset(&fConnProps, 0, sizeof(fConnProps));
	drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(
		fFd, conn_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!props) return;
	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fFd, props->props[i]);
		if (!prop) continue;
		if      (strcmp(prop->name, "CRTC_ID") == 0)
			fConnProps.crtc_id = prop->prop_id;
		else if (strcmp(prop->name, "DPMS")    == 0)
			fConnProps.dpms    = prop->prop_id;
		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);
}


status_t
DrmHWInterface::_AtomicModeset(uint32_t fb_id, drmModeModeInfo* mode)
{
	struct modeset_dev* dev = get_dev();
	if (!dev || !fPrimaryPlaneId) return B_ERROR;

	// Create (or replace) the mode blob.
	if (fModeBlobId) {
		drmModeDestroyPropertyBlob(fFd, fModeBlobId);
		fModeBlobId = 0;
	}
	if (drmModeCreatePropertyBlob(fFd, mode, sizeof(*mode), &fModeBlobId) != 0)
		return B_ERROR;

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	if (!req) return B_NO_MEMORY;

	uint32_t w = (uint32_t)dev->width;
	uint32_t h = (uint32_t)dev->height;

	// Connector → CRTC binding
	drmModeAtomicAddProperty(req, dev->conn,         fConnProps.crtc_id,    dev->crtc);
	// CRTC: activate with mode
	drmModeAtomicAddProperty(req, dev->crtc,         fCrtcProps.active,     1);
	drmModeAtomicAddProperty(req, dev->crtc,         fCrtcProps.mode_id,    fModeBlobId);
	// VRR
	if (fVRREnabled && fCrtcProps.vrr_enabled)
		drmModeAtomicAddProperty(req, dev->crtc,     fCrtcProps.vrr_enabled, 1);
	// Primary plane: full-screen scanout
	// SRC_* are in 16.16 fixed point
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.crtc_id,   dev->crtc);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.fb_id,     fb_id);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.src_x,     0);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.src_y,     0);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.src_w,     w << 16);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.src_h,     h << 16);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.crtc_x,    0);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.crtc_y,    0);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.crtc_w,    w);
	drmModeAtomicAddProperty(req, fPrimaryPlaneId,   fPlaneProps.crtc_h,    h);

	int ret = drmModeAtomicCommit(fFd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
	drmModeAtomicFree(req);
	return ret == 0 ? B_OK : B_ERROR;
}


status_t
DrmHWInterface::_AtomicSetCursor(uint32_t fb_id, uint32_t crtc_id,
	int32 x, int32 y, uint32_t w, uint32_t h)
{
	if (!fCursorPlaneId) return B_UNSUPPORTED;

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	if (!req) return B_NO_MEMORY;

	// Setting fb_id=0 / crtc_id=0 disables the cursor plane.
	drmModeAtomicAddProperty(req, fCursorPlaneId,
		fCursorPlaneProps.crtc_id, crtc_id);
	drmModeAtomicAddProperty(req, fCursorPlaneId,
		fCursorPlaneProps.fb_id, fb_id);

	if (fb_id && crtc_id) {
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.crtc_x, (uint64_t)(int64_t)x);
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.crtc_y, (uint64_t)(int64_t)y);
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.crtc_w, w);
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.crtc_h, h);
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.src_x, 0);
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.src_y, 0);
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.src_w, (uint64_t)w << 16);
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.src_h, (uint64_t)h << 16);
	}

	int ret = drmModeAtomicCommit(fFd, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);
	drmModeAtomicFree(req);
	return ret == 0 ? B_OK : B_ERROR;
}


status_t
DrmHWInterface::_AtomicFlip(uint32_t fb_id, const BRect* dirty_rects,
	uint32_t nrects)
{
	if (!fPrimaryPlaneId) return B_ERROR;

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	if (!req) return B_NO_MEMORY;

	drmModeAtomicAddProperty(req, fPrimaryPlaneId, fPlaneProps.fb_id, fb_id);

	// Encode damage rectangles as a FB_DAMAGE_CLIPS blob when supported.
	// Skip for full-screen frames: passing a whole-screen rect as damage gives
	// the driver no scanout savings and costs two extra kernel calls per flip.
	bool fullScreen = (nrects == 1 && dirty_rects != NULL
		&& dirty_rects[0].left  <= 0
		&& dirty_rects[0].top   <= 0
		&& dirty_rects[0].right  >= (float)(fDisplayMode.virtual_width  - 1)
		&& dirty_rects[0].bottom >= (float)(fDisplayMode.virtual_height - 1));

	uint32_t damageBlob = 0;
	if (!fullScreen && fPlaneProps.fb_damage_clips && nrects > 0
			&& dirty_rects != NULL) {
		// Use stack storage for the common case (nrects == 1 with coalescing).
		struct drm_mode_rect stackRects[4];
		struct drm_mode_rect* rects = (nrects <= 4)
			? stackRects
			: new struct drm_mode_rect[nrects];
		for (uint32_t i = 0; i < nrects; i++) {
			rects[i].x1 = (int32_t)dirty_rects[i].left;
			rects[i].y1 = (int32_t)dirty_rects[i].top;
			rects[i].x2 = (int32_t)dirty_rects[i].right  + 1;
			rects[i].y2 = (int32_t)dirty_rects[i].bottom + 1;
		}
		if (drmModeCreatePropertyBlob(fFd, rects,
				nrects * sizeof(struct drm_mode_rect), &damageBlob) == 0)
			drmModeAtomicAddProperty(req, fPrimaryPlaneId,
				fPlaneProps.fb_damage_clips, damageBlob);
		if (nrects > 4)
			delete[] rects;
	}

	int ret = drmModeAtomicCommit(fFd, req,
		DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, this);
	drmModeAtomicFree(req);

	if (damageBlob)
		drmModeDestroyPropertyBlob(fFd, damageBlob);

	return ret == 0 ? B_OK : B_ERROR;
}
