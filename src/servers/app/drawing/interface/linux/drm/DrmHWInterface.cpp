/*
 * Copyright 2021-2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmHWInterface.h"

#include "DrmBuffer.h"
#include "LibEvdevEventStream.h"
#include "PanelOrientationTransform.h"

#include <algorithm>
#include <new>
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
	fResizeThread(-1),
	fResizeBusy(false),
	fResizePending(false),
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
	fNeedsFlip(false),
	fWakeFd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
	fDpmsState(B_DPMS_ON),
	fBacklight(NULL),
	fAtomicSupported(false),
	fPrimaryPlaneId(0),
	fCursorPlaneId(0),
	fCursorUsesAtomic(false),
	fModeBlobId(0),
	fPlaneProps{},
	fCursorPlaneProps{},
	fCrtcProps{},
	fConnProps{},
	fVRRSupported(false),
	fVRREnabled(false),
	fPanelOrientation(PANEL_ORIENTATION_NORMAL),
	fPanelReflection(B_PANEL_REFLECTION_NONE)
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
		fSessionActive = true;
		release_sem(fSessionSem);

		_RestoreDisplay();

		// Re-arm the plane _OnSessionDisable() cleared, from last known state.
		if (fHardwareCursorEnabled && fCursor.IsSet())
			SetCursor(fCursor);

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

	// Needed before any buffer is sized below, since fRenderBuffer must be
	// allocated at logical (post-swap) dimensions, not the panel's physical
	// mode.
	_DiscoverPanelOrientation();
	_DiscoverPanelReflection();

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
	fNeedsFlip = false;
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

	uint32_t logW, logH;
	_ApplyOrientationSwap(get_dev()->width, get_dev()->height, logW, logH);
	fRenderBuffer = new MallocBuffer(logW, logH);

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
	_FillModeInfo(fDisplayMode, initDev->mode);
	uint32_t initLogW, initLogH;
	_ApplyOrientationSwap(initDev->width, initDev->height, initLogW, initLogH);
	fDisplayMode.virtual_width = initLogW;
	fDisplayMode.virtual_height = initLogH;

	fInitialized = true;
	fSessionActive = true;
	release_sem(fSessionSem);
}


void
DrmHWInterface::_DrainPendingFlip()
{
	// A flip already queued targets the buffer we're about to tear down.
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
}


void
DrmHWInterface::_OnSessionDisable()
{
	printf("Session disabled\n");

	_DrainPendingFlip();

	fSessionActive = false;
	fPageFlipPending = false;

	pthread_mutex_lock(&fDirtyMutex);
	fAccumulatedDirty.MakeEmpty();
	fPreviousDirty.MakeEmpty();
	fNeedsFlip = false;
	pthread_mutex_unlock(&fDirtyMutex);

	// Clear while still DRM master, or the next VT/session owner scans
	// out our stale sprite.
	_DisableHardwareCursor();

	if (fFd >= 0 && drmDropMaster(fFd) != 0) {
		static bool sReported = false;
		if (!sReported) {
			fprintf(stderr, "[drm] drmDropMaster failed (%s)\n",
				strerror(errno));
			sReported = true;
		}
	}

	if (fSeat != NULL)
		libseat_disable_seat(fSeat);
}


void
DrmHWInterface::_RestoreDisplay()
{
	if (fFd < 0) {
		fprintf(stderr, "[drm] _RestoreDisplay: called with fFd < 0\n");
		return;
	}

	struct modeset_dev *iter;
	for (iter = get_dev(); iter; iter = iter->next) {
		// Not iter->fb: stale after any flip, zeroed by resize.
		// fFrontBuffer is what the CRTC was actually scanning out.
		uint32_t fb = fFrontBuffer != NULL ? fFrontBuffer->GetFbId() : iter->fb;

		if (fAtomicSupported && fPrimaryPlaneId) {
			status_t r = _AtomicModeset(fb, &iter->mode);
			if (r != B_OK)
				fprintf(stderr, "[drm] _RestoreDisplay: atomic modeset "
					"failed for connector %u fb=%u: status=%#x errno=%s\n",
					iter->conn, fb, (unsigned)r, strerror(errno));
		} else if (drmModeSetCrtc(fFd, iter->crtc, fb, 0, 0,
				&iter->conn, 1, &iter->mode) != 0)
			fprintf(stderr, "[drm] _RestoreDisplay: drmModeSetCrtc "
				"failed for connector %u fb=%u: %s\n", iter->conn,
				fb, strerror(errno));
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

	// Swap before clearing the flag, or CopyBackToFront() writes into the
	// buffer the CRTC just started scanning out.
	std::swap(hw->fFrontBuffer, hw->fBackBuffer);
	hw->fWriteTarget = hw->fBackBuffer;
	hw->fPageFlipPending = false;

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
	// Damage copied while this flip was in flight reached the buffer that
	// just left the screen; ask for a follow-up flip.
	hw->fNeedsFlip = (hw->fPreviousDirty.CountRects() > 0);
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
			bool hasDirty = (fAccumulatedDirty.CountRects() > 0)
				|| fNeedsFlip;
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
				if (r == 0) {
					fPageFlipPending = true;
					pthread_mutex_lock(&fDirtyMutex);
					fNeedsFlip = false;
					pthread_mutex_unlock(&fDirtyMutex);
				} else {
					// EBUSY is routine on real hardware. Keep the
					// damage and retry; dropping it here leaves that
					// region alternating between the two buffers.
					// Do not reclaim the DRM master on EACCES: losing
					// it means another app_server is taking over.
					static bool sReported = false;
					if (!sReported) {
						fprintf(stderr, "[drm] page flip failed "
							"(%s); retrying\n", strerror(errno));
						sReported = true;
					}
				}
			}
		}
	}
}


DrmHWInterface::~DrmHWInterface()
{
	CALLED();

	// A resize in flight would tear down buffers under the teardown below.
	if (fResizeThread >= 0) {
		status_t exitValue;
		wait_for_thread(fResizeThread, &exitValue);
	}

	// Clear while we can still be DRM master, or a leftover armed cursor
	// keeps scanning out after we're gone (janus-shared fd case).
	_DisableHardwareCursor();

	// Janus-shared fd is a dup() of janus's open — dropping master here
	// would revoke janus's too, and the non-root successor can't reacquire.
	if (fFd >= 0 && fSessionActive.load()
			&& getenv("JANUS_DRM_FD") == NULL)
		drmDropMaster(fFd);

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

	if (fFd >= 0)
		_DrainPendingFlip();

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
	// Greeter pre-auth path only: input_server isn't up yet, and it
	// wouldn't share vos_login's session anyway.
	if (getenv("APP_SERVER_EMBED_INPUT") == NULL)
		return NULL;
	uint32 w = fDisplayMode.virtual_width  > 0 ? fDisplayMode.virtual_width  : 1920;
	uint32 h = fDisplayMode.virtual_height > 0 ? fDisplayMode.virtual_height : 1080;
	LibEvdevEventStream* s = new (std::nothrow) LibEvdevEventStream(w, h,
		fSeat, fPanelOrientation, fPanelReflection);
	if (s == NULL)
		return NULL;
	if (!s->IsValid()) {
		delete s;
		return NULL;
	}
	return s;
}


status_t
DrmHWInterface::Shutdown()
{
	CALLED();
	if (fFd >= 0 && fSessionActive.load()
			&& getenv("JANUS_DRM_FD") == NULL) {
		drmDropMaster(fFd);
		fSessionActive = false;
	}
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

	// mode.virtual_width/height is the caller's logical (post-rotation)
	// request; the connector only ever advertises physical panel modes.
	uint32_t physW, physH;
	_ApplyOrientationSwap(mode.virtual_width, mode.virtual_height,
		physW, physH);

	float targetRefresh = 0;
	if (mode.timing.h_total > 0 && mode.timing.v_total > 0) {
		targetRefresh = float(mode.timing.pixel_clock * 1000)
			/ float(mode.timing.h_total * mode.timing.v_total);
	}

	if (targetRefresh > 0) {
		float bestDiff = 999;
		for (int i = 0; i < conn->count_modes; i++) {
			if (conn->modes[i].hdisplay != physW ||
			    conn->modes[i].vdisplay != physH)
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
			if (conn->modes[i].hdisplay == physW &&
			    conn->modes[i].vdisplay == physH) {
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

	_DrainPendingFlip();

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
	fNeedsFlip = false;
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

	uint32_t logW, logH;
	_ApplyOrientationSwap(dev->width, dev->height, logW, logH);
	fRenderBuffer = new MallocBuffer(logW, logH);

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

	_FillModeInfo(fDisplayMode, dev->mode);
	fDisplayMode.virtual_width  = logW;
	fDisplayMode.virtual_height = logH;

	// A shrink can leave the last cursor position outside the new CRTC;
	// clamp before re-arming the hardware plane at the old coordinates.
	// fCursorLocation is logical, so clamp against the logical mode, not
	// dev->width/height (physical). Guarded on fCursor alone: SetCursor()
	// now decides hardware vs. software, so it must always run to flip
	// the sprite back on after a return from a rotated orientation.
	if (fCursor.IsSet()) {
		if (fCursorLocation.x > fDisplayMode.virtual_width - 1)
			fCursorLocation.x = fDisplayMode.virtual_width - 1;
		if (fCursorLocation.y > fDisplayMode.virtual_height - 1)
			fCursorLocation.y = fDisplayMode.virtual_height - 1;
		if (fCursorLocation.x < 0)
			fCursorLocation.x = 0;
		if (fCursorLocation.y < 0)
			fCursorLocation.y = 0;
		SetCursor(fCursor);
	}

	_NotifyFrameBufferChanged();

	UnlockExclusiveAccess();
	return B_OK;
}


void
DrmHWInterface::_FillModeInfo(display_mode& mode, const drmModeModeInfo& m)
{
	mode.space = B_RGB32;
	mode.timing.pixel_clock  = m.clock;
	mode.timing.h_display    = m.hdisplay;
	mode.timing.h_sync_start = m.hsync_start;
	mode.timing.h_sync_end   = m.hsync_end;
	mode.timing.h_total      = m.htotal;
	mode.timing.v_display    = m.vdisplay;
	mode.timing.v_sync_start = m.vsync_start;
	mode.timing.v_sync_end   = m.vsync_end;
	mode.timing.v_total      = m.vtotal;
	mode.timing.flags = 0;
	if (m.flags & DRM_MODE_FLAG_PHSYNC)
		mode.timing.flags |= B_POSITIVE_HSYNC;
	if (m.flags & DRM_MODE_FLAG_PVSYNC)
		mode.timing.flags |= B_POSITIVE_VSYNC;
	if (m.flags & DRM_MODE_FLAG_INTERLACE)
		mode.timing.flags |= B_TIMING_INTERLACED;
	mode.h_display_start = 0;
	mode.v_display_start = 0;
	mode.flags = 0;
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

	struct modeset_dev* dev = get_dev();
	drmModeConnector* conn = (dev != NULL && fFd >= 0)
		? drmModeGetConnector(fFd, dev->conn) : NULL;
	const drmModeModeInfo* picked
		= conn != NULL ? modeset_pick_mode(fFd, conn) : NULL;

	if (picked == NULL) {
		if (conn != NULL)
			drmModeFreeConnector(conn);
		*mode = fDisplayMode;
		return B_OK;
	}

	drmModeModeInfo m = *picked;
	drmModeFreeConnector(conn);

	_FillModeInfo(*mode, m);
	uint32_t logW, logH;
	_ApplyOrientationSwap(m.hdisplay, m.vdisplay, logW, logH);
	mode->virtual_width  = logW;
	mode->virtual_height = logH;
	return B_OK;
}


status_t
DrmHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	CALLED();

	if (info == NULL)
		return B_BAD_VALUE;
	if (fFd < 0)
		return B_NO_INIT;

	memset(info, 0, sizeof(*info));
	info->version = B_ACCELERANT_VERSION;

	// The driver name is the closest thing DRM has to a chipset: there is
	// no accelerant here to ask.
	drmVersionPtr version = drmGetVersion(fFd);
	if (version != NULL) {
		if (version->name != NULL)
			strlcpy(info->chipset, version->name, sizeof(info->chipset));
		if (version->desc != NULL)
			strlcpy(info->name, version->desc, sizeof(info->name));
		drmFreeVersion(version);
	}

	// A driver description is generic ("AMD GPU"), so prefer the product
	// name hwdb resolves for the card's PCI parent when there is one.
	struct stat st;
	if (fUdev != NULL && fstat(fFd, &st) == 0) {
		struct udev_device* dev
			= udev_device_new_from_devnum(fUdev, 'c', st.st_rdev);
		if (dev != NULL) {
			struct udev_device* pci
				= udev_device_get_parent_with_subsystem_devtype(dev, "pci",
					NULL);
			const char* model = pci != NULL
				? udev_device_get_property_value(pci,
					"ID_MODEL_FROM_DATABASE")
				: NULL;
			if (model != NULL && model[0] != '\0')
				strlcpy(info->name, model, sizeof(info->name));
			// pci is owned by dev; unreferencing dev covers both.
			udev_device_unref(dev);
		}
	}

	if (info->name[0] == '\0' && info->chipset[0] == '\0')
		return B_UNSUPPORTED;

	return B_OK;
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
		uint32_t logW, logH;
		_ApplyOrientationSwap(m.hdisplay, m.vdisplay, logW, logH);
		dm.virtual_width  = logW;
		dm.virtual_height = logH;
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


namespace {

// The one place the rotation and reflection direction convention lives.
// Logical (lx, ly) is a pixel index into a logicalW x logicalH buffer; the
// result is the corresponding pixel index into the (swapped, for 90/270)
// physical buffer. Cross-checked against libweston/backend-drm (modes.c's
// get_panel_orientation() + shared/matrix.c's weston_matrix_init_transform()):
// LEFT_UP is a 90 degree CCW content rotation, RIGHT_UP a 270 degree CCW
// (= 90 CW) rotation. If this ships mirrored, flip the two cases below.
// Reflection is a pre-step in logical space, applied before rotation, with
// no dimension swap; composition order matches weston_matrix_init_transform's
// Sc . T3 . R . T2 . S . T1 (flip S runs before rotate R).
inline void
rotate_point(PanelOrientation orientation, int32 lx, int32 ly,
	int32 logicalW, int32 logicalH, int32& px, int32& py,
	int32 reflection = B_PANEL_REFLECTION_NONE)
{
	int32 rx = (reflection & B_PANEL_REFLECTION_X) != 0
		? logicalW - 1 - lx : lx;
	int32 ry = (reflection & B_PANEL_REFLECTION_Y) != 0
		? logicalH - 1 - ly : ly;

	switch (orientation) {
		case PANEL_ORIENTATION_UPSIDE_DOWN:
			px = logicalW - 1 - rx;
			py = logicalH - 1 - ry;
			break;
		case PANEL_ORIENTATION_LEFT_UP:
			px = ry;
			py = logicalW - 1 - rx;
			break;
		case PANEL_ORIENTATION_RIGHT_UP:
			px = logicalH - 1 - ry;
			py = rx;
			break;
		default:
			px = rx;
			py = ry;
			break;
	}
}

}	// namespace


void
DrmHWInterface::_BlitRect(RenderingBuffer* src, RenderingBuffer* dst,
	const BRect& frame)
{
	int32 x = (int32)frame.left,  y = (int32)frame.top;
	int32 w = (int32)(frame.right - frame.left + 1);
	int32 h = (int32)(frame.bottom - frame.top + 1);

	int32 srcW = (int32)src->Width(), srcH = (int32)src->Height();
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > srcW) w = srcW - x;
	if (y + h > srcH) h = srcH - y;
	if (w <= 0 || h <= 0)
		return;

	uint32 srcBpr = src->BytesPerRow(), dstBpr = dst->BytesPerRow();
	uint8* srcBase = (uint8*)src->Bits();
	uint8* dstBase = (uint8*)dst->Bits();

	if (fPanelReflection == B_PANEL_REFLECTION_NONE
			&& (fPanelOrientation == PANEL_ORIENTATION_LEFT_UP
				|| fPanelOrientation == PANEL_ORIENTATION_RIGHT_UP)) {
		// Axis-swapped: no long contiguous run on both sides at once, so
		// AVX2 streaming stores don't apply here. Scalar per-pixel.
		if ((int32)dst->Width() != srcH || (int32)dst->Height() != srcW)
			return;
		for (int32 sy = y; sy < y + h; sy++) {
			const uint8* srow = srcBase + sy * srcBpr + x * 4;
			for (int32 sx = x; sx < x + w; sx++, srow += 4) {
				int32 px, py;
				rotate_point(fPanelOrientation, sx, sy, srcW, srcH, px, py);
				memcpy(dstBase + py * dstBpr + px * 4, srow, 4);
			}
		}
		return;
	}

	if (fPanelReflection == B_PANEL_REFLECTION_NONE
			&& fPanelOrientation == PANEL_ORIENTATION_UPSIDE_DOWN) {
		// No axis swap for 180, so each row stays contiguous and only
		// runs backwards. rotate_point() still places its start, to keep
		// the direction convention in one place.
		if ((int32)dst->Width() != srcW || (int32)dst->Height() != srcH)
			return;
		for (int32 sy = y; sy < y + h; sy++) {
			const uint8* srow = srcBase + sy * srcBpr + x * 4;
			int32 dx, dy;
			rotate_point(fPanelOrientation, x, sy, srcW, srcH, dx, dy);
			uint8* drow = dstBase + dy * dstBpr + dx * 4;
			for (int32 sx = 0; sx < w; sx++, srow += 4, drow -= 4)
				memcpy(drow, srow, 4);
		}
		return;
	}

	if (fPanelReflection != B_PANEL_REFLECTION_NONE) {
		// Reflected (with or without rotation): neither fast path above
		// applies once a flip is involved, since a flipped row is neither
		// a straight run nor a simple reversed run. Per-pixel via the same
		// rotate_point() convention, axis-swapped like the LEFT_UP/RIGHT_UP
		// fast path above whenever the orientation itself swaps axes.
		bool swapped = fPanelOrientation == PANEL_ORIENTATION_LEFT_UP
			|| fPanelOrientation == PANEL_ORIENTATION_RIGHT_UP;
		int32 expectW = swapped ? srcH : srcW;
		int32 expectH = swapped ? srcW : srcH;
		if ((int32)dst->Width() != expectW || (int32)dst->Height() != expectH)
			return;
		for (int32 sy = y; sy < y + h; sy++) {
			const uint8* srow = srcBase + sy * srcBpr + x * 4;
			for (int32 sx = x; sx < x + w; sx++, srow += 4) {
				int32 px, py;
				rotate_point(fPanelOrientation, sx, sy, srcW, srcH, px, py,
					fPanelReflection);
				memcpy(dstBase + py * dstBpr + px * 4, srow, 4);
			}
		}
		return;
	}

	int32 bufW = (int32)dst->Width(), bufH = (int32)dst->Height();
	if (x + w > bufW) w = bufW - x;
	if (y + h > bufH) h = bufH - y;
	if (w <= 0 || h <= 0)
		return;

	uint8* s = srcBase + y * srcBpr + x * 4;
	uint8* d = dstBase + y * dstBpr + x * 4;
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
			|| fCursorAndDragBitmap == NULL
			|| fCursorAndDragBitmap->Bits() == NULL || !fCursorVisible)
		return;

	IntRect cf = _CursorFrame();
	if (!cf.IsValid() || !area.Intersects(cf))
		return;

	// srcBg and the cursor bitmap are logical; dst is the physical
	// scanout, so clip against the logical bounds and rotate per pixel.
	area = area & IntRect(srcBg->Bounds());
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
	uint8* dstBase = (uint8*)dst->Bits();
	uint8* dpx = dstBase + top * dBPR + left * 4;

	uint8* crs    = (uint8*)fCursorAndDragBitmap->Bits();
	uint32 crsBPR = fCursorAndDragBitmap->BytesPerRow();
	crs += (top - (int32)floorf(cf.top))   * crsBPR
		 + (left - (int32)floorf(cf.left)) * 4;

	if (fPanelOrientation == PANEL_ORIENTATION_NORMAL
			&& fPanelReflection == B_PANEL_REFLECTION_NONE) {
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
		return;
	}

	const int32 logicalW = fDisplayMode.virtual_width;
	const int32 logicalH = fDisplayMode.virtual_height;

	for (int32 y = top; y <= bottom; y++) {
		uint8* s = bg;
		uint8* c = crs;
		for (int32 x = left; x <= right; x++) {
			int32 px, py;
			rotate_point(fPanelOrientation, x, y, logicalW, logicalH,
				px, py, fPanelReflection);
			uint8* d = dstBase + py * dBPR + px * 4;
			int a = 255 - c[3];
			d[0] = (uint8)(((s[0] * a + 255) >> 8) + c[0]);
			d[1] = (uint8)(((s[1] * a + 255) >> 8) + c[1]);
			d[2] = (uint8)(((s[2] * a + 255) >> 8) + c[2]);
			s += 4; c += 4;
		}
		bg  += bgBPR;
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
DrmHWInterface::_DisableHardwareCursor()
{
	if (fFd < 0)
		return;

	struct modeset_dev* dev = get_dev();
	if (dev == NULL)
		return;

	// Legacy ioctl can return success without clearing the plane FB on
	// atomic-capable drivers, so branch the same way the plane was armed.
	if (fCursorUsesAtomic)
		_AtomicSetCursor(0, 0, 0, 0, 0, 0);
	else
		drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
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
		if (dev->cursor_ok)
			_DisableHardwareCursor();
		fHardwareCursorEnabled = false;
		return;
	}

	// A cursor bigger than the BO (large-font UI scaling; the amdgpu
	// cursor plane itself goes up to 256x256, but our allocation is
	// fixed at 64x64, see modeset_create_cursor_fb()) cannot be cropped
	// into the sprite without silently chopping it, so it is declined
	// the same way an unusable plane is: fall back to software.
	const int32 cw = (int32)cursor->Bounds().IntegerWidth() + 1;
	const int32 ch = (int32)cursor->Bounds().IntegerHeight() + 1;
	const bool oversized = cw > (int32)dev->cursor_w
		|| ch > (int32)dev->cursor_h;

	if (!_HardwareCursorUsable() || oversized) {
		if (oversized) {
			static bool sSaidOversized = false;
			if (!sSaidOversized) {
				fprintf(stderr, "DRM: cursor %" B_PRId32 "x%" B_PRId32
					" exceeds %ux%u hardware sprite, using software "
					"cursor\n", cw, ch, dev->cursor_w, dev->cursor_h);
				sSaidOversized = true;
			}
		}
		// Arm a fully transparent sprite instead of a null one: on a
		// virtualized host that was showing our sprite as its own window
		// pointer, disabling it outright makes the host draw its default
		// pointer on top of the one _BlendCursor() draws.
		if (dev->cursor_handle != 0) {
			static bool sSaidSoftware = false;
			if (!sSaidSoftware) {
				fprintf(stderr, "DRM: cursor drawn in software, sprite "
					"armed transparent\n");
				sSaidSoftware = true;
			}
			memset(dev->cursor_map, 0, dev->cursor_size);
			drmModeSetCursor(fFd, dev->crtc, dev->cursor_handle,
				dev->cursor_w, dev->cursor_h);
		} else {
			_DisableHardwareCursor();
		}
		fHardwareCursorEnabled = false;
		return;
	}

	memset(dev->cursor_map, 0, dev->cursor_size);

	// An empty or bogus bitmap would leave the sprite armed but fully
	// transparent, which looks exactly like a cursor the host refuses to
	// draw: it still tracks the pointer, invisibly.
	if (cw <= 0 || ch <= 0 || cursor->Bits() == NULL) {
		fprintf(stderr, "DRM: cursor bitmap unusable (%" B_PRId32 "x%"
			B_PRId32 ", bits=%p), leaving sprite armed transparent\n",
			cw, ch, cursor->Bits());
		// The memset above already blanked the sprite. Returning is not
		// optional: the upload below would memcpy from a NULL source.
		fHardwareCursorEnabled = false;
		return;
	}
	const uint8* src = (const uint8*)cursor->Bits();
	uint8* dst = dev->cursor_map;
	const uint32 dstStride = dev->cursor_w * 4;
	for (int32 row = 0; row < ch; row++) {
		memcpy(dst + row * dstStride, src + row * cursor->BytesPerRow(),
			cw * 4);
	}

	const BPoint hot = cursor->GetHotSpot();
	const int32  px  = (int32)fCursorLocation.x - (int32)hot.x;
	const int32  py  = (int32)fCursorLocation.y - (int32)hot.y;

	int ret;
	if (fCursorUsesAtomic) {
		ret = (_AtomicSetCursor(dev->cursor_fb, dev->crtc,
			px, py, dev->cursor_w, dev->cursor_h) == B_OK) ? 0 : -1;
	} else {
		ret = drmModeSetCursor2(fFd, dev->crtc,
			dev->cursor_handle, dev->cursor_w, dev->cursor_h,
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
	// fHardwareCursorEnabled, not just dev->cursor_ok: SetCursor() may
	// have left a stale "ok" sprite armed-but-transparent for a rotated
	// orientation, and this must not re-show it.
	if (!dev || !dev->cursor_ok || !fHardwareCursorEnabled)
		return;

	if (!visible) {
		_DisableHardwareCursor();
	} else {
		BPoint hot(0, 0);
		if (fCursor.IsSet())
			hot = fCursor->GetHotSpot();
		const int32 px = (int32)fCursorLocation.x - (int32)hot.x;
		const int32 py = (int32)fCursorLocation.y - (int32)hot.y;

		// fHardwareCursorEnabled must track the actual result, or
		// _DrawCursor() and the hardware plane both draw at once.
		int ret;
		if (fCursorUsesAtomic) {
			ret = (_AtomicSetCursor(dev->cursor_fb, dev->crtc,
				px, py, dev->cursor_w, dev->cursor_h) == B_OK) ? 0 : -1;
		} else {
			ret = drmModeSetCursor(fFd, dev->crtc, dev->cursor_handle,
				dev->cursor_w, dev->cursor_h);
			if (ret == 0)
				drmModeMoveCursor(fFd, dev->crtc, px, py);
		}

		fHardwareCursorEnabled = (ret == 0);
	}
}


void
DrmHWInterface::SetDragBitmap(const ServerBitmap* bitmap,
	const BPoint& offsetFromCursor)
{
	struct modeset_dev* dev = get_dev();

	if (bitmap != NULL) {
		// Disable the HW sprite before delegating to base so the
		// software composite path owns the drag visual.
		if (dev && dev->cursor_ok && fHardwareCursorEnabled)
			_DisableHardwareCursor();
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

	if (fCursorUsesAtomic) {
		_AtomicSetCursor(dev->cursor_fb, dev->crtc,
			px, py, dev->cursor_w, dev->cursor_h);
	} else {
		drmModeMoveCursor(fFd, dev->crtc, px, py);
	}

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

		// The uevent carries no CONNECTOR=, so every connector is
		// rescanned; scope the resize reaction to the one we render to.
		struct modeset_dev* primary = get_dev();
		uint32_t primaryConn = primary != NULL ? primary->conn : 0;
		bool resizePrimary = false;

		drmModeRes* res = drmModeGetResources(fFd);
		if (res) {
			for (int i = 0; i < res->count_connectors; i++) {
				drmModeConnector* conn = drmModeGetConnector(fFd,
					res->connectors[i]);
				if (conn) {
					if (conn->connection == DRM_MODE_CONNECTED) {
						int r = modeset_add_connector(fFd, conn->connector_id);
						if (r == 1 && conn->connector_id == primaryConn)
							resizePrimary = true;
					} else
						modeset_remove_connector(fFd, conn->connector_id);
					drmModeFreeConnector(conn);
				}
			}
			drmModeFreeResources(res);
		}

		// Not master while inactive (VT-switched away); applying a mode
		// here would fail or steal master from whoever now owns it.
		if (resizePrimary && fSessionActive.load())
			_ScheduleResize();
	}

	udev_device_unref(dev);
}


void
DrmHWInterface::_ScheduleResize()
{
	// Off the DRM event thread so SetMode() doesn't block flip servicing;
	// don't join a resize already running, hand it the newer geometry instead.
	fResizePending.store(true);
	if (fResizeBusy.exchange(true))
		return;

	fResizeThread = spawn_thread(_ResizeThreadEntry, "drm resize",
		B_NORMAL_PRIORITY, this);
	if (fResizeThread >= 0)
		resume_thread(fResizeThread);
	else
		fResizeBusy.store(false);
}


int32
DrmHWInterface::_ResizeThreadEntry(void* data)
{
	static_cast<DrmHWInterface*>(data)->_ApplyResize();
	return 0;
}


void
DrmHWInterface::_ApplyResize()
{
	while (fResizePending.exchange(false)) {
		display_mode mode;
		if (GetPreferredMode(&mode) != B_OK)
			break;

		// SetMode() itself must stay silent; app-initiated callers already
		// trigger Desktop's own _ScreenChanged().
		if (SetMode(mode) == B_OK)
			_NotifyScreenChanged();
	}

	fResizeBusy.store(false);
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
	if (dev == NULL || dev->cursor_handle == 0) {
		fprintf(stderr, "DRM: no cursor buffer, using software cursor\n");
		fHardwareCursorEnabled = false;
		return;
	}

	// Legacy by default. An atomic cursor commit is a second non-blocking
	// commit on a CRTC that already has a page flip in flight, which the
	// kernel answers with EBUSY; Weston avoids that by batching the cursor
	// into the frame's commit, which we do not do yet. VOS_ATOMIC_CURSOR
	// opts in for testing that path.
	static const char* sAtomicCursor = getenv("VOS_ATOMIC_CURSOR");
	const bool canAtomic = sAtomicCursor != NULL && sAtomicCursor[0] != '\0'
		&& fAtomicSupported && fCursorPlaneId != 0 && dev->cursor_fb != 0;

	int r = -1;
	if (canAtomic) {
		r = (_AtomicSetCursor(dev->cursor_fb, dev->crtc,
			0, 0, dev->cursor_w, dev->cursor_h) == B_OK) ? 0 : -1;
		if (r == 0) {
			fprintf(stderr, "DRM: hardware cursor via atomic plane\n");
			fCursorUsesAtomic = true;
			fHardwareCursorEnabled = true;
			_DisableHardwareCursor();
			return;
		}
	}

	fCursorUsesAtomic = false;
	r = drmModeSetCursor(fFd, dev->crtc,
		dev->cursor_handle, dev->cursor_w, dev->cursor_h);
	if (r == 0) {
		fprintf(stderr, "DRM: hardware cursor via legacy ioctl\n");
		fHardwareCursorEnabled = true;
		_DisableHardwareCursor();
		return;
	}

	fprintf(stderr,
		"DRM: hardware cursor not supported (%s), using software "
		"cursor\n", strerror(-r));
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


// Precedence: kernel connector property, then the DMI quirk table, then
// VOS_PANEL_ORIENTATION (to test/flip rotation direction without a rebuild).
void
DrmHWInterface::_DiscoverPanelOrientation()
{
	fPanelOrientation = PANEL_ORIENTATION_NORMAL;

	struct modeset_dev* dev = get_dev();
	if (dev != NULL && fFd >= 0) {
		drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(
			fFd, dev->conn, DRM_MODE_OBJECT_CONNECTOR);
		if (props != NULL) {
			for (uint32_t i = 0; i < props->count_props; i++) {
				drmModePropertyPtr prop
					= drmModeGetProperty(fFd, props->props[i]);
				if (prop == NULL)
					continue;
				if (strcmp(prop->name, "panel orientation") == 0
						&& props->prop_values[i]
							<= PANEL_ORIENTATION_RIGHT_UP) {
					fPanelOrientation
						= (enum PanelOrientation)props->prop_values[i];
				}
				drmModeFreeProperty(prop);
			}
			drmModeFreeObjectProperties(props);
		}
	}

	if (fPanelOrientation == PANEL_ORIENTATION_NORMAL && dev != NULL
			&& _ApplyDmiOrientationQuirk(dev->width, dev->height)) {
		fprintf(stderr, "DrmHWInterface: panel orientation from DMI "
			"quirk table\n");
	}

	const char* value = getenv("VOS_PANEL_ORIENTATION");
	int32 override = fPanelOrientation;
	switch (parse_panel_orientation(value, override)) {
		case B_PANEL_ENV_INVALID:
			fprintf(stderr, "DrmHWInterface: unrecognized "
				"VOS_PANEL_ORIENTATION '%s' (want "
				"normal|upside-down|left-up|right-up or 0-3), ignoring\n",
				value);
			break;
		case B_PANEL_ENV_SET:
			fPanelOrientation = (enum PanelOrientation)override;
			fprintf(stderr, "DrmHWInterface: panel orientation overridden "
				"to '%s' via VOS_PANEL_ORIENTATION\n", value);
			break;
		case B_PANEL_ENV_UNSET:
			break;
	}
}


int32
DrmHWInterface::PanelOrientation() const
{
	return (int32)fPanelOrientation;
}


status_t
DrmHWInterface::SetPanelOrientation(int32 orientation)
{
	if (orientation < -1 || orientation > PANEL_ORIENTATION_RIGHT_UP)
		return B_BAD_VALUE;

	enum PanelOrientation previous = fPanelOrientation;
	if (orientation < 0)
		_DiscoverPanelOrientation();
	else
		fPanelOrientation = (enum PanelOrientation)orientation;

	if (fPanelOrientation == previous)
		return B_OK;

	struct modeset_dev* dev = get_dev();
	if (dev == NULL) {
		fPanelOrientation = previous;
		return B_ERROR;
	}

	// The panel itself never changes shape, so re-derive the logical size
	// from the unchanged physical one and let SetMode() do the teardown.
	uint32_t logW, logH;
	_ApplyOrientationSwap(dev->width, dev->height, logW, logH);

	display_mode mode = fDisplayMode;
	mode.virtual_width = (uint16)logW;
	mode.virtual_height = (uint16)logH;

	status_t status = SetMode(mode);
	if (status != B_OK) {
		fPanelOrientation = previous;
		return status;
	}

	// SetMode() stays silent for app-initiated callers; nothing else will
	// tell Desktop that the frame and the input orientation just changed.
	_NotifyScreenChanged();
	return B_OK;
}


// No kernel or DMI source for reflection (unlike orientation): mirrored
// panels aren't a hardware fact to discover, so VOS_PANEL_REFLECTION is the
// only source, purely for testing without a rebuild.
void
DrmHWInterface::_DiscoverPanelReflection()
{
	fPanelReflection = B_PANEL_REFLECTION_NONE;

	const char* value = getenv("VOS_PANEL_REFLECTION");
	switch (parse_panel_reflection(value, fPanelReflection)) {
		case B_PANEL_ENV_INVALID:
			fprintf(stderr, "DrmHWInterface: unrecognized "
				"VOS_PANEL_REFLECTION '%s' (want none|x|y|both or 0-3), "
				"ignoring\n", value);
			break;
		case B_PANEL_ENV_SET:
			fprintf(stderr, "DrmHWInterface: panel reflection overridden "
				"to '%s' via VOS_PANEL_REFLECTION\n", value);
			break;
		case B_PANEL_ENV_UNSET:
			break;
	}
}


int32
DrmHWInterface::PanelReflection() const
{
	return fPanelReflection;
}


// Unlike SetPanelOrientation(), never calls SetMode(): a flip never changes
// logical width/height, so a bare _NotifyScreenChanged() suffices. Also
// unlike orientation, there is no auto (-1) fallback state for reflection.
status_t
DrmHWInterface::SetPanelReflection(int32 reflection)
{
	if (reflection < 0 || reflection > B_PANEL_REFLECTION_BOTH)
		return B_BAD_VALUE;

	if (reflection == fPanelReflection)
		return B_OK;

	fPanelReflection = reflection;
	_NotifyScreenChanged();
	return B_OK;
}


// dev->width/height (physical scanout) <-> fDisplayMode.virtual_width/height
// (logical desktop) differ by an axis swap whenever the panel is mounted
// sideways. The swap is its own inverse, so one function covers both
// directions.
void
DrmHWInterface::_ApplyOrientationSwap(uint32_t w, uint32_t h,
	uint32_t& outW, uint32_t& outH) const
{
	if (fPanelOrientation == PANEL_ORIENTATION_LEFT_UP
			|| fPanelOrientation == PANEL_ORIENTATION_RIGHT_UP) {
		outW = h;
		outH = w;
	} else {
		outW = w;
		outH = h;
	}
}


// Rotated or reflected would need a pre-transformed sprite, which we
// deliberately do not produce; a cursor plane advertising DRM rotation
// would relax that, the way Weston's drm_rotation_from_output_transform()
// does. Without a cursor plane the legacy ioctl still works, but the sprite
// is then serviced by a plane we cannot see and a virtualized host may draw
// or hide it as its own pointer, so Weston declines it too (backend-drm/
// kms.c, "if (!plane) return").
bool
DrmHWInterface::_HardwareCursorUsable() const
{
	static const char* sForceHw = getenv("VOS_HW_CURSOR");
	static bool sForceHwCursor = sForceHw != NULL && sForceHw[0] != '\0';

	return fPanelOrientation == PANEL_ORIENTATION_NORMAL
		&& fPanelReflection == B_PANEL_REFLECTION_NONE
		&& (fCursorPlaneId != 0 || sForceHwCursor);
}


namespace {

struct PanelOrientationQuirk {
	const char* sysVendor;
	const char* productName;
	uint32_t width, height;
	PanelOrientation orientation;
};

// Only for kernels predating the same quirk in the kernel's own
// drm_panel_orientation_quirks.c; a newer one answers through the connector
// property. Matching mirrors upstream's: substring on the vendor, which
// these vendors pad, and exact on the product name.
const PanelOrientationQuirk kPanelOrientationQuirks[] = {
	{ "CHUWI", "MiniBook X", 1200, 1920, PANEL_ORIENTATION_RIGHT_UP },
};

bool
read_dmi_field(const char* name, char* out, size_t outSize)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/class/dmi/id/%s", name);
	FILE* f = fopen(path, "r");
	if (f == NULL)
		return false;
	bool ok = fgets(out, outSize, f) != NULL;
	fclose(f);
	if (ok) {
		size_t len = strlen(out);
		while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r'))
			out[--len] = '\0';
	}
	return ok;
}

}	// namespace


// Resolution guards against a reused BIOS string, as the kernel's does.
bool
DrmHWInterface::_ApplyDmiOrientationQuirk(uint32_t width, uint32_t height)
{
	char sysVendor[128], productName[128];
	if (!read_dmi_field("sys_vendor", sysVendor, sizeof(sysVendor))
			|| !read_dmi_field("product_name", productName,
				sizeof(productName)))
		return false;

	for (size_t i = 0;
			i < sizeof(kPanelOrientationQuirks)
				/ sizeof(kPanelOrientationQuirks[0]); i++) {
		const PanelOrientationQuirk& q = kPanelOrientationQuirks[i];
		if (strstr(sysVendor, q.sysVendor) != NULL
				&& strcmp(productName, q.productName) == 0
				&& width == q.width && height == q.height) {
			fPanelOrientation = q.orientation;
			return true;
		}
	}
	return false;
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

	// drmModeAtomicAddProperty() silently drops a property id of 0;
	// the commit then reports success without reaching the kernel.
	if (!fConnProps.crtc_id || !fCrtcProps.active || !fCrtcProps.mode_id
			|| !fPlaneProps.crtc_id || !fPlaneProps.fb_id) {
		fprintf(stderr, "[drm] _AtomicModeset: missing property id(s) "
			"(conn.crtc_id=%u crtc.active=%u crtc.mode_id=%u "
			"plane.crtc_id=%u plane.fb_id=%u); refusing\n",
			fConnProps.crtc_id, fCrtcProps.active, fCrtcProps.mode_id,
			fPlaneProps.crtc_id, fPlaneProps.fb_id);
		return B_ERROR;
	}

	_DrainPendingFlip();

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
