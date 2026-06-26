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
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

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
	fWakeFd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
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
	pthread_mutex_init(&fDirtyMutex, NULL);

	// TODO move away from env vars
	const char* janusDrmFdStr = getenv("JANUS_DRM_FD");
	bool janusManaged = (janusDrmFdStr != NULL && janusDrmFdStr[0] != '\0');

	if (janusManaged) {
		printf("DrmHWInterface: janus-managed (JANUS_DRM_FD=%s)\n",
			janusDrmFdStr);
		_OnSessionEnable();
		if (!fSessionActive) {
			fprintf(stderr,
				"DrmHWInterface: _OnSessionEnable failed under janus\n");
			return;
		}
	} else {
		fSeat = libseat_open_seat(&seat_listener, this);
		if (!fSeat) {
			fprintf(stderr, "Failed to open libseat session\n");
			return;
		}
		printf("libseat opened (standalone), fSeat=%p, seat_fd=%d\n",
			(void*)fSeat, libseat_get_fd(fSeat));
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
	}

	fRunning = true;
	fEventThread = spawn_thread(_EventThreadEntry, "drm event thread",
		B_NORMAL_PRIORITY, this);
	if (fEventThread >= 0)
		resume_thread(fEventThread);
}


void
DrmHWInterface::OnSeatEnabled()
{
	if (LockExclusiveAccess()) {
		_OnSessionEnable();
		UnlockExclusiveAccess();
	}
}


void
DrmHWInterface::OnSeatDisabled()
{
	if (LockExclusiveAccess()) {
		_OnSessionDisable();
		UnlockExclusiveAccess();
	}
}


void
DrmHWInterface::_OnSessionEnable()
{
	printf("Session enabled\n");

	if (fInitialized) {
		if (fFd >= 0)
			drmSetMaster(fFd);

		fSessionActive = true;
		release_sem(fSessionSem);

		_RestoreDisplay();

		LockExclusiveAccess();
		Invalidate(BRect(0, 0, fDisplayMode.virtual_width - 1,
				fDisplayMode.virtual_height - 1));
		UnlockExclusiveAccess();
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

	// Save current CRTC state for restoration on exit
	struct modeset_dev *iter;
	for (iter = get_dev(); iter; iter = iter->next)
		iter->saved_crtc = drmModeGetCrtc(fFd, iter->crtc);

#ifdef HAVE_GBM
	fGbmDevice = gbm_create_device(fFd);
	fUseGbm = (fGbmDevice != NULL);
	if (!fUseGbm)
		fprintf(stderr, "GBM unavailable; OpenGL kit will be inactive\n");
#endif

	fFrontBuffer = new DrmBuffer(fFd, get_dev(), false);
	fBackBuffer = NULL;
	fWriteTarget = fFrontBuffer;
	fPageFlipEnabled = false;
	fPageFlipPending = false;

	pthread_mutex_lock(&fDirtyMutex);
	fAccumulatedDirty.MakeEmpty();
	fPreviousDirty.MakeEmpty();
	pthread_mutex_unlock(&fDirtyMutex);

	if (modeset_create_back_fb(fFd, get_dev()) == 0) {
		fBackBuffer      = new DrmBuffer(fFd, get_dev(), true);
		fWriteTarget     = fBackBuffer;
		fPageFlipEnabled = true;
	} else {
		fprintf(stderr,
			"[drm] back dumb-buffer creation failed; running "
			"single-buffered with tearing\n");
	}

	fRenderBuffer = new MallocBuffer(get_dev()->width, get_dev()->height);

	_ProbeAtomic();
	if (fAtomicSupported)
		_DiscoverProperties();

	// Configure crtc
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

	_ProbeCursor();

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

	struct modeset_dev* initDev = get_dev();
	fDisplayMode.virtual_width = initDev->width;
	fDisplayMode.virtual_height = initDev->height;
	fDisplayMode.space = B_RGB32;

	drmModeModeInfo& m = initDev->mode;
	fDisplayMode.timing.pixel_clock  = m.clock;
	fDisplayMode.timing.h_display    = m.hdisplay;
	fDisplayMode.timing.h_sync_start = m.hsync_start;
	fDisplayMode.timing.h_sync_end   = m.hsync_end;
	fDisplayMode.timing.h_total      = m.htotal;
	fDisplayMode.timing.v_display    = m.vdisplay;
	fDisplayMode.timing.v_sync_start = m.vsync_start;
	fDisplayMode.timing.v_sync_end   = m.vsync_end;
	fDisplayMode.timing.v_total      = m.vtotal;
	fDisplayMode.timing.flags = 0;
	if (m.flags & DRM_MODE_FLAG_PHSYNC)
		fDisplayMode.timing.flags |= B_POSITIVE_HSYNC;
	if (m.flags & DRM_MODE_FLAG_PVSYNC)
		fDisplayMode.timing.flags |= B_POSITIVE_VSYNC;
	if (m.flags & DRM_MODE_FLAG_INTERLACE)
		fDisplayMode.timing.flags |= B_TIMING_INTERLACED;
	fDisplayMode.h_display_start = 0;
	fDisplayMode.v_display_start = 0;
	fDisplayMode.flags = 0;

	fInitialized = true;
	fSessionActive = true;
	release_sem(fSessionSem);
}


void
DrmHWInterface::_OnSessionDisable()
{
	printf("Session disabled\n");

	// Drain before disabling session
	for (int i = 0; i < 4 && fPageFlipPending; i++) {
		struct pollfd pfd = { fFd, POLLIN, 0 };
		if (poll(&pfd, 1, 16) > 0 && (pfd.revents & POLLIN)) {
			drmEventContext evctx = {
				.version           = DRM_EVENT_CONTEXT_VERSION,
				.page_flip_handler = _PageFlipHandler,
			};
			drmHandleEvent(fFd, &evctx);
		} else
			break;
	}

	fSessionActive = false;
	fPageFlipPending = false;

	pthread_mutex_lock(&fDirtyMutex);
	fAccumulatedDirty.MakeEmpty();
	fPreviousDirty.MakeEmpty();
	pthread_mutex_unlock(&fDirtyMutex);

	if (fFd >= 0)
		drmDropMaster(fFd);

	if (fSeat != NULL)
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


int32
DrmHWInterface::_EventThreadEntry(void* data)
{
	static_cast<DrmHWInterface*>(data)->_EventThreadMain();
	return 0;
}


void
DrmHWInterface::_PageFlipHandler(int fd, unsigned int frame,
	unsigned int sec, unsigned int usec, void* data)
{
	DrmHWInterface* hw = static_cast<DrmHWInterface*>(data);
	hw->fPageFlipPending = false;

	std::swap(hw->fFrontBuffer, hw->fBackBuffer);
	hw->fWriteTarget = hw->fBackBuffer;

	if (hw->fRenderBuffer == NULL)
		return;

	// Build the dirty region: union of this frame's accumulated damage
	// with the previous frame's (covers double-buffer staleness). Then
	// rotate: previous <- accumulated, accumulated <- empty.
	BRegion toBlit;
	pthread_mutex_lock(&hw->fDirtyMutex);
	if (hw->fPreviousDirty.CountRects() > 0) {
		toBlit.Include(&hw->fPreviousDirty);
		toBlit.Include(&hw->fAccumulatedDirty);
	} else
		toBlit.Include(&hw->fAccumulatedDirty);
	hw->fPreviousDirty = hw->fAccumulatedDirty;
	hw->fAccumulatedDirty.MakeEmpty();
	pthread_mutex_unlock(&hw->fDirtyMutex);

	if (toBlit.CountRects() > 0) {
		bool locked = hw->LockExclusiveAccess();
		int32 count = toBlit.CountRects();
		for (int32 i = 0; i < count; i++)
			hw->_BlitRect(hw->fRenderBuffer, hw->fWriteTarget,
				toBlit.RectAt(i));
		if (locked)
			hw->UnlockExclusiveAccess();

		if (!hw->fHardwareCursorEnabled) {
			bool cursorLocked = hw->fFloatingOverlaysLock.Lock();
			IntRect cf = hw->_CursorFrame();
			if (cf.IsValid())
				hw->_BlendCursor(hw->fRenderBuffer,
					hw->fWriteTarget, cf);
			if (cursorLocked)
				hw->fFloatingOverlaysLock.Unlock();
		}
	}
}


void
DrmHWInterface::_EventThreadMain()
{
	while (fRunning) {
		int seat_fd = fSeat ? libseat_get_fd(fSeat) : -1;

		bool active = fSessionActive.load();

		struct pollfd pfds[4];
		int nfds = 0;
		int seat_idx = -1;
		int drm_idx  = -1;
		int udev_idx = -1;
		int wake_idx = -1;

		if (seat_fd >= 0) {
			pfds[nfds].fd      = seat_fd;
			pfds[nfds].events  = POLLIN;
			pfds[nfds].revents = 0;
			seat_idx = nfds++;
		}

		if (active && fFd >= 0) {
			pfds[nfds].fd      = fFd;
			pfds[nfds].events  = POLLIN;
			pfds[nfds].revents = 0;
			drm_idx = nfds++;
		}

		if (fUdevFd >= 0) {
			pfds[nfds].fd      = fUdevFd;
			pfds[nfds].events  = POLLIN;
			pfds[nfds].revents = 0;
			udev_idx = nfds++;
		}

		if (fWakeFd >= 0) {
			pfds[nfds].fd      = fWakeFd;
			pfds[nfds].events  = POLLIN;
			pfds[nfds].revents = 0;
			wake_idx = nfds++;
		}

		// Spin protection
		if (nfds == 0) {
			acquire_sem_etc(fSessionSem, 1, B_RELATIVE_TIMEOUT, 100000);
			continue;
		}

		// TODO this should be dynamic I think not 16ms hardcoded
		int ret = poll(pfds, nfds, 16);
		if (ret < 0) {
			if (errno == EBADF || errno == EINVAL) {
				acquire_sem_etc(fSessionSem, 1, B_RELATIVE_TIMEOUT, 100000);
				continue;
			}
			if (errno == EINTR)
				continue;
			snooze(10000);
			continue;
		}

		if (ret > 0) {
			if (seat_idx >= 0
					&& (pfds[seat_idx].revents & (POLLIN | POLLHUP))) {
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
			if (wake_idx >= 0 && (pfds[wake_idx].revents & POLLIN)) {
				uint64_t v;
				read(fWakeFd, &v, sizeof(v));
			}
			if (udev_idx >= 0 && (pfds[udev_idx].revents & POLLIN)) {
				_HandleHotplug();
			}
		}

		if (active && fPageFlipEnabled && !fPageFlipPending
				&& fBackBuffer != NULL
				&& fDpmsState == B_DPMS_ON) {
			pthread_mutex_lock(&fDirtyMutex);
			bool hasDirty = (fAccumulatedDirty.CountRects() > 0);
			pthread_mutex_unlock(&fDirtyMutex);

			if (hasDirty) {
				struct modeset_dev* dev = get_dev();
				int r;
				if (fAtomicSupported && fPrimaryPlaneId) {
				r = (_AtomicFlip(fWriteTarget->GetFbId(),
					NULL, 0) == B_OK) ? 0 : -1;
				} else {
					r = drmModePageFlip(fFd, dev->crtc,
						fWriteTarget->GetFbId(),
						DRM_MODE_PAGE_FLIP_EVENT, this);
				}
				if (r == 0)
					fPageFlipPending = true;
				else {
					pthread_mutex_lock(&fDirtyMutex);
					fAccumulatedDirty.MakeEmpty();
					fPreviousDirty.MakeEmpty();
					pthread_mutex_unlock(&fDirtyMutex);
				}
			}
		}
	}
}


DrmHWInterface::~DrmHWInterface()
{
	CALLED();

	fRunning = false;
	release_sem(fSessionSem);

	if (fWakeFd >= 0) {
		uint64_t v = 1;
		write(fWakeFd, &v, sizeof(v));
	}

	if (fEventThread >= 0) {
		status_t exitValue;
		wait_for_thread(fEventThread, &exitValue);
	}

	if (fWakeFd >= 0) {
		close(fWakeFd);
		fWakeFd = -1;
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

	pthread_mutex_destroy(&fDirtyMutex);
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

	if (!LockExclusiveAccess())
		return B_ERROR;

	drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
	if (!conn) { UnlockExclusiveAccess(); return B_ERROR; }

	drmModeModeInfo* found = NULL;

	float targetRefresh = 0;
	if (mode.timing.h_total > 0 && mode.timing.v_total > 0) {
		targetRefresh = float(mode.timing.pixel_clock * 1000)
			/ float(mode.timing.h_total * mode.timing.v_total);
	}

	if (targetRefresh > 0) {
		float bestDiff = 999;
		for (int i = 0; i < conn->count_modes; i++) {
			if (conn->modes[i].hdisplay != mode.virtual_width ||
			    conn->modes[i].vdisplay != mode.virtual_height)
				continue;
			float modeRefresh = float(conn->modes[i].clock * 1000)
				/ float(conn->modes[i].htotal * conn->modes[i].vtotal);
			float diff = fabsf(modeRefresh - targetRefresh);
			if (diff < bestDiff) {
				bestDiff = diff;
				found = &conn->modes[i];
			}
		}
	} else {
		for (int i = 0; i < conn->count_modes; i++) {
			if (conn->modes[i].hdisplay == mode.virtual_width &&
			    conn->modes[i].vdisplay == mode.virtual_height) {
				found = &conn->modes[i];
				break;
			}
		}
	}

	if (!found) {
		drmModeFreeConnector(conn);
		UnlockExclusiveAccess();
		return B_BAD_VALUE;
	}

	delete fFrontBuffer; fFrontBuffer = NULL;
	delete fBackBuffer;  fBackBuffer  = NULL;
	delete fRenderBuffer; fRenderBuffer = NULL;

	if (dev->fb) {
		drmModeRmFB(fFd, dev->fb);
		dev->fb = 0;
	}

	if (dev->back_fb) {
		drmModeRmFB(fFd, dev->back_fb);
		dev->back_fb = 0;
	}

	dev->width  = found->hdisplay;
	dev->height = found->vdisplay;
	memcpy(&dev->mode, found, sizeof(*found));
	drmModeFreeConnector(conn);

	if (modeset_create_fb(fFd, dev) != 0) {
		UnlockExclusiveAccess();
		return B_ERROR;
	}

	fFrontBuffer = new DrmBuffer(fFd, dev, false);
	fBackBuffer = NULL;
	fWriteTarget = fFrontBuffer;
	fPageFlipEnabled = false;
	fPageFlipPending = false;

	pthread_mutex_lock(&fDirtyMutex);
	fAccumulatedDirty.MakeEmpty();
	fPreviousDirty.MakeEmpty();
	pthread_mutex_unlock(&fDirtyMutex);

	if (modeset_create_back_fb(fFd, dev) == 0) {
		fBackBuffer = new DrmBuffer(fFd, dev, true);
		fWriteTarget = fBackBuffer;
		fPageFlipEnabled = true;
	} else {
		fprintf(stderr,
			"[drm] SetMode: back dumb-buffer creation failed; running "
			"single-buffered with tearing\n");
	}

	fRenderBuffer = new MallocBuffer(dev->width, dev->height);

	int ret;
	if (fAtomicSupported && fPrimaryPlaneId) {
		if (_AtomicModeset(dev->fb, &dev->mode) != B_OK) {
			UnlockExclusiveAccess();
			return B_ERROR;
		}
		ret = 0;
	} else {
		ret = drmModeSetCrtc(fFd, dev->crtc, dev->fb, 0, 0,
		                     &dev->conn, 1, &dev->mode);
		if (ret) {
			UnlockExclusiveAccess();
			return B_ERROR;
		}
	}

	fDisplayMode.virtual_width  = dev->width;
	fDisplayMode.virtual_height = dev->height;
	fDisplayMode.space = B_RGB32;

	drmModeModeInfo& m = dev->mode;
	fDisplayMode.timing.pixel_clock  = m.clock;
	fDisplayMode.timing.h_display    = m.hdisplay;
	fDisplayMode.timing.h_sync_start = m.hsync_start;
	fDisplayMode.timing.h_sync_end   = m.hsync_end;
	fDisplayMode.timing.h_total      = m.htotal;
	fDisplayMode.timing.v_display    = m.vdisplay;
	fDisplayMode.timing.v_sync_start = m.vsync_start;
	fDisplayMode.timing.v_sync_end   = m.vsync_end;
	fDisplayMode.timing.v_total      = m.vtotal;
	fDisplayMode.timing.flags = 0;
	if (m.flags & DRM_MODE_FLAG_PHSYNC)
		fDisplayMode.timing.flags |= B_POSITIVE_HSYNC;
	if (m.flags & DRM_MODE_FLAG_PVSYNC)
		fDisplayMode.timing.flags |= B_POSITIVE_VSYNC;
	if (m.flags & DRM_MODE_FLAG_INTERLACE)
		fDisplayMode.timing.flags |= B_TIMING_INTERLACED;
	fDisplayMode.h_display_start = 0;
	fDisplayMode.v_display_start = 0;
	fDisplayMode.flags = 0;

	_NotifyFrameBufferChanged();

	UnlockExclusiveAccess();
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

	// If a driver has no vblank let's store this info for next time.
	static std::atomic<bool> sVBlankUnsupported(false);
	if (sVBlankUnsupported.load(std::memory_order_relaxed)) {
		// TODO: seems reasonable but double check this
		snooze(16667);
		return B_OK;
	}

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

		if (errno == ENOTTY || errno == EINVAL
				|| errno == EOPNOTSUPP || errno == ENOSYS) {
			sVBlankUnsupported.store(true, std::memory_order_relaxed);
			snooze(16667);
			return B_OK;
		}
		return B_ERROR;
	}
}


status_t
DrmHWInterface::SetDPMSMode(uint32 state)
{
	struct modeset_dev* dev = get_dev();
	if (!dev || fFd < 0)
		return B_ERROR;

	int dpms;
	switch (state) {
		case B_DPMS_ON:      dpms = DRM_MODE_DPMS_ON;      break;
		case B_DPMS_STAND_BY: dpms = DRM_MODE_DPMS_STANDBY; break;
		case B_DPMS_SUSPEND: dpms = DRM_MODE_DPMS_SUSPEND;  break;
		case B_DPMS_OFF:     dpms = DRM_MODE_DPMS_OFF;     break;
		default: return B_BAD_VALUE;
	}

	if (fConnProps.dpms) {
		int ret = drmModeConnectorSetProperty(fFd, dev->conn,
			fConnProps.dpms, dpms);
		if (ret == 0) {
			fDpmsState = state;
			return B_OK;
		}
		return B_ERROR;
	}

	drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
	if (!conn)
		return B_ERROR;

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
	if (!fBacklight)
		return B_UNSUPPORTED;
	int max = (int)backlight_get_max_brightness(fBacklight);
	int val = (int)(brightness * max + 0.5f);
	return backlight_set_brightness(fBacklight, val) == 0 ? B_OK : B_ERROR;
}


status_t
DrmHWInterface::GetBrightness(float* brightness)
{
	if (!fBacklight || !brightness)
		return B_UNSUPPORTED;
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
	if (fRenderBuffer != NULL)
		return fRenderBuffer;
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
	if (w <= 0 || h <= 0)
		return;

	uint32 srcBpr = src->BytesPerRow(), dstBpr = dst->BytesPerRow();
	uint8* s = (uint8*)src->Bits() + y * srcBpr + x * 4;
	uint8* d = (uint8*)dst->Bits() + y * dstBpr + x * 4;
	int32  bytes = w * 4;

#if defined(__x86_64__) || defined(__i386__)
	static bool sAvx2 = __builtin_cpu_supports("avx2");
	if (sAvx2
			&& ((uintptr_t)d % 32) == 0
			&& (dstBpr % 32) == 0
			&& bytes >= 64) {
		_BlitRect_AVX2(s, d, srcBpr, dstBpr, bytes, h);
		return;
	}
#endif

	for (int32 row = 0; row < h; row++, s += srcBpr, d += dstBpr)
		memcpy(d, s, bytes);
}


#if defined(__x86_64__) || defined(__i386__)
__attribute__((target("avx2")))
void
DrmHWInterface::_BlitRect_AVX2(const uint8* s, uint8* d,
	uint32 srcBpr, uint32 dstBpr, int32 bytes, int32 rows)
{
	for (int32 row = 0; row < rows; row++, s += srcBpr, d += dstBpr) {
		int32 r = bytes;
		const uint8* sp = s;
		uint8* dp = d;
		while (r >= 64) {
			__m256i v0 = _mm256_loadu_si256((const __m256i*)sp);
			__m256i v1 = _mm256_loadu_si256((const __m256i*)(sp + 32));
			_mm256_stream_si256((__m256i*)dp,      v0);
			_mm256_stream_si256((__m256i*)(dp+32), v1);
			sp += 64; dp += 64; r -= 64;
		}
		if (r > 0)
			memcpy(dp, sp, r);
	}
	_mm_sfence();
}
#endif


void
DrmHWInterface::_BlendCursor(RenderingBuffer* srcBg, RenderingBuffer* dst,
	IntRect area) const
{
	if (srcBg == NULL || dst == NULL || !area.IsValid()
			|| fCursorAndDragBitmap == NULL || !fCursorVisible)
		return;

	IntRect cf = _CursorFrame();
	if (!cf.IsValid() || !area.Intersects(cf))
		return;

	area = area & IntRect(dst->Bounds());
	area = area & cf;
	if (!area.IsValid())
		return;

	const int32 left   = area.left;
	const int32 top    = area.top;
	const int32 right  = area.right;
	const int32 bottom = area.bottom;

	uint32 bgBPR = srcBg->BytesPerRow();
	uint32 dBPR  = dst->BytesPerRow();
	uint8* bg  = (uint8*)srcBg->Bits() + top * bgBPR + left * 4;
	uint8* dpx = (uint8*)dst->Bits()   + top * dBPR  + left * 4;

	uint8* crs    = (uint8*)fCursorAndDragBitmap->Bits();
	uint32 crsBPR = fCursorAndDragBitmap->BytesPerRow();
	crs += (top - (int32)floorf(cf.top))   * crsBPR
		 + (left - (int32)floorf(cf.left)) * 4;

	for (int32 y = top; y <= bottom; y++) {
		uint8* s = bg;
		uint8* d = dpx;
		uint8* c = crs;
		for (int32 x = left; x <= right; x++) {
			int a = 255 - c[3];
			d[0] = (uint8)(((s[0] * a + 255) >> 8) + c[0]);
			d[1] = (uint8)(((s[1] * a + 255) >> 8) + c[1]);
			d[2] = (uint8)(((s[2] * a + 255) >> 8) + c[2]);
			s += 4; d += 4; c += 4;
		}
		bg  += bgBPR;
		dpx += dBPR;
		crs += crsBPR;
	}
}


status_t
DrmHWInterface::CopyBackToFront(const BRect& frame)
{
	if (fFrontBuffer == NULL)
		return B_ERROR;

	if (fRenderBuffer == NULL)
		return HWInterface::CopyBackToFront(frame);

	if (fPageFlipEnabled) {
		pthread_mutex_lock(&fDirtyMutex);
		fAccumulatedDirty.Include(frame);
		pthread_mutex_unlock(&fDirtyMutex);

		// When no flip is in flight, blit immediately so the back
		// buffer is ready for the next page flip. When a flip IS
		// pending, the flip handler will blit the accumulated region
		// on completion.
		if (!fPageFlipPending) {
			_BlitRect(fRenderBuffer, fWriteTarget, frame);

			if (!fHardwareCursorEnabled) {
				bool overlaysLocked = fFloatingOverlaysLock.Lock();
				_BlendCursor(fRenderBuffer, fWriteTarget,
					IntRect(frame));
				if (overlaysLocked)
					fFloatingOverlaysLock.Unlock();
			}
		}

		if (fWakeFd >= 0) {
			uint64_t v = 1;
			write(fWakeFd, &v, sizeof(v));
		}
		return B_OK;
	}

	_BlitRect(fRenderBuffer, fWriteTarget, frame);

	if (!fHardwareCursorEnabled) {
		bool overlaysLocked = fFloatingOverlaysLock.Lock();
		_BlendCursor(fRenderBuffer, fWriteTarget, IntRect(frame));
		if (overlaysLocked)
			fFloatingOverlaysLock.Unlock();
	}

	return B_OK;
}


status_t
DrmHWInterface::CopyBackToFront(const BRegion& region)
{
	if (fFrontBuffer == NULL)
		return B_ERROR;

	if (fRenderBuffer == NULL)
		return HWInterface::CopyBackToFront(region);

	int32 count = region.CountRects();
	if (count == 0)
		return B_OK;

	if (fPageFlipEnabled) {
		pthread_mutex_lock(&fDirtyMutex);
		fAccumulatedDirty.Include(&region);
		pthread_mutex_unlock(&fDirtyMutex);

		if (!fPageFlipPending) {
			for (int32 i = 0; i < count; i++)
				_BlitRect(fRenderBuffer, fWriteTarget,
					region.RectAt(i));

			if (!fHardwareCursorEnabled) {
				bool overlaysLocked = fFloatingOverlaysLock.Lock();
				IntRect cf = _CursorFrame();
				if (cf.IsValid())
					_BlendCursor(fRenderBuffer,
						fWriteTarget, cf);
				if (overlaysLocked)
					fFloatingOverlaysLock.Unlock();
			}
		}

		if (fWakeFd >= 0) {
			uint64_t v = 1;
			write(fWakeFd, &v, sizeof(v));
		}
		return B_OK;
	}

	for (int32 i = 0; i < count; i++)
		_BlitRect(fRenderBuffer, fWriteTarget, region.RectAt(i));

	if (!fHardwareCursorEnabled) {
		bool overlaysLocked = fFloatingOverlaysLock.Lock();
		IntRect cf = _CursorFrame();
		if (cf.IsValid())
			_BlendCursor(fRenderBuffer, fWriteTarget, cf);
		if (overlaysLocked)
			fFloatingOverlaysLock.Unlock();
	}

	return B_OK;
}


void
DrmHWInterface::SetCursor(ServerCursor* cursor)
{
	if (fDragBitmap.IsSet())
		return;

	HWInterface::SetCursor(cursor);

	struct modeset_dev* dev = get_dev();
	if (!dev)
		return;

	if (cursor == NULL) {
		if (dev->cursor_ok) {
			drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
			fHardwareCursorEnabled = false;
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

	const BPoint hot = cursor->GetHotSpot();
	const int32  px  = (int32)fCursorLocation.x - (int32)hot.x;
	const int32  py  = (int32)fCursorLocation.y - (int32)hot.y;

	int ret;
	if (fAtomicSupported && fCursorPlaneId && dev->cursor_fb) {
		ret = (_AtomicSetCursor(dev->cursor_fb, dev->crtc,
			px, py, 64, 64) == B_OK) ? 0 : -1;
	} else {
		ret = drmModeSetCursor2(fFd, dev->crtc,
			dev->cursor_handle, 64, 64,
			(int32)hot.x, (int32)hot.y);
		if (ret == 0)
			drmModeMoveCursor(fFd, dev->crtc, px, py);
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
	if (fDragBitmap.IsSet())
		return;

	HWInterface::SetCursorVisible(visible);

	struct modeset_dev* dev = get_dev();
	if (!dev || !dev->cursor_ok)
		return;

	if (!visible) {
		if (fAtomicSupported && fCursorPlaneId)
			_AtomicSetCursor(0, 0, 0, 0, 0, 0);
		else
			drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
	} else {
		BPoint hot(0, 0);
		if (fCursor.IsSet())
			hot = fCursor->GetHotSpot();
		const int32 px = (int32)fCursorLocation.x - (int32)hot.x;
		const int32 py = (int32)fCursorLocation.y - (int32)hot.y;

		if (fAtomicSupported && fCursorPlaneId && dev->cursor_fb)
			_AtomicSetCursor(dev->cursor_fb, dev->crtc,
				px, py, dev->cursor_w, dev->cursor_h);
		else {
			drmModeSetCursor(fFd, dev->crtc, dev->cursor_handle,
				dev->cursor_w, dev->cursor_h);
			drmModeMoveCursor(fFd, dev->crtc, px, py);
		}
	}
}


void
DrmHWInterface::SetDragBitmap(const ServerBitmap* bitmap,
	const BPoint& offsetFromCursor)
{
	struct modeset_dev* dev = get_dev();

	if (bitmap != NULL) {
		// Disable the HW sprite before delegating to base so the
		// software composite path owns the drag visual. Use the
		// atomic path when supported — legacy drmModeSetCursor can
		// return success without clearing the plane FB on atomic
		// drivers (root cause of the "two cursors" symptom).
		if (dev && dev->cursor_ok && fHardwareCursorEnabled) {
			if (fAtomicSupported && fCursorPlaneId)
				_AtomicSetCursor(0, 0, 0, 0, 0, 0);
			else
				drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
		}
		fHardwareCursorEnabled = false;
		fCursorObscured = false;
		fCursorVisible = true;
		HWInterface::SetDragBitmap(bitmap, offsetFromCursor);
		if (fPageFlipEnabled && fWakeFd >= 0) {
			uint64_t v = 1;
			write(fWakeFd, &v, sizeof(v));
		}
	} else {
		HWInterface::SetDragBitmap(bitmap, offsetFromCursor);
		// Force a full sprite content re-arm. SetCursor always
		// re-uploads cursor_map + drmModeSetCursor2 / _AtomicSetCursor
		// regardless of pointer identity, so even if fCursor is the
		// same ServerCursor the sprite is refreshed.
		if (fCursor.IsSet())
			SetCursor(fCursor);
	}
}


void
DrmHWInterface::MoveCursorTo(float x, float y)
{
	BPoint hot(0, 0);
	if (fCursor.IsSet())
		hot = fCursor->GetHotSpot();

	const int32 oldPx = (int32)floorf(fCursorLocation.x - hot.x);
	const int32 oldPy = (int32)floorf(fCursorLocation.y - hot.y);

	HWInterface::MoveCursorTo(x, y);

	// When the HW sprite is off (software composite / drag), the base
	// MoveCursorTo already handled the cursor draw — no plane update.
	if (!fHardwareCursorEnabled)
		return;

	struct modeset_dev* dev = get_dev();
	if (!dev || !dev->cursor_ok)
		return;

	const int32 px = (int32)floorf(x - hot.x);
	const int32 py = (int32)floorf(y - hot.y);

	if (fAtomicSupported && fCursorPlaneId && dev->cursor_fb)
		_AtomicSetCursor(dev->cursor_fb, dev->crtc,
			px, py, dev->cursor_w, dev->cursor_h);
	else
		drmModeMoveCursor(fFd, dev->crtc, px, py);

	_PushCursorTrackDirty(oldPx, oldPy, px, py);
}


void
DrmHWInterface::_DrawCursor(IntRect area) const
{
	if (!fHardwareCursorEnabled)
		HWInterface::_DrawCursor(area);
}


void
DrmHWInterface::_PushCursorTrackDirty(int32 oldX, int32 oldY,
	int32 newX, int32 newY)
{
	struct modeset_dev* dev = get_dev();
	if (!dev || dev->cursor_w <= 0 || dev->cursor_h <= 0)
		return;

	BRegion track;
	track.Include(BRect(oldX, oldY,
		oldX + dev->cursor_w - 1, oldY + dev->cursor_h - 1));
	track.Include(BRect(newX, newY,
		newX + dev->cursor_w - 1, newY + dev->cursor_h - 1));

	if (track.CountRects() == 0)
		return;

	pthread_mutex_lock(&fDirtyMutex);
	fAccumulatedDirty.Include(&track);
	pthread_mutex_unlock(&fDirtyMutex);

	if (fPageFlipEnabled && fWakeFd >= 0) {
		uint64_t v = 1;
		write(fWakeFd, &v, sizeof(v));
	}
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
						modeset_remove_connector(fFd, conn->connector_id);
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


void
DrmHWInterface::_ProbeAtomic()
{
	fAtomicSupported = false;
	drmSetClientCap(fFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	fAtomicSupported = (drmSetClientCap(fFd, DRM_CLIENT_CAP_ATOMIC, 1) == 0);
	printf("DRM: atomic modesetting %s\n",
		fAtomicSupported ? "supported" : "not supported (legacy fallback)");
}


void
DrmHWInterface::_ProbeCursor()
{
	struct modeset_dev* dev = get_dev();
	if (dev == NULL || dev->cursor_handle == 0)
		return;

	int r = drmModeSetCursor(fFd, dev->crtc,
		dev->cursor_handle, dev->cursor_w, dev->cursor_h);
	if (r == 0) {
		fHardwareCursorEnabled = true;
		drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
		return;
	}

	if (fAtomicSupported && fCursorPlaneId && dev->cursor_fb) {
		r = (_AtomicSetCursor(dev->cursor_fb, dev->crtc,
			0, 0, dev->cursor_w, dev->cursor_h) == B_OK) ? 0 : -1;
		if (r == 0) {
			fHardwareCursorEnabled = true;
			_AtomicSetCursor(0, 0, 0, 0, 0, 0);
			return;
		}
		fprintf(stderr,
			"DRM: hardware cursor not supported (legacy: %s, atomic: "
			"failed), using software cursor\n", strerror(-r));
	} else {
		fprintf(stderr,
			"DRM: hardware cursor not supported (%s), using software "
			"cursor\n", strerror(-r));
	}
	fHardwareCursorEnabled = false;
}


int
DrmHWInterface::_CrtcIndex(uint32_t crtc_id)
{
	drmModeRes* res = drmModeGetResources(fFd);
	if (!res)
		return 0;
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
	if (!dev)
		return;

	int crtcIdx = _CrtcIndex(dev->crtc);
	drmModePlaneResPtr planes = drmModeGetPlaneResources(fFd);
	if (planes) {
		for (uint32_t i = 0; i < planes->count_planes; i++) {
			drmModePlanePtr plane = drmModeGetPlane(fFd, planes->planes[i]);
			if (!plane)
				continue;
			bool forOurCrtc = (plane->possible_crtcs & (1u << crtcIdx)) != 0;
			drmModeFreePlane(plane);
			if (!forOurCrtc)
				continue;

			drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(
				fFd, planes->planes[i], DRM_MODE_OBJECT_PLANE);
			if (!props)
				continue;
			for (uint32_t j = 0; j < props->count_props; j++) {
				drmModePropertyPtr prop = drmModeGetProperty(fFd, props->props[j]);
				if (!prop)
					continue;
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
	if (!oprops)
		return;
	for (uint32_t i = 0; i < oprops->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fFd, oprops->props[i]);
		if (!prop)
			continue;
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
	if (!props)
		return;
	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fFd, props->props[i]);
		if (!prop)
			continue;
		if      (strcmp(prop->name, "ACTIVE")      == 0)
			fCrtcProps.active      = prop->prop_id;
		else if (strcmp(prop->name, "MODE_ID")     == 0)
			fCrtcProps.mode_id     = prop->prop_id;
		else if (strcmp(prop->name, "VRR_ENABLED") == 0) {
			fCrtcProps.vrr_enabled = prop->prop_id;
			fVRRSupported = true;
			// TODO wire to DesktopSettings
			fVRREnabled   = false;
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
	if (!props)
		return;
	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fFd, props->props[i]);
		if (!prop)
			continue;
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
	if (!dev || !fPrimaryPlaneId)
		return B_ERROR;

	// Drain flipping events
	for (int i = 0; i < 4 && fPageFlipPending; i++) {
		struct pollfd pfd = { fFd, POLLIN, 0 };
		if (poll(&pfd, 1, 16) > 0 && (pfd.revents & POLLIN)) {
			drmEventContext evctx = {
				.version           = DRM_EVENT_CONTEXT_VERSION,
				.page_flip_handler = _PageFlipHandler,
			};
			drmHandleEvent(fFd, &evctx);
		} else
			break;
	}

	if (fModeBlobId) {
		drmModeDestroyPropertyBlob(fFd, fModeBlobId);
		fModeBlobId = 0;
	}
	if (drmModeCreatePropertyBlob(fFd, mode, sizeof(*mode), &fModeBlobId) != 0)
		return B_ERROR;

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	if (!req)
		return B_NO_MEMORY;

	uint32_t w = (uint32_t)dev->width;
	uint32_t h = (uint32_t)dev->height;

	drmModeAtomicAddProperty(req, dev->conn,         fConnProps.crtc_id,    dev->crtc);

	drmModeAtomicAddProperty(req, dev->crtc,         fCrtcProps.active,     1);
	drmModeAtomicAddProperty(req, dev->crtc,         fCrtcProps.mode_id,    fModeBlobId);

	if (fVRREnabled && fCrtcProps.vrr_enabled)
		drmModeAtomicAddProperty(req, dev->crtc,     fCrtcProps.vrr_enabled, 1);

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
	if (!fCursorPlaneId)
		return B_UNSUPPORTED;

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	if (!req)
		return B_NO_MEMORY;

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
DrmHWInterface::_AtomicFlip(uint32_t fb_id, const BRect* /*dirty_rects*/,
	uint32_t /*nrects*/)
{
	// TODO FB_DAMAGE_CLIPS comes back after dirty tracking + BRegion is stable.
	if (!fPrimaryPlaneId)
		return B_ERROR;

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	if (!req)
		return B_NO_MEMORY;

	struct modeset_dev* dev = get_dev();
	if (dev) {
		drmModeAtomicAddProperty(req, fPrimaryPlaneId,
			fPlaneProps.crtc_id, dev->crtc);
	}
	drmModeAtomicAddProperty(req, fPrimaryPlaneId, fPlaneProps.fb_id, fb_id);

	int ret = drmModeAtomicCommit(fFd, req,
		DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, this);
	drmModeAtomicFree(req);

	return ret == 0 ? B_OK : B_ERROR;
}
