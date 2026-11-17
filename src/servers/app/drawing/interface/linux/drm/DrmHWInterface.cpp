/*
 * Copyright 2021-2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmHWInterface.h"

#include "ClientMemoryAllocator.h"
#include "DrmBuffer.h"
#include "DrmCompat.h"
#include "DrmFlipPolicy.h"
#include "DrmOutput.h"
#include "LibEvdevEventStream.h"
#include "Overlay.h"
#include "ServerBitmap.h"
#include "PanelOrientationTransform.h"
#include "ScreenManager.h"

#include <algorithm>
#include <new>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <libdrm/drm_mode.h>
#include <libdrm/drm_fourcc.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>

#include <String.h>

#include <device/DrmDeviceSelect.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include "modeset.h"


int DrmHWInterface::fFd = -1;

static const uint32 kMaxDamageClips = 16;

// Connector ids repeat from card to card: the top byte holds the card's slot.
static inline uint32
output_key(int slot, uint32 connector)
{
	return slot == 0 ? connector : ((uint32)slot << 24) | connector;
}

static inline int
key_slot(uint32 key)
{
	return (int)(key >> 24);
}

static inline uint32
key_connector(uint32 key)
{
	return key & 0xffffff;
}

static drmModeModeInfo* find_connector_mode(drmModeConnector* conn,
	uint32_t width, uint32_t height, const display_mode& mode);


// An output beside the first one. It presents its rectangle of the render
// buffer through its own scanout buffers.
struct DrmHWInterface::Output {
	// The key; see output_key().
	uint32_t			connector;
	// The connector on its card, and the card's fd.
	uint32_t			rawConnector;
	int					fd;
	int					slot;
	struct modeset_dev*	dev;
	DrmBuffer*			front;
	DrmBuffer*			back;
	DrmBuffer*			target;
	DrmBufferView		source;
	BPoint				origin;
	display_mode		mode;
	int32				orientation;
	int32				reflection;
	uint32				dpms;
	// Output coordinates; fDirtyMutex.
	BRegion				dirty;
	BRegion				stale;
	bool				flipPending;
	uint64_t			generation;
	bigtime_t			lastFlip;
	bool				modeChanged;

	// The pointer's own sprite on this output: a small dumb buffer on its
	// fd, loaded for one cursor at a time.
	uint32_t			spriteHandle;
	uint8_t*			spriteMap;
	uint32_t			spriteSize;
	uint32_t			spriteW;
	uint32_t			spriteH;
	const void*			spriteCursor;
	bool				spriteFailed;
	bool				spriteShown;

	~Output()
	{
		if (spriteMap != NULL)
			munmap(spriteMap, spriteSize);
		if (spriteHandle != 0) {
			struct drm_mode_destroy_dumb dreq;
			memset(&dreq, 0, sizeof(dreq));
			dreq.handle = spriteHandle;
			drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
		}
	}

	BRect Frame() const
	{
		return BRect(origin.x, origin.y, origin.x + mode.virtual_width - 1,
			origin.y + mode.virtual_height - 1);
	}
};


DrmBufferView::DrmBufferView()
	:
	fBits(NULL),
	fBytesPerRow(0),
	fWidth(0),
	fHeight(0),
	fColorSpace(B_RGB32)
{
}


void
DrmBufferView::SetTo(RenderingBuffer* buffer, int32 x, int32 y, uint32 width,
	uint32 height)
{
	fBits = NULL;
	fBytesPerRow = 0;
	fWidth = 0;
	fHeight = 0;

	if (buffer == NULL || buffer->Bits() == NULL || x < 0 || y < 0
			|| x + width > buffer->Width() || y + height > buffer->Height())
		return;

	fBytesPerRow = buffer->BytesPerRow();
	fBits = (uint8*)buffer->Bits() + y * fBytesPerRow + x * 4;
	fWidth = width;
	fHeight = height;
	fColorSpace = buffer->ColorSpace();
}


status_t
DrmBufferView::InitCheck() const
{
	return fBits != NULL ? B_OK : B_NO_INIT;
}


color_space
DrmBufferView::ColorSpace() const
{
	return fColorSpace;
}


void*
DrmBufferView::Bits() const
{
	return fBits;
}


uint32
DrmBufferView::BytesPerRow() const
{
	return fBytesPerRow;
}


uint32
DrmBufferView::Width() const
{
	return fWidth;
}


uint32
DrmBufferView::Height() const
{
	return fHeight;
}


// Scoped fModesetMutex holder: SetMode() has many return paths and holds a
// modeset_dev pointer across all of them, while a udev hotplug on the event
// thread can free that node in _RescanConnectors().
namespace {
struct ModesetGuard {
	ModesetGuard(pthread_mutex_t* mutex) : fMutex(mutex)
		{ pthread_mutex_lock(fMutex); }
	~ModesetGuard() { pthread_mutex_unlock(fMutex); }
	pthread_mutex_t* fMutex;
};
}


// New dumb and render buffers come back uninitialized, often holding the
// previous rotation; fill opaque black (cursor blending assumes alpha 255).
static void
_ClearBufferToBlack(RenderingBuffer* buffer)
{
	if (buffer == NULL || buffer->Bits() == NULL)
		return;
	// B_RGB32 is B, G, R, A in memory: 0xFF000000 is opaque black, while
	// a 0xFF byte fill is opaque white.
	uint8* row = (uint8*)buffer->Bits();
	const uint32 width = buffer->Width();
	for (uint32 y = 0; y < buffer->Height(); y++) {
		uint32* pixel = (uint32*)row;
		for (uint32 x = 0; x < width; x++)
			pixel[x] = 0xFF000000;
		row += buffer->BytesPerRow();
	}
}

extern "C" void seat_enable_cb(struct libseat* seat, void* data)
{
	DrmHWInterface* hw = static_cast<DrmHWInterface*>(data);
	// Through the locked wrapper: enable rebuilds and restores buffers.
	hw->OnSeatEnabled();

}

extern "C" void seat_disable_cb(struct libseat* seat, void* data)
{
	DrmHWInterface* hw = static_cast<DrmHWInterface*>(data);
	hw->OnSeatDisabled();

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
	fLastModeCheck(0),
	fLastPreferredWidth(0),
	fLastPreferredHeight(0),
	fUserSetMode(false),
	fSessionSem(create_sem(0, "drm session sem")),
	fUdev(NULL),
	fUdevMonitor(NULL),
	fUdevFd(-1),
	fLastSuspendCheck(0),
#ifdef HAVE_GBM
	fGbmDevice(NULL),
#endif
	fRenderBuffer(NULL),
	fDev(NULL),
	fOrigin(B_ORIGIN),
	fPageFlipEnabled(false),
	fPageFlipPending(false),
	fCursorInFlightAt(0),
	fHoldStats(getenv("VOS_COMPOSE_HOLD_STATS") != NULL
		&& atoi(getenv("VOS_COMPOSE_HOLD_STATS")) != 0),
	fHoldDeferred(0),
	fHoldExpired(0),
	fHoldStatsAt(0),
	fNeedsFlip(false),
	fDamageFlush(false),
	fWakeQueued(false),
	fDirectRender(false),
	fFrameSerial(0),
	fAgeSlotCount(0),
	fPacingLockHeld(false),
	fFlipIssueTime(0),
	fLastFlipIssue(0),
	fLastFlipDone(0),
	fFlipVblank(false),
	fWakeFd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)),
	fDpmsState(B_DPMS_ON),
	fBacklight(NULL),
	fTemperature(6500.0f),
	fTemperatureSupported(true),
	fAtomicSupported(false),
	fPrimaryPlaneId(0),
	fCursorPlaneId(0),
	fCursorUsesAtomic(false),
	fAtomicBatch(false),
	fStagedCursor{},
	fVblankPacedFlip(false),
	fFlipFailures(0),
	fFlipRecovered(false),
	fCursorRetryAt(0),
	fModesetActive(false),
	fModeBlobId(0),
	fPlaneProps{},
	fCursorPlaneProps{},
	fCrtcProps{},
	fConnProps{},
	fVRRSupported(false),
	fVRREnabled(false),
	fVRRWanted(false),
	fPanelOrientation(PANEL_ORIENTATION_NORMAL),
	fMountingOrientation(B_PANEL_ORIENTATION_NORMAL),
	fUserOrientation(B_PANEL_ORIENTATION_NORMAL),
	fPanelReflection(B_PANEL_REFLECTION_NONE)
{
	memset(fAgeFbIds, 0, sizeof(fAgeFbIds));
	pthread_mutex_init(&fFlipMutex, NULL);
	pthread_mutex_init(&fCursorBlendMutex, NULL);
	// Recursive: SetMode() holds it and calls SetCursor(), which re-takes
	// it to copy the cursor plane ids out of the modeset node.
	pthread_mutexattr_t modesetAttr;
	pthread_mutexattr_init(&modesetAttr);
	pthread_mutexattr_settype(&modesetAttr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&fModesetMutex, &modesetAttr);
	pthread_mutexattr_destroy(&modesetAttr);
	pthread_cond_init(&fFlipCond, NULL);
	fFlipGeneration = 0;
	pthread_mutex_init(&fOverlayMutex, NULL);
	fOverlayChannelCounter = 0;

	// Safe Mode and other nomodeset boots have no DRM device; janus_session
	// sets JANUS_FBDEV so we fail at once and app_server falls back to fbdev.
	if (getenv("JANUS_FBDEV") != NULL) {
		fprintf(stderr,
			"DrmHWInterface: JANUS_FBDEV set; skipping DRM for fbdev fallback\n");
		return;
	}

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
		// Without janus we must not block forever: janus already owns the
		// seat in a session, so our own libseat session never activates.
		// Firmware-only cards (simpledrm/efidrm) are not a GPU driver.
		bool haveCard = false;
		for (int i = 0; i <= 9; i++) {
			char path[64];
			snprintf(path, sizeof(path), "/dev/dri/card%d", i);
			if (access(path, F_OK) == 0 && !drm_card_is_firmware(i)) {
				haveCard = true;
				break;
			}
		}
		if (!haveCard) {
			fprintf(stderr,
				"DrmHWInterface: no KMS DRM device present; "
				"failing for fbdev\n");
			return;
		}

		fSeat = libseat_open_seat(&seat_listener, this);
		if (!fSeat) {
			fprintf(stderr, "Failed to open libseat session\n");
			return;
		}
		printf("libseat opened (standalone), fSeat=%p, seat_fd=%d\n",
			(void*)fSeat, libseat_get_fd(fSeat));
		// Bound the wait; a seat that never activates must not hang app_server.
		for (int waited = 0; !fSessionActive && waited < 3000; waited += 100) {
			int ret = libseat_dispatch(fSeat, 100);
			if (ret < 0)
				break;
		}
		if (!fSessionActive) {
			fprintf(stderr,
				"DrmHWInterface: libseat session did not activate in time\n");
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
DrmHWInterface::_ResetFlipState()
{
	// Probe flips again, and drop updates staged against the old state.
	fVblankPacedFlip = false;
	fFlipFailures = 0;
	fFlipRecovered = false;
	fCursorRetryAt = 0;
	fDirtyMutex.Lock();
	fStagedCursor.valid = false;
	fStagedPlanes.clear();
	fNeedsFlip = true;
	fDirtyMutex.Unlock();
}


void
DrmHWInterface::_OnSessionEnable()
{
	printf("Session enabled\n");

	// The new session gets a fresh flip probe: whatever made the old
	// one fall back left with the old DRM master.
	fVblankPacedFlip = false;
	fFlipFailures = 0;

	if (fInitialized) {
		fSessionActive = true;
		release_sem(fSessionSem);

		_RestoreDisplay();

		// Re-arm the plane _OnSessionDisable() cleared, from last known state.
		_RearmCursor();
		_RearmOverlayPlanes();

		LockExclusiveAccess();
		Invalidate(_DesktopBounds());
		UnlockExclusiveAccess();
		return;
	}

	struct drm_extra_card extras[DRM_MAX_EXTRA_CARDS];
	int extraCount = 0;

	const char* janusDrmFdStr = getenv("JANUS_DRM_FD");
	if (janusDrmFdStr != NULL && janusDrmFdStr[0] != '\0') {
		fFd = atoi(janusDrmFdStr);
		fDeviceId = 0;

		// "fd:card,fd:card": the other cards with connectors.
		const char* list = getenv("JANUS_DRM_FDS");
		while (list != NULL && list[0] != '\0'
				&& extraCount < DRM_MAX_EXTRA_CARDS) {
			char* end;
			const int extraFd = (int)strtol(list, &end, 10);
			if (*end != ':')
				break;
			const int extraCard = (int)strtol(end + 1, &end, 10);
			extras[extraCount].deviceId = 0;
			extras[extraCount].fd = extraFd;
			extras[extraCount].cardIndex = extraCard;
			extraCount++;
			if (*end != ',')
				break;
			list = end + 1;
		}
	} else {
		int deviceId = -1;
		int fd = -1;
		int cardIndex = -1;
		const char* why = NULL;
		if (!drm_select_seat_device(fSeat, deviceId, fd, cardIndex, why,
				extras, &extraCount)) {
			fDeviceId = -1;
			fFd = -1;
		} else {
			char path[B_PATH_NAME_LENGTH];
			snprintf(path, sizeof(path), "/dev/dri/card%d",
				cardIndex);
			fDeviceId = deviceId;
			fFd = fd;
			printf("DrmHWInterface: opened DRM device %s fd=%d (%s)\n",
				path, fFd, why);
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

	fDev = _PickDev();
	if (fDev == NULL) {
		fprintf(stderr, "DrmHWInterface: no connected output\n");
		return;
	}

	_OpenCards(extras, extraCount);

	// Needed before any buffer is sized below, since fRenderBuffer must be
	// allocated at logical (post-swap) dimensions, not the panel's physical
	// mode.
	_DiscoverPanelOrientation();
	_DiscoverPanelReflection();

	// Save current CRTC state for restoration on exit
	struct modeset_dev *iter;
	for (iter = get_dev(); iter; iter = iter->next)
		iter->saved_crtc = drmModeGetCrtc(fFd, iter->crtc);

	_SelectPresentMode();

	fFrontBuffer = new DrmBuffer(fFd, fDev, false);
	fBackBuffer = NULL;
	fWriteTarget = fFrontBuffer;
	fPageFlipEnabled = false;
	fPageFlipPending = false;

	fDirtyMutex.Lock();
	fAccumulatedDirty.MakeEmpty();
	fBackStale.MakeEmpty();
	fNeedsFlip = false;
	fDirtyMutex.Unlock();

	if (fDamageFlush) {
		fPageFlipEnabled = true;
	} else if (modeset_create_back_fb(fFd, fDev) == 0) {
		fBackBuffer      = new DrmBuffer(fFd, fDev, true);
		fWriteTarget     = fBackBuffer;
		fPageFlipEnabled = true;
	} else {
		fprintf(stderr,
			"[drm] back dumb-buffer creation failed; running "
			"single-buffered with tearing\n");
	}

	uint32_t logW, logH;
	_ApplyOrientationSwap(fDev->width, fDev->height, logW, logH);
	_RebuildRenderBuffer(logW, logH);

	// The modeset below scans out fFrontBuffer at once; a fresh dumb
	// buffer is uninitialized memory, so hand the CRTC defined contents.
	_ClearBufferToBlack(fFrontBuffer);
	_ClearBufferToBlack(fBackBuffer);

	_ProbeAtomic();
	{
		const char* vrr = getenv("VOS_VRR");
		fVRRWanted = vrr != NULL && vrr[0] != '\0' && strcmp(vrr, "0") != 0;
	}
	if (fAtomicSupported)
		_DiscoverProperties();

	// Default when atomic and a cursor plane exist; VOS_ATOMIC_BATCH=0 or
	// VOS_LEGACY_CURSOR=1 keep the legacy flip and cursor ioctls.
	static const char* sAtomicBatch = getenv("VOS_ATOMIC_BATCH");
	static const char* sLegacyCursor = getenv("VOS_LEGACY_CURSOR");
	const bool batchOff = (sAtomicBatch != NULL && strcmp(sAtomicBatch, "0") == 0)
		|| (sLegacyCursor != NULL && sLegacyCursor[0] != '\0'
			&& strcmp(sLegacyCursor, "0") != 0);
	fAtomicBatch = !batchOff && fAtomicSupported && fPrimaryPlaneId != 0
		&& fCursorPlaneId != 0;
	fprintf(stderr, "DRM: %s\n", fAtomicBatch
		? "batched atomic commit (flip+cursor+planes)"
		: batchOff ? "legacy flip and cursor (forced)"
			: "legacy flip and cursor (no atomic cursor plane)");

	// Configure the crtc; the other outputs are set up as they are added.
	iter = fDev;
	{
		bool modesetOk = false;
		if (fAtomicSupported && fPrimaryPlaneId) {
			status_t r = _AtomicModeset(iter->fb, &iter->mode);
			modesetOk = (r == B_OK);
			if (!modesetOk)
				fprintf(stderr, "atomic modeset failed for connector %u: %m\n",
					iter->conn);
		}
		// Atomic commit failure at boot must not leave the CRTC dark
		// while app_server keeps drawing into an unsampled buffer.
		if (!modesetOk) {
			ret = drmModeSetCrtc(fFd, iter->crtc, iter->fb, 0, 0,
			                     &iter->conn, 1, &iter->mode);
			if (ret)
				fprintf(stderr, "cannot set CRTC for connector %u (%d): %m\n",
					iter->conn, errno);
			else if (fAtomicSupported && fPrimaryPlaneId)
				fprintf(stderr, "fell back to legacy CRTC for connector %u\n",
					iter->conn);
		}
	}

	modeset_create_cursor_fb(fFd, fDev);

	_ProbeCursor();
	_SetupDirectRender();

	fUdev = udev_new();
	if (fUdev) {
		struct stat st;
		if (fstat(fFd, &st) == 0) {
			struct udev_device* udevDev = udev_device_new_from_devnum(fUdev,
				'c', st.st_rdev);
			if (udevDev) {
				struct modeset_dev* dev = fDev;
				drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
				if (conn) {
					fBacklight = backlight_init(udevDev, conn->connector_type);
					if (fBacklight != NULL) {
						fprintf(stderr, "DRM: backlight bound to %s "
							"(type %d, max %d)\n", fBacklight->path,
							fBacklight->type, fBacklight->max_brightness);
					} else {
						fprintf(stderr, "DRM: no backlight device found "
							"(connector type %u)\n", conn->connector_type);
					}
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

	struct modeset_dev* initDev = fDev;
	_FillModeInfo(fDisplayMode, initDev->mode);
	uint32_t initLogW, initLogH;
	_ApplyOrientationSwap(initDev->width, initDev->height, initLogW, initLogH);
	fDisplayMode.virtual_width = initLogW;
	fDisplayMode.virtual_height = initLogH;
	fLastPreferredWidth = initLogW;
	fLastPreferredHeight = initLogH;
	fUserSetMode = false;

	fInitialized = true;
	fSessionActive = true;
	release_sem(fSessionSem);
}


void
DrmHWInterface::_DrainPendingFlip()
{
	if (!fPageFlipPending.load())
		return;

	// In standalone mode the libseat callbacks run on the event thread
	// itself (libseat_dispatch), so waiting for that thread to deliver
	// the flip would wait for ourselves. Service the fd inline instead;
	// we are the only reader here.
	if (find_thread(NULL) == fEventThread) {
		for (int i = 0; i < 8 && fPageFlipPending.load(); i++) {
			struct pollfd pfd = { fFd, POLLIN, 0 };
			if (poll(&pfd, 1, 16) <= 0 || !(pfd.revents & POLLIN))
				break;
			drmEventContext evctx = {
				.version           = DRM_EVENT_CONTEXT_VERSION,
				.page_flip_handler = _PageFlipHandler,
			};
			drmHandleEvent(fFd, &evctx);
		}
		if (fPageFlipPending.load()) {
			pthread_mutex_lock(&fFlipMutex);
			fFlipGeneration++;
			fPageFlipPending = false;
			_DamageAll();
			pthread_mutex_unlock(&fFlipMutex);
		}
		return;
	}

	// The event thread is the only reader of the DRM fd: calling
	// drmHandleEvent() here as a second reader races it and can split an
	// event across two reads. Instead wake it and wait for the completion.
	// A timeout abandons the flip: the generation bump below makes the
	// late event a no-op, so the caller may tear the buffers down.
	_Wake();

	struct timespec deadline;
	clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_nsec += 250 * 1000 * 1000;
	if (deadline.tv_nsec >= 1000 * 1000 * 1000) {
		deadline.tv_sec += 1;
		deadline.tv_nsec -= 1000 * 1000 * 1000;
	}

	pthread_mutex_lock(&fFlipMutex);
	while (fPageFlipPending.load()) {
		if (pthread_cond_timedwait(&fFlipCond, &fFlipMutex, &deadline) != 0)
			break;
	}
	if (fPageFlipPending.load()) {
		// Abandoned: a completion event that lands after the caller's
		// teardown must not swap the replacement buffers.
		fFlipGeneration++;
		fPageFlipPending = false;
		_DamageAll();
	}
	pthread_mutex_unlock(&fFlipMutex);
}


// An abandoned flip may still have landed, so which buffer is on screen is
// unknown: both get the whole frame again.
void
DrmHWInterface::_DamageAll()
{
	fDirtyMutex.Lock();
	fAccumulatedDirty.Include(BRect(0, 0, fDisplayMode.virtual_width - 1,
		fDisplayMode.virtual_height - 1));
	fDirtyMutex.Unlock();
}


// #pragma mark - outputs


// The internal panel when there is one, else the lowest connector.
struct modeset_dev*
DrmHWInterface::_PickDev() const
{
	struct modeset_dev* picked = NULL;
	bool pickedInternal = false;

	for (struct modeset_dev* iter = get_dev(); iter; iter = iter->next) {
		bool internal = false;
		drmModeConnector* conn = drmModeGetConnectorCurrent(fFd, iter->conn);
		if (conn != NULL) {
			internal = conn->connector_type == DRM_MODE_CONNECTOR_eDP
				|| conn->connector_type == DRM_MODE_CONNECTOR_LVDS
				|| conn->connector_type == DRM_MODE_CONNECTOR_DSI;
			drmModeFreeConnector(conn);
		}

		if (picked == NULL || (internal && !pickedInternal)
				|| (internal == pickedInternal && iter->conn < picked->conn)) {
			picked = iter;
			pickedInternal = internal;
		}
	}

	return picked;
}


BRect
DrmHWInterface::_Frame() const
{
	return BRect(fOrigin.x, fOrigin.y,
		fOrigin.x + fDisplayMode.virtual_width - 1,
		fOrigin.y + fDisplayMode.virtual_height - 1);
}


BRect
DrmHWInterface::_DesktopBounds() const
{
	if (fRenderBuffer == NULL)
		return _Frame();
	return BRect(0, 0, fRenderBuffer->Width() - 1,
		fRenderBuffer->Height() - 1);
}


bool
DrmHWInterface::_SingleOutput() const
{
	return fOutputs.empty() && fOrigin == B_ORIGIN;
}


// Exclusive lock held. width and height are the first output's logical
// size, which fDisplayMode may not hold yet.
void
DrmHWInterface::_RebuildRenderBuffer(uint32 width, uint32 height)
{
	BRect bounds(fOrigin.x, fOrigin.y, fOrigin.x + width - 1,
		fOrigin.y + height - 1);
	for (size_t i = 0; i < fOutputs.size(); i++)
		bounds = bounds | fOutputs[i]->Frame();

	const uint32 totalWidth = (uint32)bounds.right + 1;
	const uint32 totalHeight = (uint32)bounds.bottom + 1;

	if (fRenderBuffer == NULL || fRenderBuffer->Width() != totalWidth
			|| fRenderBuffer->Height() != totalHeight) {
		MallocBuffer* buffer = new MallocBuffer(totalWidth, totalHeight);
		_ClearBufferToBlack(buffer);

		// What stays in place keeps its pixels; nobody redraws it.
		if (fRenderBuffer != NULL && fRenderBuffer->Bits() != NULL
				&& buffer->Bits() != NULL) {
			const uint32 rows = std::min(fRenderBuffer->Height(),
				totalHeight);
			const uint32 bytes = std::min(fRenderBuffer->Width(),
				totalWidth) * 4;
			const uint8* from = (const uint8*)fRenderBuffer->Bits();
			uint8* to = (uint8*)buffer->Bits();
			for (uint32 y = 0; y < rows; y++) {
				memcpy(to, from, bytes);
				from += fRenderBuffer->BytesPerRow();
				to += buffer->BytesPerRow();
			}
		}

		delete fRenderBuffer;
		fRenderBuffer = buffer;
	}

	fSource.SetTo(fRenderBuffer, (int32)fOrigin.x, (int32)fOrigin.y, width,
		height);
	for (size_t i = 0; i < fOutputs.size(); i++) {
		Output* output = fOutputs[i];
		output->source.SetTo(fRenderBuffer, (int32)output->origin.x,
			(int32)output->origin.y, output->mode.virtual_width,
			output->mode.virtual_height);
	}
}


void
DrmHWInterface::_SplitDamage(const BRect& frame, bool outputsOnly)
{
	if (!outputsOnly && fStandIn == 0) {
		BRect part = frame & _Frame();
		if (part.IsValid()) {
			part.OffsetBy(-fOrigin.x, -fOrigin.y);
			fAccumulatedDirty.Include(part);
		}
	}

	for (size_t i = 0; i < fOutputs.size(); i++) {
		Output* output = fOutputs[i];
		BRect part = frame & output->Frame();
		if (part.IsValid()) {
			part.OffsetBy(-output->origin.x, -output->origin.y);
			output->dirty.Include(part);
		}
	}
}


// Desktop coordinates in, each output's own out.
void
DrmHWInterface::_AddDamage(const BRect& frame)
{
	fDirtyMutex.Lock();
	if (_SingleOutput())
		fAccumulatedDirty.Include(frame);
	else
		_SplitDamage(frame, false);
	fDirtyMutex.Unlock();
}


void
DrmHWInterface::_AddDamage(const BRegion& region)
{
	fDirtyMutex.Lock();
	if (_SingleOutput())
		fAccumulatedDirty.Include(&region);
	else {
		const int32 count = region.CountRects();
		for (int32 i = 0; i < count; i++)
			_SplitDamage(region.RectAt(i), false);
	}
	fDirtyMutex.Unlock();
}


void
DrmHWInterface::_AddOutputDamage(const BRect& frame)
{
	if (!frame.IsValid())
		return;

	fDirtyMutex.Lock();
	const bool any = !fOutputs.empty();
	if (any)
		_SplitDamage(frame, true);
	fDirtyMutex.Unlock();

	if (any)
		_Wake();
}


// Logical rects of region from the buffer on screen into the one about to be
// shown, so held areas keep showing what is on screen now.
static void
copy_scanout_rects(RenderingBuffer* from, RenderingBuffer* to,
	const BRegion& region, int32 logicalW, int32 logicalH, int32 orientation,
	int32 reflection)
{
	const int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		BRect r = region.RectAt(i);
		int32 x0 = (int32)r.left, y0 = (int32)r.top;
		int32 x1 = (int32)r.right, y1 = (int32)r.bottom;
		x0 = max_c(x0, 0);
		y0 = max_c(y0, 0);
		x1 = min_c(x1, logicalW - 1);
		y1 = min_c(y1, logicalH - 1);
		if (x1 < x0 || y1 < y0)
			continue;
		int32 ax, ay, bx, by;
		rotate_point(orientation, x0, y0, logicalW, logicalH, ax, ay,
			reflection);
		rotate_point(orientation, x1, y1, logicalW, logicalH, bx, by,
			reflection);
		const int32 px0 = min_c(ax, bx), px1 = max_c(ax, bx);
		const int32 py0 = min_c(ay, by), py1 = max_c(ay, by);
		if (px1 >= (int32)from->Width() || py1 >= (int32)from->Height()
				|| px1 >= (int32)to->Width() || py1 >= (int32)to->Height())
			continue;
		for (int32 y = py0; y <= py1; y++) {
			memcpy((uint8*)to->Bits() + (size_t)y * to->BytesPerRow()
					+ px0 * 4,
				(const uint8*)from->Bits() + (size_t)y * from->BytesPerRow()
					+ px0 * 4,
				(px1 - px0 + 1) * 4);
		}
	}
}


void
DrmHWInterface::SetComposeHold(const void* owner, const BRegion* region)
{
	fDirtyMutex.Lock();
	if (region == NULL || region->CountRects() == 0)
		fComposeHolds.erase(owner);
	else {
		ComposeHold& hold = fComposeHolds[owner];
		hold.region = *region;
		hold.at = system_time();
	}
	fDirtyMutex.Unlock();
}


// A hold older than this is a hung client; its area is shown anyway.
static const bigtime_t kComposeHoldMax = 100000;


void
DrmHWInterface::_HeldRegion(BRegion& held)
{
	held.MakeEmpty();
	if (fComposeHolds.empty())
		return;
	const bigtime_t now = system_time();
	for (std::map<const void*, ComposeHold>::iterator it
			= fComposeHolds.begin(); it != fComposeHolds.end();) {
		if (now - it->second.at > kComposeHoldMax) {
			fHoldExpired++;
			fComposeHolds.erase(it++);
			continue;
		}
		held.Include(&it->second.region);
		++it;
	}
}


void
DrmHWInterface::_ReportHoldStats()
{
	if (!fHoldStats)
		return;
	const bigtime_t now = system_time();
	if (fHoldStatsAt == 0)
		fHoldStatsAt = now;
	if (now - fHoldStatsAt < 1000000)
		return;
	fDirtyMutex.Lock();
	const uint32 deferred = fHoldDeferred;
	const uint32 expired = fHoldExpired;
	fHoldDeferred = 0;
	fHoldExpired = 0;
	fDirtyMutex.Unlock();
	fHoldStatsAt = now;
	fprintf(stderr, "compose hold: deferred %u rects/s, expired %u\n",
		deferred, expired);
}


void
DrmHWInterface::_DamageOutputs()
{
	fDirtyMutex.Lock();
	for (size_t i = 0; i < fOutputs.size(); i++) {
		Output* output = fOutputs[i];
		output->dirty.Include(BRect(0, 0, output->mode.virtual_width - 1,
			output->mode.virtual_height - 1));
	}
	fDirtyMutex.Unlock();
}


// The fd of the card the key belongs to, or -1.
int
DrmHWInterface::_FdForKey(uint32 key) const
{
	const int slot = key_slot(key);
	if (slot == 0)
		return fFd;

	for (size_t i = 0; i < fCards.size(); i++) {
		if (fCards[i].slot == slot)
			return fCards[i].fd;
	}
	return -1;
}


int
DrmHWInterface::_CardIndexForSlot(int slot) const
{
	for (size_t i = 0; i < fCards.size(); i++) {
		if (fCards[i].slot == slot)
			return fCards[i].index;
	}
	return slot;
}


/*!	Takes over the cards beside the first one that have connectors: their fds
	come with the session, from janus or from libseat. Each gets its own
	list of connectors.
*/
void
DrmHWInterface::_OpenCards(const struct drm_extra_card* extras, int count)
{
	for (int i = 0; i < count && (int)fCards.size() < 3; i++) {
		if (modeset_prepare(extras[i].fd) != 0) {
			if (fSeat != NULL)
				libseat_close_device(fSeat, extras[i].deviceId);
			continue;
		}

		Card card;
		card.slot = (int)fCards.size() + 1;
		card.fd = extras[i].fd;
		card.index = extras[i].cardIndex;
		card.deviceId = extras[i].deviceId;
#ifdef HAVE_GBM
		card.gbm = NULL;
#endif
		fCards.push_back(card);
		fprintf(stderr, "DRM: card%d drives outputs too\n", card.index);
	}
}


DrmHWInterface::Output*
DrmHWInterface::_FindOutput(uint32 connector) const
{
	for (size_t i = 0; i < fOutputs.size(); i++) {
		if (fOutputs[i]->connector == connector)
			return fOutputs[i];
	}
	return NULL;
}


// Exclusive lock and fModesetMutex held; the node has its framebuffer.
status_t
DrmHWInterface::_SetupOutput(Output* output)
{
	struct modeset_dev* dev = output->dev;

	if (dev->saved_crtc == NULL)
		dev->saved_crtc = drmModeGetCrtc(output->fd, dev->crtc);

	output->front = new DrmBuffer(output->fd, dev, false);
	output->back = NULL;
	output->target = output->front;
	if (!fDamageFlush && modeset_create_back_fb(output->fd, dev) == 0) {
		output->back = new DrmBuffer(output->fd, dev, true);
		output->target = output->back;
	}

	_ClearBufferToBlack(output->front);
	_ClearBufferToBlack(output->back);

	if (drmModeSetCrtc(output->fd, dev->crtc, dev->fb, 0, 0, &dev->conn, 1,
			&dev->mode) != 0) {
		fprintf(stderr, "[drm] cannot set CRTC for connector %u: %s\n",
			dev->conn, strerror(errno));
		_TeardownOutput(output);
		return B_ERROR;
	}

	const bool swapped = output->orientation == PANEL_ORIENTATION_LEFT_UP
		|| output->orientation == PANEL_ORIENTATION_RIGHT_UP;
	fDirtyMutex.Lock();
	_FillModeInfo(output->mode, dev->mode);
	output->mode.virtual_width = swapped ? dev->height : dev->width;
	output->mode.virtual_height = swapped ? dev->width : dev->height;
	fDirtyMutex.Unlock();

	output->flipPending = false;
	output->lastFlip = 0;
	output->stale.MakeEmpty();
	return B_OK;
}


// A flip still in flight completes against the bumped generation and is
// dropped.
void
DrmHWInterface::_TeardownOutput(Output* output)
{
	output->generation++;
	output->flipPending = false;
	output->target = NULL;
	delete output->front;
	output->front = NULL;
	delete output->back;
	output->back = NULL;
}


bool
DrmHWInterface::_OutputsWaiting()
{
	bool waiting = false;
	fDirtyMutex.Lock();
	for (size_t i = 0; i < fOutputs.size(); i++) {
		if (fOutputs[i]->flipPending || (fOutputs[i]->dpms == B_DPMS_ON
				&& fOutputs[i]->dirty.CountRects() > 0))
			waiting = true;
	}
	fDirtyMutex.Unlock();
	return waiting;
}


// Event thread. Each output copies its damage out of the render buffer
// and shows it: a page flip with two buffers, a flush with one.
void
DrmHWInterface::_PresentOutputs()
{
	const bigtime_t now = system_time();

	bool due = false;
	fDirtyMutex.Lock();
	for (size_t i = 0; i < fOutputs.size(); i++) {
		Output* output = fOutputs[i];
		if (!output->flipPending && output->dpms == B_DPMS_ON
				&& output->dirty.CountRects() > 0
				&& now >= output->lastFlip + _FramePeriodFor(output->mode))
			due = true;
	}
	fDirtyMutex.Unlock();

	if (!due || !LockExclusiveAccess())
		return;

	_SyncOutputSprites();

	for (size_t i = 0; i < fOutputs.size(); i++) {
		Output* output = fOutputs[i];
		if (output->flipPending || output->dpms != B_DPMS_ON
				|| output->target == NULL
				|| now < output->lastFlip + _FramePeriodFor(output->mode))
			continue;

		BRegion damage;
		fDirtyMutex.Lock();
		BRegion held;
		_HeldRegion(held);
		held.OffsetBy(-output->origin.x, -output->origin.y);
		damage = output->dirty;
		damage.Exclude(&held);
		output->dirty.IntersectWith(&held);
		fHoldDeferred += output->dirty.CountRects();
		// Draws from here on wake the thread again.
		fWakeQueued = false;
		fDirtyMutex.Unlock();

		if (damage.CountRects() == 0)
			continue;

		BRegion present(damage);
		BRegion keepStale(output->stale);
		keepStale.IntersectWith(&held);
		BRegion stale(output->stale);
		stale.Exclude(&held);
		present.Include(&stale);
		if (output->back != NULL && keepStale.CountRects() > 0) {
			copy_scanout_rects(output->front, output->target, keepStale,
				(int32)output->source.Width(), (int32)output->source.Height(),
				output->orientation, output->reflection);
		}

		const int32 count = present.CountRects();
		for (int32 j = 0; j < count; j++) {
			_BlitRect(&output->source, output->target, present.RectAt(j),
				output->orientation, output->reflection);
		}

		bool overlaysLocked = fFloatingOverlaysLock.Lock();
		IntRect cursorFrame = _SoftCursorFrame();
		if (cursorFrame.IsValid()) {
			cursorFrame.OffsetBy(-(int32)output->origin.x,
				-(int32)output->origin.y);
			if (!output->spriteShown
					&& present.Intersects((BRect)cursorFrame)) {
				_BlendCursor(&output->source, output->target, cursorFrame,
					output->origin, output->orientation, output->reflection);
			}
		}
		if (overlaysLocked)
			fFloatingOverlaysLock.Unlock();

		int r;
		if (output->back == NULL) {
			drm_mode_rect rects[kMaxDamageClips];
			const uint32 clipCount = _DamageClips(present, rects,
				kMaxDamageClips, output->mode.virtual_width,
				output->mode.virtual_height, output->orientation,
				output->reflection);
			drm_clip_rect clips[kMaxDamageClips];
			for (uint32 j = 0; j < clipCount; j++) {
				clips[j].x1 = rects[j].x1;
				clips[j].y1 = rects[j].y1;
				clips[j].x2 = rects[j].x2;
				clips[j].y2 = rects[j].y2;
			}
			r = drmModeDirtyFB(output->fd, output->target->GetFbId(),
				clipCount > 0 ? clips : NULL, clipCount);
			// ENOSYS: the scanout needs no flush.
			if (r == -ENOSYS)
				r = 0;
		} else {
			pthread_mutex_lock(&fFlipMutex);
			FlipToken* token = new FlipToken;
			token->hw = this;
			token->generation = output->generation;
			token->output = output->connector;
			token->cursorOnly = false;
			fFlipTokens.push_back(token);

			r = drmModePageFlip(output->fd, output->dev->crtc,
				output->target->GetFbId(), DRM_MODE_PAGE_FLIP_EVENT, token);
			if (r != 0) {
				fFlipTokens.pop_back();
				delete token;
			} else
				output->flipPending = true;
			pthread_mutex_unlock(&fFlipMutex);
		}

		output->lastFlip = now;
		if (r == 0) {
			// The buffer leaving the screen has all but this frame.
			if (output->back != NULL) {
				output->stale = damage;
				output->stale.Include(&keepStale);
			} else
				output->stale.MakeEmpty();
		} else {
			fDirtyMutex.Lock();
			output->dirty.Include(&damage);
			fDirtyMutex.Unlock();
		}
	}

	UnlockExclusiveAccess();
}


// The cursor image in the output's sprite; false when it cannot be had.
bool
DrmHWInterface::_LoadOutputSprite(Output* output)
{
	if (output->spriteHandle == 0) {
		uint64_t width = 64;
		uint64_t height = 64;
		drmGetCap(output->fd, DRM_CAP_CURSOR_WIDTH, &width);
		drmGetCap(output->fd, DRM_CAP_CURSOR_HEIGHT, &height);

		struct drm_mode_create_dumb creq;
		memset(&creq, 0, sizeof(creq));
		creq.width = (uint32_t)width;
		creq.height = (uint32_t)height;
		creq.bpp = 32;
		if (drmIoctl(output->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0)
			return false;

		struct drm_mode_map_dumb mreq;
		memset(&mreq, 0, sizeof(mreq));
		mreq.handle = creq.handle;
		void* map = MAP_FAILED;
		if (drmIoctl(output->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) == 0) {
			map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED,
				output->fd, mreq.offset);
		}
		if (map == MAP_FAILED) {
			struct drm_mode_destroy_dumb dreq;
			memset(&dreq, 0, sizeof(dreq));
			dreq.handle = creq.handle;
			drmIoctl(output->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
			return false;
		}

		output->spriteHandle = creq.handle;
		output->spriteMap = (uint8_t*)map;
		output->spriteSize = creq.size;
		output->spriteW = (uint32_t)width;
		output->spriteH = (uint32_t)height;
	}

	const int32 cw = (int32)fCursor->Bounds().IntegerWidth() + 1;
	const int32 ch = (int32)fCursor->Bounds().IntegerHeight() + 1;
	if (cw <= 0 || ch <= 0 || cw > (int32)output->spriteW
			|| ch > (int32)output->spriteH || fCursor->Bits() == NULL)
		return false;

	// Rows are as wide as the sprite.
	memset(output->spriteMap, 0, output->spriteSize);
	const uint32_t stride = output->spriteSize / output->spriteH;
	for (int32 row = 0; row < ch; row++) {
		memcpy(output->spriteMap + row * stride,
			(const uint8*)fCursor->Bits() + row * fCursor->BytesPerRow(),
			cw * 4);
	}
	return true;
}


// Where the pointer image lies on the desktop.
BRect
DrmHWInterface::_SpriteFrame(const BPoint& where) const
{
	if (!fCursor.IsSet())
		return BRect(where, where);

	const BPoint hot = fCursor->GetHotSpot();
	const BRect bounds = fCursor->Bounds();
	const float left = floorf(where.x - hot.x);
	const float top = floorf(where.y - hot.y);
	return BRect(left, top, left + bounds.IntegerWidth(),
		top + bounds.IntegerHeight());
}


// A change of the pointer outside of a move: the sprites follow at once.
void
DrmHWInterface::_RefreshOutputSprites()
{
	if (fOutputs.empty() || !LockParallelAccess())
		return;
	_SyncOutputSprites();
	UnlockParallelAccess();
}


/*!	The pointer is shown by a hardware sprite on the output it is on, when
	that output is not rotated and the driver takes it; every other output
	hides its sprite, and the ones without blend the cursor in software.
	The hardware lock is held.
*/
void
DrmHWInterface::_SyncOutputSprites()
{
	if (fOutputs.empty())
		return;

	const bool show = fCursor.IsSet() && fCursorVisible
		&& !fDragBitmap.IsSet();
	const BPoint where = fCursorLocation;

	for (size_t i = 0; i < fOutputs.size(); i++) {
		Output* output = fOutputs[i];

		// Every output the pointer reaches into shows its part of it.
		bool want = show && output->target != NULL
			&& output->orientation == PANEL_ORIENTATION_NORMAL
			&& output->reflection == B_PANEL_REFLECTION_NONE
			&& output->Frame().Intersects(_SpriteFrame(where));

		if (want && output->spriteCursor != (const void*)fCursor.Get()) {
			output->spriteFailed = !_LoadOutputSprite(output);
			output->spriteCursor = (const void*)fCursor.Get();
			// Set again: some drivers keep the old image otherwise.
			if (!output->spriteFailed)
				output->spriteShown = false;
		}
		if (want && output->spriteFailed)
			want = false;

		bool failed = false;
		if (want) {
			const BPoint hot = fCursor->GetHotSpot();
			const int32 x = (int32)floorf(where.x - hot.x - output->origin.x);
			const int32 y = (int32)floorf(where.y - hot.y - output->origin.y);
			if (!output->spriteShown) {
				failed = drmModeSetCursor(output->fd, output->dev->crtc,
					output->spriteHandle, output->spriteW, output->spriteH)
						!= 0;
			}
			if (!failed)
				failed = drmModeMoveCursor(output->fd, output->dev->crtc,
					x, y) != 0;

			if (failed) {
				output->spriteFailed = true;
				want = false;
			} else if (!output->spriteShown) {
				output->spriteShown = true;
				// What was blended in goes.
				_AddOutputDamage((BRect)_SoftCursorFrame());
			}
		}

		if (!want && output->spriteShown) {
			drmModeSetCursor(output->fd, output->dev->crtc, 0, 0, 0);
			output->spriteShown = false;
			if (failed)
				_AddOutputDamage((BRect)_SoftCursorFrame());
		}
	}
}


// Event thread, inside drmHandleEvent() with the exclusive lock held.
void
DrmHWInterface::_OutputFlipComplete(uint32 connector, uint64_t generation)
{
	Output* output = _FindOutput(connector);
	if (output == NULL || output->generation != generation)
		return;

	std::swap(output->front, output->back);
	output->target = output->back;
	output->flipPending = false;
}


// fModesetMutex held. onlyLost leaves a CRTC that still scans out alone.
void
DrmHWInterface::_RestoreOutputs(bool onlyLost)
{
	for (size_t i = 0; i < fOutputs.size(); i++) {
		Output* output = fOutputs[i];
		struct modeset_dev* dev = output->dev;
		if (output->front == NULL)
			continue;

		if (onlyLost) {
			drmModeCrtc* crtc = drmModeGetCrtc(output->fd, dev->crtc);
			const bool active = crtc != NULL && crtc->mode_valid
				&& crtc->buffer_id != 0;
			if (crtc != NULL)
				drmModeFreeCrtc(crtc);
			if (active)
				continue;
		}

		if (drmModeSetCrtc(output->fd, dev->crtc, output->front->GetFbId(),
				0, 0, &dev->conn, 1, &dev->mode) != 0) {
			fprintf(stderr, "[drm] cannot restore connector %u: %s\n",
				dev->conn, strerror(errno));
		}
	}

	_DamageOutputs();
}


void
DrmHWInterface::_SetOutputDPMS(Output* output, uint32 state)
{
	int dpms;
	switch (state) {
		case B_DPMS_ON:			dpms = DRM_MODE_DPMS_ON; break;
		case B_DPMS_STAND_BY:	dpms = DRM_MODE_DPMS_STANDBY; break;
		case B_DPMS_SUSPEND:	dpms = DRM_MODE_DPMS_SUSPEND; break;
		case B_DPMS_OFF:		dpms = DRM_MODE_DPMS_OFF; break;
		default:
			return;
	}

	drmModeConnector* conn = drmModeGetConnectorCurrent(output->fd,
		output->rawConnector);
	if (conn == NULL)
		return;

	for (int i = 0; i < conn->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(output->fd,
			conn->props[i]);
		if (prop == NULL)
			continue;
		if (strcmp(prop->name, "DPMS") == 0) {
			if (drmModeConnectorSetProperty(output->fd,
					output->rawConnector,
					prop->prop_id, dpms) == 0)
				output->dpms = state;
		}
		drmModeFreeProperty(prop);
	}
	drmModeFreeConnector(conn);

	// Nothing was shown while it was off.
	if (output->dpms == B_DPMS_ON) {
		fDirtyMutex.Lock();
		output->dirty.Include(BRect(0, 0, output->mode.virtual_width - 1,
			output->mode.virtual_height - 1));
		fDirtyMutex.Unlock();
		_Wake();
	}
}


void
DrmHWInterface::GetConnectedOutputs(std::vector<uint32>& connectors)
{
	connectors.clear();

	// VOS_DRM_SINGLE_OUTPUT keeps to the first output.
	static const char* sSingle = getenv("VOS_DRM_SINGLE_OUTPUT");
	if (fFd < 0 || fDev == NULL || (sSingle != NULL && sSingle[0] != '\0'))
		return;

	for (size_t card = 0; card <= fCards.size(); card++) {
		const int slot = card == 0 ? 0 : fCards[card - 1].slot;
		const int fd = card == 0 ? fFd : fCards[card - 1].fd;

		drmModeRes* res = drmModeGetResources(fd);
		if (res == NULL)
			continue;

		for (int i = 0; i < res->count_connectors; i++) {
			drmModeConnector* conn = drmModeGetConnectorCurrent(fd,
				res->connectors[i]);
			if (conn == NULL)
				continue;
			if (conn->connection == DRM_MODE_CONNECTED
					&& conn->count_modes > 0
					&& (slot != 0 || conn->connector_id != fDev->conn)
					&& output_key(slot, conn->connector_id) != fStandIn) {
				const uint32 key = output_key(slot, conn->connector_id);
				fDirtyMutex.Lock();
				const bool disabled = std::find(fDisabledOutputs.begin(),
					fDisabledOutputs.end(), key) != fDisabledOutputs.end();
				fDirtyMutex.Unlock();
				if (!disabled)
					connectors.push_back(key);
			}
			drmModeFreeConnector(conn);
		}
		drmModeFreeResources(res);
	}
}


void
DrmHWInterface::ListOutputs(std::vector<OutputInfo>& outputs)
{
	outputs.clear();
	if (fFd < 0 || fDev == NULL)
		return;

	for (size_t card = 0; card <= fCards.size(); card++) {
		const int slot = card == 0 ? 0 : fCards[card - 1].slot;
		const int fd = card == 0 ? fFd : fCards[card - 1].fd;

		drmModeRes* res = drmModeGetResources(fd);
		if (res == NULL)
			continue;

		for (int i = 0; i < res->count_connectors; i++) {
			drmModeConnector* conn = drmModeGetConnectorCurrent(fd,
				res->connectors[i]);
			if (conn == NULL)
				continue;

			const bool first = output_key(slot, conn->connector_id)
				== _FirstKey();
			if (first || (conn->connection == DRM_MODE_CONNECTED
					&& conn->count_modes > 0)) {
				OutputInfo info;
				info.connector = output_key(slot, conn->connector_id);
				// Distinct across the cards: ids follow it.
				info.index = slot * 32 + i;
				info.first = first;
				info.connected = conn->connection == DRM_MODE_CONNECTED
					&& conn->count_modes > 0;
				_ConnectorNameFor(info.connector, info.name);

				fDirtyMutex.Lock();
				info.disabled = std::find(fDisabledOutputs.begin(),
					fDisabledOutputs.end(), info.connector)
						!= fDisabledOutputs.end();
				fDirtyMutex.Unlock();

				// The first one leads the list.
				if (first)
					outputs.insert(outputs.begin(), info);
				else
					outputs.push_back(info);
			}
			drmModeFreeConnector(conn);
		}
		drmModeFreeResources(res);
	}
}


// The caller has ScreenManager take the output's screen away.
void
DrmHWInterface::SetOutputDisabled(uint32 connector, bool disabled)
{
	if (fDev == NULL)
		return;

	// The first one is only noted: PromoteOutput() hands the screen to
	// another output and switches this one off then.
	const bool first = connector == fDev->conn;
	const int fd = _FdForKey(connector);

	fDirtyMutex.Lock();
	std::vector<uint32>::iterator found = std::find(fDisabledOutputs.begin(),
		fDisabledOutputs.end(), connector);
	if (disabled && found == fDisabledOutputs.end())
		fDisabledOutputs.push_back(connector);
	else if (!disabled && found != fDisabledOutputs.end())
		fDisabledOutputs.erase(found);
	const bool shown = _FindOutput(connector) != NULL;
	fDirtyMutex.Unlock();

	// Not ours yet: whatever lit it before us is still on it.
	if (disabled && !shown && !first) {
		ModesetGuard modesetGuard(&fModesetMutex);
		for (struct modeset_dev* iter = get_dev_for(fd); iter;
				iter = iter->next) {
			if (iter->conn == key_connector(connector))
				drmModeSetCrtc(fd, iter->crtc, 0, 0, 0, NULL, 0, NULL);
		}
	}
}


// The new output starts to the right of the others; its screen owns the
// returned interface.
HWInterface*
DrmHWInterface::AddOutput(uint32 connector)
{
	if (fFd < 0 || !fInitialized || fDev == NULL || connector == fDev->conn)
		return NULL;

	const int fd = _FdForKey(connector);
	const uint32 raw = key_connector(connector);
	if (fd < 0)
		return NULL;

	if (!LockExclusiveAccess())
		return NULL;

	ModesetGuard modesetGuard(&fModesetMutex);

	if (_FindOutput(connector) != NULL) {
		UnlockExclusiveAccess();
		return NULL;
	}

	struct modeset_dev* dev = NULL;
	for (int pass = 0; pass < 2 && dev == NULL; pass++) {
		for (struct modeset_dev* iter = get_dev_for(fd); iter;
				iter = iter->next) {
			if (iter->conn == raw)
				dev = iter;
		}
		if (dev == NULL && pass == 0)
			modeset_add_connector(fd, raw);
	}

	if (dev == NULL) {
		// Every output needs a CRTC of its own; say so once.
		if (std::find(fSkippedOutputs.begin(), fSkippedOutputs.end(),
				connector) == fSkippedOutputs.end()) {
			fSkippedOutputs.push_back(connector);
			fprintf(stderr, "DRM: no free CRTC for connector %u, output "
				"skipped\n", connector);
		}
		UnlockExclusiveAccess();
		return NULL;
	}

	Output* output = new(std::nothrow) Output();
	if (output == NULL) {
		UnlockExclusiveAccess();
		return NULL;
	}

	fSkippedOutputs.erase(std::remove(fSkippedOutputs.begin(),
		fSkippedOutputs.end(), connector), fSkippedOutputs.end());

	output->connector = connector;
	output->rawConnector = raw;
	output->fd = fd;
	output->slot = key_slot(connector);
	output->dev = dev;
	output->front = NULL;
	output->back = NULL;
	output->target = NULL;
	output->origin = BPoint(_DesktopBounds().right + 1, 0);
	output->orientation = PANEL_ORIENTATION_NORMAL;
	output->reflection = B_PANEL_REFLECTION_NONE;
	output->dpms = B_DPMS_ON;
	output->flipPending = false;
	output->generation = 0;
	output->lastFlip = 0;
	output->modeChanged = false;
	output->spriteHandle = 0;
	output->spriteMap = NULL;
	output->spriteSize = 0;
	output->spriteW = 0;
	output->spriteH = 0;
	output->spriteCursor = NULL;
	output->spriteFailed = false;
	output->spriteShown = false;

	if (_SetupOutput(output) != B_OK) {
		delete output;
		UnlockExclusiveAccess();
		return NULL;
	}

	DrmOutput* interface = new(std::nothrow) DrmOutput(this, connector);
	if (interface == NULL) {
		_TeardownOutput(output);
		delete output;
		UnlockExclusiveAccess();
		return NULL;
	}

	// The painter draws into the render buffer from here on.
	if (fDirectRender) {
		_DrainPendingFlip();
		_LeaveDirectRender();
	}

	fDirtyMutex.Lock();
	fOutputs.push_back(output);
	fDirtyMutex.Unlock();
	_DiscoverOutputPlanes(output);

	_RebuildRenderBuffer(fDisplayMode.virtual_width,
		fDisplayMode.virtual_height);
	_DamageOutputs();
	_NotifyFrameBufferChanged();

	fprintf(stderr, "DRM: output on connector %u, %ux%u at %d,%d\n",
		connector, output->mode.virtual_width, output->mode.virtual_height,
		(int)output->origin.x, (int)output->origin.y);

	UnlockExclusiveAccess();
	_Wake();
	return interface;
}


void
DrmHWInterface::RemoveOutput(uint32 connector)
{
	if (!LockExclusiveAccess())
		return;

	ModesetGuard modesetGuard(&fModesetMutex);

	Output* output = _FindOutput(connector);
	if (output == NULL) {
		UnlockExclusiveAccess();
		return;
	}

	// Its planes go dark before the monitor does.
	_ReleaseOverlayPlanes(connector);

	fDirtyMutex.Lock();
	for (size_t i = 0; i < fOutputs.size(); i++) {
		if (fOutputs[i] == output) {
			fOutputs.erase(fOutputs.begin() + i);
			break;
		}
	}
	fDirtyMutex.Unlock();

	// Its node restores the CRTC it found; a monitor that is still there
	// would light up with that.
	const uint32_t crtc = output->dev->crtc;
	const int fd = output->fd;
	const uint32_t raw = output->rawConnector;
	_TeardownOutput(output);
	delete output;
	modeset_remove_connector(fd, raw);
	drmModeSetCrtc(fd, crtc, 0, 0, 0, NULL, 0, NULL);

	_RebuildRenderBuffer(fDisplayMode.virtual_width,
		fDisplayMode.virtual_height);
	_NotifyFrameBufferChanged();

	fprintf(stderr, "DRM: output on connector %u removed\n", connector);

	UnlockExclusiveAccess();
}


bool
DrmHWInterface::IsInternalConnector(uint32 connector)
{
	const int fd = _FdForKey(connector);
	connector = key_connector(connector);
	bool internal = false;
	drmModeConnector* conn = drmModeGetConnectorCurrent(fd, connector);
	if (conn != NULL) {
		internal = conn->connector_type == DRM_MODE_CONNECTOR_eDP
			|| conn->connector_type == DRM_MODE_CONNECTOR_LVDS
			|| conn->connector_type == DRM_MODE_CONNECTOR_DSI;
		drmModeFreeConnector(conn);
	}
	return internal;
}


bool
DrmHWInterface::IsInternalPanel() const
{
	return fFd >= 0 && fDev != NULL
		&& const_cast<DrmHWInterface*>(this)->IsInternalConnector(
			_FirstKey());
}


/*!	The first output stays while it is usable. When its monitor is gone, or
	the user switched it off, an output that is still there takes over: the
	internal panel if it is one, else the lowest connector.
*/
uint32
DrmHWInterface::OutputToPromote()
{
	if (fFd < 0 || !fInitialized || fDev == NULL || _FirstUsable())
		return 0;

	std::vector<uint32> candidates;
	fDirtyMutex.Lock();
	for (size_t i = 0; i < fOutputs.size(); i++) {
		// The first output stays on its card.
		if (fOutputs[i]->slot == 0
			&& std::find(fDisabledOutputs.begin(), fDisabledOutputs.end(),
				fOutputs[i]->connector) == fDisabledOutputs.end())
			candidates.push_back(fOutputs[i]->connector);
	}
	fDirtyMutex.Unlock();

	uint32 best = 0;
	bool bestInternal = false;
	for (size_t i = 0; i < candidates.size(); i++) {
		const bool internal = IsInternalConnector(candidates[i]);
		if (best == 0 || (internal && !bestInternal)
				|| (internal == bestInternal && candidates[i] < best)) {
			best = candidates[i];
			bestInternal = internal;
		}
	}

	return best;
}


// The first output has a monitor and is not switched off.
bool
DrmHWInterface::_FirstUsable()
{
	bool usable = false;
	drmModeConnector* conn = drmModeGetConnectorCurrent(fFd, fDev->conn);
	if (conn != NULL) {
		usable = conn->connection == DRM_MODE_CONNECTED
			&& conn->count_modes > 0;
		drmModeFreeConnector(conn);
	}

	fDirtyMutex.Lock();
	if (std::find(fDisabledOutputs.begin(), fDisabledOutputs.end(),
			(uint32)fDev->conn) != fDisabledOutputs.end())
		usable = false;
	fDirtyMutex.Unlock();
	return usable;
}


// The connector the first screen shows.
uint32
DrmHWInterface::_FirstKey() const
{
	return fStandIn != 0 ? fStandIn : fDev->conn;
}


/*!	The first output has nothing left on its card, and an output of another
	card is there: the lowest connector, the internal panel first.
*/
uint32
DrmHWInterface::OutputToAdopt()
{
	if (fFd < 0 || !fInitialized || fDev == NULL || fStandIn != 0
			|| _FirstUsable())
		return 0;

	std::vector<uint32> candidates;
	bool sameCard = false;
	fDirtyMutex.Lock();
	for (size_t i = 0; i < fOutputs.size(); i++) {
		if (std::find(fDisabledOutputs.begin(), fDisabledOutputs.end(),
				fOutputs[i]->connector) != fDisabledOutputs.end())
			continue;
		if (fOutputs[i]->slot == 0)
			sameCard = true;
		candidates.push_back(fOutputs[i]->connector);
	}
	fDirtyMutex.Unlock();

	// Outputs of the first card are promoted instead.
	uint32 best = 0;
	bool bestInternal = false;
	for (size_t i = 0; !sameCard && i < candidates.size(); i++) {
		const bool internal = IsInternalConnector(candidates[i]);
		if (best == 0 || (internal && !bestInternal)
				|| (internal == bestInternal && candidates[i] < best)) {
			best = candidates[i];
			bestInternal = internal;
		}
	}
	return best;
}


void
DrmHWInterface::_BindBacklight()
{
	if (fBacklight != NULL) {
		backlight_destroy(fBacklight);
		fBacklight = NULL;
	}

	if (fUdev == NULL || fDev == NULL)
		return;

	struct stat st;
	if (fstat(fFd, &st) != 0)
		return;

	struct udev_device* udevDev = udev_device_new_from_devnum(fUdev, 'c',
		st.st_rdev);
	if (udevDev == NULL)
		return;

	drmModeConnector* conn = drmModeGetConnector(fFd, fDev->conn);
	if (conn != NULL) {
		fBacklight = backlight_init(udevDev, conn->connector_type);
		drmModeFreeConnector(conn);
	}
	udev_device_unref(udevDev);
}


/*!	The output on \a connector takes over as the first one, when that went
	away or was switched off. The scanout of the first one is rebuilt on its
	node, the old node and its screen are dropped; the output keeps its
	place on the desktop and its rotation.
*/
bool
DrmHWInterface::PromoteOutput(uint32 connector)
{
	if (fFd < 0 || !fInitialized || fDev == NULL || connector == fDev->conn
			|| key_slot(connector) != 0)
		return false;

	if (!LockExclusiveAccess())
		return false;

	ModesetGuard modesetGuard(&fModesetMutex);

	Output* output = _FindOutput(connector);
	if (output == NULL) {
		UnlockExclusiveAccess();
		return false;
	}

	_DrainPendingFlip();
	if (fDirectRender)
		_LeaveDirectRender();
	_DisableHardwareCursor();

	struct modeset_dev* old = fDev;
	struct modeset_dev* dev = output->dev;
	const uint32 oldConnector = old->conn;
	const uint32_t oldCrtc = old->crtc;
	const BPoint origin = output->origin;
	display_mode mode = output->mode;

	fDirtyMutex.Lock();
	for (size_t i = 0; i < fOutputs.size(); i++) {
		if (fOutputs[i] == output) {
			fOutputs.erase(fOutputs.begin() + i);
			break;
		}
	}
	fDirtyMutex.Unlock();

	fUserOrientation = output->orientation;
	fPanelReflection = output->reflection;
	_TeardownOutput(output);
	delete output;

	// The buffers of the old first one wrap its node, which goes now; a
	// flip still in flight completes against the bumped generation.
	pthread_mutex_lock(&fFlipMutex);
	fFlipGeneration++;
	delete fFrontBuffer;
	fFrontBuffer = NULL;
	delete fBackBuffer;
	fBackBuffer = NULL;
	fWriteTarget = NULL;
	fPageFlipPending = false;
	pthread_mutex_unlock(&fFlipMutex);

	fDev = dev;
	fOrigin = origin;

	drmModeSetCrtc(fFd, oldCrtc, 0, 0, 0, NULL, 0, NULL);
	modeset_remove_connector(fFd, oldConnector);

	// Everything the first output derived from its node.
	_DiscoverPanelOrientation();
	fPrimaryPlaneId = 0;
	fCursorPlaneId = 0;
	_ReleaseOverlayPlanes(0);
	_ReleaseOverlayPlanes(connector);
	if (fAtomicSupported)
		_DiscoverProperties();

	modeset_create_cursor_fb(fFd, dev);
	fCursorUsesAtomic = false;
	_ProbeCursor();
	_BindBacklight();

	uint32_t logW, logH;
	_ApplyOrientationSwap(dev->width, dev->height, logW, logH);
	mode.virtual_width = logW;
	mode.virtual_height = logH;

	// Builds the scanout buffers and programs the CRTC, as for a mode change.
	status_t status = SetMode(mode);
	if (status != B_OK) {
		fprintf(stderr, "DRM: cannot scan out connector %u as the first "
			"output (%s)\n", connector, strerror(status));
	} else {
		fprintf(stderr, "DRM: connector %u is the first output now\n",
			connector);
	}

	UnlockExclusiveAccess();
	_Wake();
	return status == B_OK;
}


// Exclusive lock held. The first screen takes the place and mode of the
// output that stands in for it.
void
DrmHWInterface::_SyncStandIn(Output* output)
{
	fDirtyMutex.Lock();
	fDisplayMode = output->mode;
	fOrigin = output->origin;
	fDirtyMutex.Unlock();

	_RebuildRenderBuffer(fDisplayMode.virtual_width,
		fDisplayMode.virtual_height);
	_DamageOutputs();
	_NotifyFrameBufferChanged();
}


/*!	The first output is switched off, and the desktop goes on at the output
	of another card, which keeps presenting through its own buffers. The
	first screen shows that monitor from now on and its own scanout is
	parked, so nothing is flipped to a CRTC without a monitor.
*/
bool
DrmHWInterface::AdoptOutput(uint32 connector)
{
	if (fFd < 0 || !fInitialized || fDev == NULL || fStandIn != 0
			|| key_slot(connector) == 0)
		return false;

	if (!LockExclusiveAccess())
		return false;

	ModesetGuard modesetGuard(&fModesetMutex);

	Output* output = _FindOutput(connector);
	if (output == NULL) {
		UnlockExclusiveAccess();
		return false;
	}

	_DrainPendingFlip();
	if (fDirectRender)
		_LeaveDirectRender();
	_DisableHardwareCursor();
	fHardwareCursorEnabled = false;
	_ReleaseOverlayPlanes(0, false);

	drmModeSetCrtc(fFd, fDev->crtc, 0, 0, 0, NULL, 0, NULL);

	fDirtyMutex.Lock();
	fStandIn = connector;
	fAccumulatedDirty.MakeEmpty();
	fBackStale.MakeEmpty();
	fNeedsFlip = false;
	fStagedCursor.valid = false;
	fStagedPlanes.clear();
	fDirtyMutex.Unlock();

	_SyncStandIn(output);

	fprintf(stderr, "DRM: the first card has no output left, connector %u "
		"of card%d shows the desktop\n", key_connector(connector),
		_CardIndexForSlot(key_slot(connector)));

	UnlockExclusiveAccess();
	_Wake();
	return true;
}


/*!	The first output has its monitor back, or the adopted output is gone or
	switched off: the first screen scans out on its own again. The adopted
	output is dropped; ScreenManager gives it a screen again if it stays.
*/
bool
DrmHWInterface::ReleaseStandIn()
{
	if (fStandIn == 0 || fFd < 0 || !fInitialized || fDev == NULL)
		return false;

	bool lost = true;
	fDirtyMutex.Lock();
	Output* output = _FindOutput(fStandIn);
	const bool disabled = std::find(fDisabledOutputs.begin(),
		fDisabledOutputs.end(), fStandIn) != fDisabledOutputs.end();
	if (output != NULL && !disabled) {
		drmModeConnector* conn = drmModeGetConnectorCurrent(output->fd,
			output->rawConnector);
		if (conn != NULL) {
			lost = conn->connection != DRM_MODE_CONNECTED
				|| conn->count_modes == 0;
			drmModeFreeConnector(conn);
		}
	}
	fDirtyMutex.Unlock();

	const bool usable = _FirstUsable();
	if (!usable && !lost && OutputToPromote() == 0)
		return false;

	if (!LockExclusiveAccess())
		return false;

	ModesetGuard modesetGuard(&fModesetMutex);

	const uint32 standIn = fStandIn;
	fStandIn = 0;
	RemoveOutput(standIn);

	display_mode mode;
	if (!usable || _PreferredModeFor(fDev->conn, fPanelOrientation, true,
			&mode) != B_OK || SetMode(mode) != B_OK) {
		// Back on the old scanout, as it was before.
		uint32_t logW, logH;
		_ApplyOrientationSwap(fDev->width, fDev->height, logW, logH);
		_FillModeInfo(fDisplayMode, fDev->mode);
		fDisplayMode.virtual_width = logW;
		fDisplayMode.virtual_height = logH;
		_RebuildRenderBuffer(logW, logH);
		_RestoreDisplay();
		_DamageAll();
		fDirtyMutex.Lock();
		fNeedsFlip = fPageFlipEnabled;
		fDirtyMutex.Unlock();
		if (fCursor.IsSet())
			SetCursor(fCursor);
		_NotifyFrameBufferChanged();
	}

	fprintf(stderr, "DRM: the first output scans out again\n");

	UnlockExclusiveAccess();
	_Wake();
	return true;
}


bool
DrmHWInterface::TakeOutputModeChange(uint32 connector)
{
	bool changed = false;
	fDirtyMutex.Lock();
	Output* output = _FindOutput(connector);
	if (output != NULL) {
		changed = output->modeChanged;
		output->modeChanged = false;
	}
	fDirtyMutex.Unlock();
	return changed;
}


status_t
DrmHWInterface::SetOutputMode(uint32 connector, const display_mode& mode)
{
	if (fFd < 0)
		return B_ERROR;

	if (!LockExclusiveAccess())
		return B_ERROR;

	ModesetGuard modesetGuard(&fModesetMutex);

	Output* output = _FindOutput(connector);
	if (output == NULL) {
		UnlockExclusiveAccess();
		return B_BAD_VALUE;
	}

	struct modeset_dev* dev = output->dev;
	drmModeConnector* conn = drmModeGetConnector(output->fd, dev->conn);
	if (conn == NULL) {
		UnlockExclusiveAccess();
		return B_ERROR;
	}

	// The request is logical; the connector advertises physical modes.
	const bool swapped = output->orientation == PANEL_ORIENTATION_LEFT_UP
		|| output->orientation == PANEL_ORIENTATION_RIGHT_UP;
	const uint32_t physW = swapped ? mode.virtual_height : mode.virtual_width;
	const uint32_t physH = swapped ? mode.virtual_width : mode.virtual_height;

	drmModeModeInfo* found = find_connector_mode(conn, physW, physH, mode);
	if (found == NULL) {
		drmModeFreeConnector(conn);
		UnlockExclusiveAccess();
		return B_BAD_VALUE;
	}

	_ReleaseOverlayPlanes(connector, false);
	_TeardownOutput(output);

	if (dev->fb) {
		drmModeRmFB(output->fd, dev->fb);
		dev->fb = 0;
	}
	if (dev->back_fb) {
		drmModeRmFB(output->fd, dev->back_fb);
		dev->back_fb = 0;
	}

	dev->width = found->hdisplay;
	dev->height = found->vdisplay;
	memcpy(&dev->mode, found, sizeof(*found));
	drmModeFreeConnector(conn);

	status_t status = modeset_create_fb(output->fd, dev) == 0 ? B_OK : B_ERROR;
	if (status == B_OK)
		status = _SetupOutput(output);

	_RebuildRenderBuffer(fDisplayMode.virtual_width,
		fDisplayMode.virtual_height);
	if (status == B_OK)
		_ClearBufferToBlack(&output->source);
	_DamageAll();
	_DamageOutputs();
	_NotifyFrameBufferChanged();

	UnlockExclusiveAccess();
	_Wake();
	return status;
}


status_t
DrmHWInterface::GetOutputMode(uint32 connector, display_mode* mode)
{
	status_t status = B_BAD_VALUE;
	fDirtyMutex.Lock();
	Output* output = _FindOutput(connector);
	if (output != NULL) {
		*mode = output->mode;
		status = B_OK;
	}
	fDirtyMutex.Unlock();
	return status;
}


status_t
DrmHWInterface::GetOutputPreferredMode(uint32 connector, display_mode* mode)
{
	return _PreferredModeFor(connector, OutputOrientation(connector), true,
		mode);
}


status_t
DrmHWInterface::GetOutputModeList(uint32 connector, display_mode** _modeList,
	uint32* _count)
{
	return _ModeListFor(connector, OutputOrientation(connector), _modeList,
		_count);
}


status_t
DrmHWInterface::GetOutputMonitorInfo(uint32 connector, monitor_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;
	return _MonitorInfoFor(connector, info);
}


status_t
DrmHWInterface::GetOutputConnectorName(uint32 connector, BString& name)
{
	return _ConnectorNameFor(connector, name);
}


BPoint
DrmHWInterface::Origin() const
{
	return fOrigin;
}


status_t
DrmHWInterface::SetOrigin(BPoint origin)
{
	origin.x = floorf(origin.x);
	origin.y = floorf(origin.y);
	if (origin.x < 0 || origin.y < 0)
		return B_BAD_VALUE;

	if (!LockExclusiveAccess())
		return B_ERROR;

	if (origin != fOrigin) {
		fOrigin = origin;
		if (fStandIn != 0) {
			Output* standIn = _FindOutput(fStandIn);
			if (standIn != NULL) {
				fDirtyMutex.Lock();
				standIn->origin = origin;
				fDirtyMutex.Unlock();
			}
		}
		_RebuildRenderBuffer(fDisplayMode.virtual_width,
			fDisplayMode.virtual_height);
		_DamageAll();
		_DamageOutputs();
		_NotifyFrameBufferChanged();

		// The sprite's place is relative to this output.
		if (fCursor.IsSet())
			SetCursor(fCursor);
	}

	UnlockExclusiveAccess();
	_Wake();
	return B_OK;
}


BPoint
DrmHWInterface::OutputOrigin(uint32 connector)
{
	BPoint origin(B_ORIGIN);
	fDirtyMutex.Lock();
	Output* output = _FindOutput(connector);
	if (output != NULL)
		origin = output->origin;
	fDirtyMutex.Unlock();
	return origin;
}


status_t
DrmHWInterface::SetOutputOrigin(uint32 connector, BPoint origin)
{
	origin.x = floorf(origin.x);
	origin.y = floorf(origin.y);
	if (origin.x < 0 || origin.y < 0)
		return B_BAD_VALUE;

	if (!LockExclusiveAccess())
		return B_ERROR;

	Output* output = _FindOutput(connector);
	if (output == NULL) {
		UnlockExclusiveAccess();
		return B_BAD_VALUE;
	}

	if (origin != output->origin) {
		fDirtyMutex.Lock();
		output->origin = origin;
		fDirtyMutex.Unlock();

		_RebuildRenderBuffer(fDisplayMode.virtual_width,
			fDisplayMode.virtual_height);
		_DamageAll();
		_DamageOutputs();
		_NotifyFrameBufferChanged();
	}

	UnlockExclusiveAccess();
	_Wake();
	return B_OK;
}


int32
DrmHWInterface::OutputOrientation(uint32 connector)
{
	int32 orientation = PANEL_ORIENTATION_NORMAL;
	fDirtyMutex.Lock();
	Output* output = _FindOutput(connector);
	if (output != NULL)
		orientation = output->orientation;
	fDirtyMutex.Unlock();
	return orientation;
}


// The caller tells the screen's listeners.
status_t
DrmHWInterface::SetOutputOrientation(uint32 connector, int32 orientation)
{
	if (orientation < -1 || orientation > PANEL_ORIENTATION_RIGHT_UP)
		return B_BAD_VALUE;
	if (orientation < 0)
		orientation = PANEL_ORIENTATION_NORMAL;

	if (!LockExclusiveAccess())
		return B_ERROR;

	Output* output = _FindOutput(connector);
	if (output == NULL) {
		UnlockExclusiveAccess();
		return B_BAD_VALUE;
	}

	if (orientation == output->orientation) {
		UnlockExclusiveAccess();
		return B_OK;
	}

	const int32 previous = output->orientation;
	output->orientation = orientation;

	// The panel keeps its shape: the logical size follows the rotation.
	const bool swapped = orientation == PANEL_ORIENTATION_LEFT_UP
		|| orientation == PANEL_ORIENTATION_RIGHT_UP;
	display_mode mode = output->mode;
	mode.virtual_width = swapped ? output->dev->height : output->dev->width;
	mode.virtual_height = swapped ? output->dev->width : output->dev->height;

	status_t status = SetOutputMode(connector, mode);
	if (status != B_OK)
		output->orientation = previous;

	UnlockExclusiveAccess();
	return status;
}


int32
DrmHWInterface::OutputReflection(uint32 connector)
{
	int32 reflection = B_PANEL_REFLECTION_NONE;
	fDirtyMutex.Lock();
	Output* output = _FindOutput(connector);
	if (output != NULL)
		reflection = output->reflection;
	fDirtyMutex.Unlock();
	return reflection;
}


status_t
DrmHWInterface::SetOutputReflection(uint32 connector, int32 reflection)
{
	if (reflection < 0 || reflection > B_PANEL_REFLECTION_BOTH)
		return B_BAD_VALUE;

	if (!LockExclusiveAccess())
		return B_ERROR;

	Output* output = _FindOutput(connector);
	if (output == NULL) {
		UnlockExclusiveAccess();
		return B_BAD_VALUE;
	}

	output->reflection = reflection;
	_DamageOutputs();

	UnlockExclusiveAccess();
	_Wake();
	return B_OK;
}


uint32
DrmHWInterface::OutputDPMSMode(uint32 connector)
{
	uint32 state = B_DPMS_ON;
	fDirtyMutex.Lock();
	Output* output = _FindOutput(connector);
	if (output != NULL)
		state = output->dpms;
	fDirtyMutex.Unlock();
	return state;
}


status_t
DrmHWInterface::SetOutputDPMSMode(uint32 connector, uint32 state)
{
	if (!LockExclusiveAccess())
		return B_ERROR;

	status_t status = B_BAD_VALUE;
	Output* output = _FindOutput(connector);
	if (output != NULL) {
		_SetOutputDPMS(output, state);
		status = output->dpms == state ? B_OK : B_ERROR;
	}

	UnlockExclusiveAccess();
	return status;
}


void
DrmHWInterface::_OnSessionDisable()
{
	printf("Session disabled\n");

	_DrainPendingFlip();

	fSessionActive = false;

	// The VT owner is switching away: any completion that still lands is
	// for buffers we are about to stop driving, so invalidate it.
	pthread_mutex_lock(&fFlipMutex);
	fFlipGeneration++;
	fPageFlipPending = false;
	pthread_mutex_unlock(&fFlipMutex);

	fDirtyMutex.Lock();
	fAccumulatedDirty.MakeEmpty();
	fBackStale.MakeEmpty();
	fNeedsFlip = false;
	for (size_t i = 0; i < fOutputs.size(); i++) {
		fOutputs[i]->generation++;
		fOutputs[i]->flipPending = false;
		fOutputs[i]->dirty.MakeEmpty();
		fOutputs[i]->stale.MakeEmpty();
	}
	fDirtyMutex.Unlock();

	// Clear while still DRM master, or the next VT/session owner scans
	// out our stale sprite.
	_DisableHardwareCursor();

	// Same for overlay planes; geometry is kept for re-arm on resume.
	pthread_mutex_lock(&fOverlayMutex);
	for (size_t i = 0; i < fOverlayPlanes.size(); i++) {
		if (fOverlayPlanes[i].claimed != NULL)
			fOverlayPlanes[i].claimed->Disable();
	}
	pthread_mutex_unlock(&fOverlayMutex);

	if (fFd >= 0 && drmDropMaster(fFd) != 0) {
		static bool sReported = false;
		if (!sReported) {
			fprintf(stderr, "[drm] drmDropMaster failed (%s)\n",
				strerror(errno));
			sReported = true;
		}
	}

	for (size_t i = 0; i < fCards.size(); i++)
		drmDropMaster(fCards[i].fd);

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

	// Iterates the modeset list; a concurrent udev rescan can free nodes.
	ModesetGuard modesetGuard(&fModesetMutex);
	// Same window as SetMode: a concurrent flip EINVAL here is the
	// modeset, not a broken flip.
	fModesetActive = true;
	struct modeset_dev *iter = fStandIn == 0 ? fDev : NULL;
	if (iter != NULL) {
		// Not iter->fb: stale after any flip, zeroed by resize.
		// fFrontBuffer is what the CRTC was actually scanning out.
		// Snapshot it under the flip mutex: the completion handler swaps
		// the pointer there without holding the mode lock.
		uint32_t fb;
		pthread_mutex_lock(&fFlipMutex);
		fb = fFrontBuffer != NULL ? fFrontBuffer->GetFbId() : iter->fb;
		pthread_mutex_unlock(&fFlipMutex);

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
	_RestoreOutputs();
	fModesetActive = false;
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
	// Runs on the event thread inside drmHandleEvent(). SetMode() may tear
	// the buffers down concurrently: it holds the mode (write) lock and
	// waits for us in _DrainPendingFlip(). So the swap is guarded by
	// fFlipMutex and stamped with the flip generation; SetMode() bumps the
	// generation before deleting, and a completion for an older generation
	// is discarded instead of swapping the replacement buffers.
	FlipToken* token = static_cast<FlipToken*>(data);
	DrmHWInterface* hw = token->hw;
	const uint64_t generation = token->generation;
	const uint32_t output = token->output;

	const bigtime_t when = sec != 0 || usec != 0
		? (bigtime_t)sec * 1000000 + usec : system_time();

	pthread_mutex_lock(&hw->fFlipMutex);
	for (size_t i = 0; i < hw->fFlipTokens.size(); i++) {
		if (hw->fFlipTokens[i] == token) {
			hw->fFlipTokens.erase(hw->fFlipTokens.begin() + i);
			break;
		}
	}
	if (token->cursorOnly) {
		delete token;
		hw->fCursorInFlightAt = 0;
		pthread_mutex_unlock(&hw->fFlipMutex);
		hw->_Wake();
		return;
	}
	hw->fLastFlipDone = system_time();
	std::vector<PlaneNote> planes;
	planes.swap(token->planes);
	delete token;

	if (output != 0) {
		pthread_mutex_unlock(&hw->fFlipMutex);
		hw->_OutputFlipComplete(output, generation);
		return;
	}

	if (generation != hw->fFlipGeneration) {
		// Stale completion for a flip whose buffers are already gone.
		pthread_mutex_unlock(&hw->fFlipMutex);
		hw->_NotifyPlanes(planes, true, when);
		return;
	}

	// Back at once means no vblank behind the event.
	const bigtime_t took = system_time() - hw->fLastFlipIssue;
	if (took >= hw->_FramePeriod() / 4)
		hw->fFlipVblank = true;
	else if (took < 1000)
		hw->fFlipVblank = false;

	hw->_FinishFrameSwap();
	hw->_NotifyPlanes(planes, true, when);
}


// Called with fFlipMutex held; releases it.
void
DrmHWInterface::_FinishFrameSwap()
{
	if (!fDamageFlush) {
		std::swap(fFrontBuffer, fBackBuffer);
		fWriteTarget = fBackBuffer;
	}
	fPageFlipPending = false;
	pthread_cond_broadcast(&fFlipCond);

	if (fRenderBuffer != NULL && fDirectRender) {
		_DirectFlipComplete();
		return;
	}

	pthread_mutex_unlock(&fFlipMutex);
}


void
DrmHWInterface::_ComposeFrame(const BRegion& region)
{
	RenderingBuffer* target = fWriteTarget;
	if (fRenderBuffer == NULL || target == NULL)
		return;

	const int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++)
		_BlitRect(&fSource, target, region.RectAt(i));

	if (fHardwareCursorEnabled)
		return;

	bool overlaysLocked = fFloatingOverlaysLock.Lock();
	IntRect cursorFrame = _CursorFrame();
	if (cursorFrame.IsValid()) {
		cursorFrame.OffsetBy(-(int32)fOrigin.x, -(int32)fOrigin.y);
		if (region.Intersects((BRect)cursorFrame)) {
			_BlendCursor(&fSource, target, cursorFrame, fOrigin,
				fPanelOrientation, fPanelReflection);
		}
	}
	if (overlaysLocked)
		fFloatingOverlaysLock.Unlock();
}


// Called with fFlipMutex held and the exclusive lock held by this thread;
// releases the mutex. The painter wrote the frame into the buffer that was
// just scanned out, so only the region the new write target is missing is
// copied from the front buffer.
void
DrmHWInterface::_DirectFlipComplete()
{
	const int32 presentedSlot = _AgeSlotForFb(fFrontBuffer != NULL
		? fFrontBuffer->GetFbId() : 0);
	const int32 writeSlot = _AgeSlotForFb(fWriteTarget != NULL
		? fWriteTarget->GetFbId() : 0);

	BRegion stale;
	fDirtyMutex.Lock();
	fAgeTracker.AddDamage(fAccumulatedDirty);
	const int32 pending = fAccumulatedDirty.CountRects();
	fAccumulatedDirty.MakeEmpty();
	fBackStale.MakeEmpty();
	fFrameSerial++;
	fAgeTracker.CommitFrame(fFrameSerial, presentedSlot, writeSlot, &stale);
	// Damage that arrived while the flip was in flight needs a follow-up.
	fNeedsFlip = pending > 0;
	fDirtyMutex.Unlock();

	if (fAgeTracker.AgeOf(writeSlot) < 0) {
		stale.MakeEmpty();
		stale.Include(BRect(0, 0, fDisplayMode.virtual_width - 1,
			fDisplayMode.virtual_height - 1));
	}

	RenderingBuffer* front = fFrontBuffer;
	RenderingBuffer* target = fWriteTarget;
	pthread_mutex_unlock(&fFlipMutex);

	if (front != NULL && target != NULL) {
		const int32 count = stale.CountRects();
		for (int32 i = 0; i < count; i++)
			_BlitRect(front, target, stale.RectAt(i));
	}

	_NotifyFrameBufferChanged();

	if (fPacingLockHeld && find_thread(NULL) == fEventThread) {
		fPacingLockHeld = false;
		UnlockExclusiveAccess();
	}
}


// Less one millisecond, so a flip event that lands on a real vblank never
// misses the next one.
bigtime_t
DrmHWInterface::_FramePeriod() const
{
	return _FramePeriodFor(fDisplayMode);
}


bigtime_t
DrmHWInterface::_FramePeriodFor(const display_mode& mode)
{
	const display_timing& t = mode.timing;
	bigtime_t period = 16667;
	if (t.pixel_clock > 0 && t.h_total > 0 && t.v_total > 0) {
		period = (bigtime_t)t.h_total * t.v_total * 1000
			/ t.pixel_clock;
	}
	return period > 2000 ? period - 1000 : period;
}


int32
DrmHWInterface::_AgeSlotForFb(uint32_t fbId) const
{
	for (int32 i = 0; i < fAgeSlotCount; i++) {
		if (fAgeFbIds[i] == fbId)
			return i;
	}
	return -1;
}


// Armed only for page flipped double buffering with a cursor plane: a
// software cursor would be blended into the targets and trail.
void
DrmHWInterface::_SetupDirectRender()
{
	fDirectRender = false;
	fAgeSlotCount = 0;
	memset(fAgeFbIds, 0, sizeof(fAgeFbIds));

	static const char* sDirect = getenv("VOS_DRM_DIRECT_RENDER");
	if (sDirect == NULL || sDirect[0] != '1')
		return;

	if (!fPageFlipEnabled || fRenderBuffer == NULL || fBackBuffer == NULL
			|| fFrontBuffer == NULL || fVblankPacedFlip || !_SingleOutput())
		return;

	if (!_HardwareCursorUsable() || !fHardwareCursorEnabled)
		return;

	fAgeFbIds[0] = fFrontBuffer->GetFbId();
	fAgeFbIds[1] = fBackBuffer->GetFbId();
	fAgeSlotCount = 2;
	fAgeTracker.Init(2);
	fFrameSerial = 0;

	fDirectRender = true;
	printf("[drm] direct render into scanout buffers\n");
}


// Event thread, exclusive lock held, no flip pending. Hands the frame back
// to the render buffer when the hardware cursor is lost.
void
DrmHWInterface::_LeaveDirectRender()
{
	fDirectRender = false;

	const BRect full(0, 0, fDisplayMode.virtual_width - 1,
		fDisplayMode.virtual_height - 1);
	_BlitRect(fFrontBuffer, fRenderBuffer, full);
	_BlitRect(fRenderBuffer, fWriteTarget, full);

	fDirtyMutex.Lock();
	fAccumulatedDirty.Include(full);
	fBackStale.MakeEmpty();
	fNeedsFlip = true;
	fDirtyMutex.Unlock();

	_NotifyFrameBufferChanged();
}


// P1 fallback: dumb buffers exist but no page flip ever completes, so the
// flip block would retry into eternity and the screen freezes on the boot
// frame. Pacing a SetCrtc on vblank trades tearing for a usable display.
bool
DrmHWInterface::_VblankPacedFlip()
{
	if (fFd < 0 || fWriteTarget == NULL)
		return false;

	WaitForRetrace(B_INFINITE_TIMEOUT);

	struct modeset_dev* dev = fDev;
	if (dev == NULL)
		return false;

	int r = drmModeSetCrtc(fFd, dev->crtc, fWriteTarget->GetFbId(),
		0, 0, &dev->conn, 1, &dev->mode);
	if (r != 0) {
		static bool sReported = false;
		if (!sReported) {
			fprintf(stderr, "[drm] vblank-paced SetCrtc failed "
				"(%s)\n", strerror(errno));
			sReported = true;
		}
		return false;
	}

	// Event thread, exclusive lock held: SetMode() cannot run, but the swap
	// contract wants fFlipMutex anyway.
	pthread_mutex_lock(&fFlipMutex);
	_FinishFrameSwap();
	return true;
}


void
DrmHWInterface::_EventThreadMain()
{
	while (fRunning) {
		int seat_fd = fSeat ? libseat_get_fd(fSeat) : -1;

		bool active = fSessionActive.load();

		struct pollfd pfds[8];
		int cardIdx[4];
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

		for (size_t card = 0; card < fCards.size() && card < 4; card++) {
			cardIdx[card] = -1;
			if (active) {
				pfds[nfds].fd      = fCards[card].fd;
				pfds[nfds].events  = POLLIN;
				pfds[nfds].revents = 0;
				cardIdx[card] = nfds++;
			}
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

		// Idle unless a frame is in flight or waiting; one held back by
		// the pacing below wakes when it is due.
		_ReportHoldStats();
		int timeout = 500;
		if (fPageFlipPending || fPacingLockHeld)
			timeout = 16;
		else if (fPageFlipEnabled && fStandIn == 0) {
			fDirtyMutex.Lock();
			const bool dirty = fAccumulatedDirty.CountRects() > 0
				|| fNeedsFlip || fStagedCursor.valid || !fStagedPlanes.empty();
			// Nothing left to do: the next change must write the wake.
			if (!dirty)
				fWakeQueued = false;
			fDirtyMutex.Unlock();

			// Wanted a frame for seconds and none reached the screen.
			static bigtime_t sDirtySince = 0;
			static bigtime_t sSeenDone = 0;
			static bigtime_t sStallReport = 0;
			const bigtime_t at = system_time();
			const bigtime_t done = fLastFlipDone.load();
			if (!dirty || done != sSeenDone)
				sDirtySince = dirty ? at : 0;
			sSeenDone = done;
			if (dirty && sDirtySince != 0 && at - sDirtySince > 2000000
					&& at - sStallReport > 5000000) {
				sStallReport = at;
				fprintf(stderr, "[drm] no frame completed for %lld ms while "
					"dirty (pending %d, cursor in flight %d)\n",
					(long long)((at - sDirtySince) / 1000),
					(int)fPageFlipPending.load(),
					(int)(fCursorInFlightAt.load() != 0));
			}
			if (dirty) {
				const bigtime_t flipDue = fFlipVblank
					? 0 : fLastFlipIssue + _FramePeriod();
				const bigtime_t now = system_time();
				timeout = flipDue > now
					? (int)((flipDue - now + 999) / 1000) : 16;
			}
		}
		const bool outputsWaiting = _OutputsWaiting();
		if (outputsWaiting && timeout > 16)
			timeout = 16;
		int ret = poll(pfds, nfds, timeout);
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
				// The handler swaps the buffer pointers; SetMode() replaces
				// them under the same lock. A damage flush swaps nothing.
				bool locked = !fDamageFlush && LockExclusiveAccess();
				drmHandleEvent(fFd, &evctx);
				if (locked)
					UnlockExclusiveAccess();
			}
			for (size_t card = 0; card < fCards.size() && card < 4; card++) {
				if (cardIdx[card] < 0
						|| (pfds[cardIdx[card]].revents & POLLIN) == 0)
					continue;

				static drmEventContext cardContext = {
					.version           = DRM_EVENT_CONTEXT_VERSION,
					.page_flip_handler = DrmHWInterface::_PageFlipHandler,
				};
				// Its flips swap the buffers of an output.
				bool locked = LockExclusiveAccess();
				drmHandleEvent(fCards[card].fd, &cardContext);
				if (locked)
					UnlockExclusiveAccess();
			}
			if (wake_idx >= 0 && (pfds[wake_idx].revents & POLLIN)) {
				uint64_t v;
				read(fWakeFd, &v, sizeof(v));
				// Damage waiting for its frame is woken for by the timeout;
				// further draws must not wake the thread again.
				fDirtyMutex.Lock();
				if (!outputsWaiting
						&& !(fPageFlipEnabled && fStandIn == 0
							&& (fAccumulatedDirty.CountRects() > 0
								|| fNeedsFlip || fStagedCursor.valid
								|| !fStagedPlanes.empty())))
					fWakeQueued = false;
				fDirtyMutex.Unlock();
			}
			if (udev_idx >= 0 && (pfds[udev_idx].revents & POLLIN)) {
				_HandleHotplug();
			}
		}

		if (active && _CheckResume())
			_OnResume();

		// A wedged flip must not hold the pacing lock, and with it every
		// drawing thread, forever.
		if (fPacingLockHeld && (!fPageFlipPending
				|| system_time() - fFlipIssueTime > 250000)) {
			fPacingLockHeld = false;
			UnlockExclusiveAccess();
		}

		if (active && fDirectRender && !fHardwareCursorEnabled
				&& !fPageFlipPending && !fPacingLockHeld) {
			if (LockExclusiveAccess()) {
				if (fDirectRender && !fHardwareCursorEnabled
						&& !fPageFlipPending && fFrontBuffer != NULL
						&& fWriteTarget != NULL && fRenderBuffer != NULL)
					_LeaveDirectRender();
				UnlockExclusiveAccess();
			}
		}

		// SetMode() deletes and recreates the buffers under the exclusive
		// lock; reading fWriteTarget outside it used a freed buffer (#303).
		bool flipLocked = false;
		// Without a real vblank (virtio-gpu) a flip completes at once and
		// every client frame became a full upload. Issue at most one per
		// refresh period.
		// A cursor commit in flight holds the frame back too; taking the
		// lock only to skip the flip starves the presenters.
		if (active && fPageFlipEnabled && !fPageFlipPending
				&& !_CommitInFlight()
				&& fDpmsState == B_DPMS_ON && fStandIn == 0
				&& (fFlipVblank
					|| system_time() >= fLastFlipIssue + _FramePeriod())) {
			// Idle frames must not drain the drawing threads every poll.
			fDirtyMutex.Lock();
			BRegion held;
			_HeldRegion(held);
			held.OffsetBy(-fOrigin.x, -fOrigin.y);
			BRegion composable(fAccumulatedDirty);
			composable.Exclude(&held);
			bool wantFlip = composable.CountRects() > 0 || fNeedsFlip
				|| !fStagedPlanes.empty();
			fDirtyMutex.Unlock();
			if (wantFlip)
				flipLocked = LockExclusiveAccess();
		}

		// Only the cursor moved: commit it alone, it need not wait for a
		// frame. A refusal stays staged and is retried on the next pass.
		if (active && !flipLocked && fAtomicBatch && !_CommitInFlight()
				&& fDpmsState == B_DPMS_ON && fStandIn == 0
				&& system_time() >= fCursorRetryAt) {
			CursorUpdate only;
			only.valid = false;
			fDirtyMutex.Lock();
			if (fStagedCursor.valid) {
				only = fStagedCursor;
				fStagedCursor.valid = false;
			}
			fDirtyMutex.Unlock();
			if (only.valid && !_CommitCursorOnly(only)) {
				fDirtyMutex.Lock();
				if (!fStagedCursor.valid)
					fStagedCursor = only;
				fDirtyMutex.Unlock();
			}
		}

		if (flipLocked && fPageFlipEnabled && !_CommitInFlight()
				&& fWriteTarget != NULL && fDpmsState == B_DPMS_ON) {
			CursorUpdate staged;
			staged.valid = false;
			std::vector<PlaneUpdate> planes;
			std::vector<PlaneNote> lost;
			bool usedAtomic = false;
			// What changed since the last frame, and that plus what the
			// target missed while it was on screen.
			BRegion damage;
			BRegion present;

			fDirtyMutex.Lock();
			BRegion held;
			BRegion keep;
			BRegion keepStale;
			_HeldRegion(held);
			held.OffsetBy(-fOrigin.x, -fOrigin.y);
			// Direct render draws on screen: holds do not apply.
			if (fDirectRender)
				held.MakeEmpty();
			BRegion composable(fAccumulatedDirty);
			composable.Exclude(&held);
			const bool hasDirty = composable.CountRects() > 0
				|| fNeedsFlip || fStagedCursor.valid || !fStagedPlanes.empty();
			if (hasDirty) {
				// Draws from here on wake the thread again.
				fWakeQueued = false;
				if (fAtomicBatch) {
					staged = fStagedCursor;
					fStagedCursor.valid = false;
				}
				planes.swap(fStagedPlanes);
				for (size_t i = 0; i < planes.size(); i++) {
					const bigtime_t now = system_time();
					static bigtime_t sLastReport = 0;
					if (planes[i].staged != 0
							&& now - planes[i].staged > 20000
							&& now - sLastReport > 1000000) {
						sLastReport = now;
						fprintf(stderr, "[drm] plane %u staged %lld ms "
							"before its commit\n", planes[i].planeId,
							(long long)((now - planes[i].staged) / 1000));
					}
				}
				// Direct render keeps its damage for the completion.
				if (!fDirectRender) {
					// Held areas wait for their session to end.
					keep = fAccumulatedDirty;
					keep.IntersectWith(&held);
					keepStale = fBackStale;
					keepStale.IntersectWith(&held);
					damage = composable;
					fAccumulatedDirty = keep;
					present = composable;
					BRegion stale(fBackStale);
					stale.Exclude(&held);
					present.Include(&stale);
					fHoldDeferred += keep.CountRects();
				} else
					present = fAccumulatedDirty;
			}
			fDirtyMutex.Unlock();

			if (hasDirty && !fDirectRender) {
				if (keepStale.CountRects() > 0 && fFrontBuffer != NULL
						&& fWriteTarget != fFrontBuffer) {
					copy_scanout_rects(fFrontBuffer, fWriteTarget, keepStale,
						(int32)fSource.Width(), (int32)fSource.Height(),
						fPanelOrientation, fPanelReflection);
				}
				_ComposeFrame(present);
			}

			const bool paced = fVblankPacedFlip;
			bool attempted = false;
			int r = -1;
			int flipErr = 0;
			if (hasDirty && paced) {
				r = _VblankPacedFlip() ? 0 : -1;
			} else if (hasDirty) {
				// Issue under fFlipMutex: SetMode() tears the buffers
				// down under the same mutex, so fWriteTarget cannot be
				// deleted between the NULL check and GetFbId(), and the
				// token is registered before any completion can land.
				pthread_mutex_lock(&fFlipMutex);
				if (fWriteTarget != NULL) {
					attempted = true;
					const uint32_t fb = fWriteTarget->GetFbId();
					const bool atomic = fAtomicSupported
						&& fPrimaryPlaneId != 0;
					if (fDamageFlush && !atomic) {
						// Synchronous: no event, nothing left pending.
						r = _DirtyFlush(fb, present) == B_OK ? 0 : -1;
						flipErr = errno;
					} else {
						FlipToken* token = new FlipToken;
						token->hw = this;
						token->generation = fFlipGeneration;
						token->output = 0;
						token->cursorOnly = false;
						fFlipTokens.push_back(token);

						if (atomic) {
							usedAtomic = true;
							for (size_t i = 0; i < planes.size(); i++) {
								if (planes[i].serial == 0)
									continue;
								PlaneNote note = { planes[i].planeId,
									planes[i].serial, 0 };
								token->planes.push_back(note);
							}
							r = (_AtomicCommitFrame(fb,
								present.CountRects() > 0 ? &present : NULL,
								staged.valid ? &staged : NULL,
								planes.empty() ? NULL : &planes, token)
								== B_OK) ? 0 : -1;
						} else {
							struct modeset_dev* dev = fDev;
							r = drmModePageFlip(fFd, dev->crtc, fb,
								DRM_MODE_PAGE_FLIP_EVENT, token);
						}
						if (r != 0) {
							flipErr = errno;
							// Refused (EBUSY is routine): no event will
							// arrive, so reap the token here.
							for (size_t i = 0; i < fFlipTokens.size(); i++) {
								if (fFlipTokens[i] == token) {
									fFlipTokens.erase(
										fFlipTokens.begin() + i);
									break;
								}
							}
							delete token;
						} else
							fPageFlipPending = true;
					}
				}
				pthread_mutex_unlock(&fFlipMutex);
			}

			if (hasDirty) {
				fDirtyMutex.Lock();
				if (r == 0) {
					fNeedsFlip = false;
					// The buffer leaving the screen has all but this frame.
					if (!fDirectRender && !fDamageFlush) {
						fBackStale = damage;
						fBackStale.Include(&keepStale);
					}
				} else {
					// Not shown: damage and cursor ride the retry.
					fAccumulatedDirty.Include(&damage);
					if (staged.valid && !fStagedCursor.valid)
						fStagedCursor = staged;
					for (size_t i = 0; i < planes.size(); i++) {
						bool newer = false;
						for (size_t j = 0; j < fStagedPlanes.size(); j++) {
							if (fStagedPlanes[j].planeId == planes[i].planeId)
								newer = true;
						}
						if (!newer)
							fStagedPlanes.push_back(planes[i]);
						else if (planes[i].serial != 0) {
							PlaneNote note = { planes[i].planeId,
								planes[i].serial, 0 };
							lost.push_back(note);
						}
					}
				}
				fDirtyMutex.Unlock();
				if (!lost.empty())
					_NotifyPlanes(lost, false, 0);
			}

			if (hasDirty && r == 0 && !usedAtomic) {
				std::vector<PlaneNote> shown;
				for (size_t i = 0; i < planes.size(); i++) {
					_SetPlaneNow(planes[i]);
					if (planes[i].serial != 0) {
						PlaneNote note = { planes[i].planeId,
							planes[i].serial, 0 };
						shown.push_back(note);
					}
				}
				if (!shown.empty())
					_NotifyPlanes(shown, true, system_time());
			}

			if (hasDirty && r == 0) {
				fFlipFailures = 0;
				fFlipRecovered = false;
				fLastFlipIssue = system_time();
				if (fDirectRender && !paced && !fPacingLockHeld) {
					// Keep the exclusive lock until the completion
					// handler drops it.
					fPacingLockHeld = true;
					fFlipIssueTime = system_time();
					flipLocked = false;
				}
			} else if (hasDirty && attempted) {
				// Do not reclaim the DRM master on EACCES: losing it
				// means another app_server is taking over.
				static bool sReported = false;
				if (!sReported) {
					fprintf(stderr, "[drm] page flip failed "
						"(%s); retrying\n", strerror(flipErr));
					sReported = true;
				}
				// Only flip-unsupported errnos outside a modeset
				// count; EACCES and EPERM around a VT switch or
				// an EINVAL under a modeset are transient, and
				// eight of those must not tear the session down.
				// EINVAL on an atomic frame: remodeset once and retry
				// before it counts against the flip.
				if (flipErr == EINVAL && usedAtomic && !fFlipRecovered) {
					fFlipRecovered = true;
					fprintf(stderr, "[drm] frame commit EINVAL, "
						"remodeset and retry\n");
					_RestoreDisplay();
					fDirtyMutex.Lock();
					fNeedsFlip = true;
					fDirtyMutex.Unlock();
				} else if (drm_flip_failure_counts(flipErr,
						fModesetActive.load(),
						&fFlipFailures)) {
					fVblankPacedFlip = true;
					fprintf(stderr, "[drm] page flip unusable "
						"(%s); pacing SetCrtc on vblank "
						"(tearing possible)\n",
						strerror(flipErr));
				}
			}
		}
		if (flipLocked)
			UnlockExclusiveAccess();

		if (active)
			_PresentOutputs();

		// QEMU resize may not emit HOTPLUG=1: poll the preferred geometry (no
		// reprobe), but not while a user-set mode is active or it would undo it.
		if (active && !fUserSetMode
				&& system_time() - fLastModeCheck > 2000000LL) {
			fLastModeCheck = system_time();

			display_mode preferred;
			if (GetPreferredModeCurrent(&preferred) == B_OK
				&& (preferred.virtual_width != fLastPreferredWidth.load()
					|| preferred.virtual_height
						!= fLastPreferredHeight.load())) {
				fprintf(stderr, "[drm] preferred mode %ux%u -> %ux%u; "
					"scheduling resize\n", fLastPreferredWidth.load(),
					fLastPreferredHeight.load(), preferred.virtual_width,
					preferred.virtual_height);
				fLastPreferredWidth = preferred.virtual_width;
				fLastPreferredHeight = preferred.virtual_height;
				_ScheduleResize();
			}
		}
	}
}


#ifdef HAVE_GBM
struct gbm_device*
DrmHWInterface::GbmDevice()
{
	fDirtyMutex.Lock();
	if (fGbmDevice == NULL && fFd >= 0)
		fGbmDevice = gbm_create_device(fFd);
	struct gbm_device* device = fGbmDevice;
	fDirtyMutex.Unlock();
	return device;
}
#endif


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
	_ReleaseOverlayPlanes();

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
	for (size_t i = 0; i < fOutputs.size(); i++) {
		_TeardownOutput(fOutputs[i]);
		delete fOutputs[i];
	}
	fOutputs.clear();
	delete fRenderBuffer;
	fRenderBuffer = NULL;

#ifdef HAVE_GBM
	for (size_t i = 0; i < fCards.size(); i++) {
		if (fCards[i].gbm != NULL)
			gbm_device_destroy(fCards[i].gbm);
		fCards[i].gbm = NULL;
	}
	if (fGbmDevice != NULL) {
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

	for (size_t i = 0; i < fCards.size(); i++) {
		modeset_cleanup(fCards[i].fd);
		if (fSeat != NULL)
			libseat_close_device(fSeat, fCards[i].deviceId);
		else
			close(fCards[i].fd);
	}
	fCards.clear();

	if (fSeat && fDeviceId > 0)
		libseat_close_device(fSeat, fDeviceId);

	if (fSeat)
		libseat_close_seat(fSeat);

	modeset_cleanup(fFd);

	pthread_mutex_destroy(&fFlipMutex);
	pthread_mutex_destroy(&fCursorBlendMutex);
	pthread_mutex_destroy(&fModesetMutex);
	pthread_cond_destroy(&fFlipCond);
	for (size_t i = 0; i < fFlipTokens.size(); i++)
		delete fFlipTokens[i];
	fFlipTokens.clear();
	pthread_mutex_destroy(&fOverlayMutex);
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


// The connector's mode of that size closest to the wanted refresh rate.
static drmModeModeInfo*
find_connector_mode(drmModeConnector* conn, uint32_t width, uint32_t height,
	const display_mode& mode)
{
	drmModeModeInfo* found = NULL;

	float targetRefresh = 0;
	if (mode.timing.h_total > 0 && mode.timing.v_total > 0) {
		targetRefresh = float(mode.timing.pixel_clock * 1000)
			/ float(mode.timing.h_total * mode.timing.v_total);
	}

	if (targetRefresh > 0) {
		float bestDiff = 999;
		for (int i = 0; i < conn->count_modes; i++) {
			if (conn->modes[i].hdisplay != width
					|| conn->modes[i].vdisplay != height)
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
			if (conn->modes[i].hdisplay == width
					&& conn->modes[i].vdisplay == height) {
				found = &conn->modes[i];
				break;
			}
		}
	}

	return found;
}


status_t
DrmHWInterface::SetMode(const display_mode& mode)
{
	CALLED();

	if (fFd < 0)
		return B_ERROR;

	if (!LockExclusiveAccess())
		return B_ERROR;

	// Held for the whole body: fDev's node stays valid while a udev
	// hotplug on the event thread walks the same list.
	ModesetGuard modesetGuard(&fModesetMutex);

	struct modeset_dev* dev = fDev;
	if (!dev) {
		UnlockExclusiveAccess();
		return B_ERROR;
	}

	// The monitor of the adopted output takes the mode.
	if (fStandIn != 0) {
		const uint32 standIn = fStandIn;
		status_t status = SetOutputMode(standIn, mode);
		Output* output = _FindOutput(standIn);
		if (output != NULL)
			_SyncStandIn(output);
		UnlockExclusiveAccess();
		return status;
	}

	drmModeConnector* conn = drmModeGetConnector(fFd, dev->conn);
	if (!conn) { UnlockExclusiveAccess(); return B_ERROR; }

	// mode.virtual_width/height is the caller's logical (post-rotation)
	// request; the connector only ever advertises physical panel modes.
	uint32_t physW, physH;
	_ApplyOrientationSwap(mode.virtual_width, mode.virtual_height,
		physW, physH);

	drmModeModeInfo* found = find_connector_mode(conn, physW, physH, mode);
	if (!found) {
		drmModeFreeConnector(conn);
		UnlockExclusiveAccess();
		return B_BAD_VALUE;
	}

	_DrainPendingFlip();

	// The modeset reshapes the CRTC under the planes.
	_ReleaseOverlayPlanes(0, false);

	// Bump the generation before the pointers change: a completion event
	// that lands after the drain (abandoned flip) must not swap these.
	// Publish the replacement buffers under the same mutex the flip path
	// issues under, so it never sees a half-built pair.
	pthread_mutex_lock(&fFlipMutex);
	fFlipGeneration++;
	delete fFrontBuffer; fFrontBuffer = NULL;
	delete fBackBuffer;  fBackBuffer  = NULL;
	fWriteTarget = NULL;
	pthread_mutex_unlock(&fFlipMutex);

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

	pthread_mutex_lock(&fFlipMutex);
	fFrontBuffer = new DrmBuffer(fFd, dev, false);
	fBackBuffer = NULL;
	fWriteTarget = fFrontBuffer;
	fPageFlipEnabled = false;
	fPageFlipPending = false;

	fDirtyMutex.Lock();
	fAccumulatedDirty.MakeEmpty();
	fBackStale.MakeEmpty();
	fNeedsFlip = false;
	fDirtyMutex.Unlock();

	if (fDamageFlush) {
		fPageFlipEnabled = true;
	} else if (modeset_create_back_fb(fFd, dev) == 0) {
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
	_RebuildRenderBuffer(logW, logH);
	pthread_mutex_unlock(&fFlipMutex);

	// The modeset below starts scanning out fFrontBuffer immediately;
	// give it defined contents instead of recycled old-rotation pages.
	_ClearBufferToBlack(fFrontBuffer);
	_ClearBufferToBlack(fBackBuffer);
	_ClearBufferToBlack(&fSource);

	// A flip attempted while the CRTC is being reprogrammed fails
	// with EINVAL; mark the window so that failure is not counted
	// as proof the flip is unsupported.
	fModesetActive = true;
	int ret;
	if (fAtomicSupported && fPrimaryPlaneId) {
		if (_AtomicModeset(dev->fb, &dev->mode) != B_OK) {
			fModesetActive = false;
			UnlockExclusiveAccess();
			return B_ERROR;
		}
		ret = 0;
	} else {
		ret = drmModeSetCrtc(fFd, dev->crtc, dev->fb, 0, 0,
		                     &dev->conn, 1, &dev->mode);
	}
	fModesetActive = false;
	if (ret) {
		UnlockExclusiveAccess();
		return B_ERROR;
	}

	_FillModeInfo(fDisplayMode, dev->mode);
	fDisplayMode.virtual_width  = logW;
	fDisplayMode.virtual_height = logH;

	// A mode other than the preferred one is a user choice and disarms the
	// poll; matching the preference (hotplug, QEMU resize) leaves it armed.
	{
		display_mode preferredMode;
		if (GetPreferredMode(&preferredMode) == B_OK
			&& preferredMode.virtual_width == logW
			&& preferredMode.virtual_height == logH)
			fUserSetMode = false;
		else
			fUserSetMode = true;
		fLastPreferredWidth = logW;
		fLastPreferredHeight = logH;
	}

	// A shrink can leave the last cursor position outside the new CRTC;
	// clamp before re-arming the hardware plane at the old coordinates.
	// fCursorLocation is logical, so clamp against the logical mode, not
	// dev->width/height (physical). Guarded on fCursor alone: SetCursor()
	// now decides hardware vs. software, so it must always run to flip
	// the sprite back on after a return from a rotated orientation.
	if (fCursor.IsSet()) {
		const BRect desktop = _DesktopBounds();
		if (fCursorLocation.x > desktop.right)
			fCursorLocation.x = desktop.right;
		if (fCursorLocation.y > desktop.bottom)
			fCursorLocation.y = desktop.bottom;
		if (fCursorLocation.x < 0)
			fCursorLocation.x = 0;
		if (fCursorLocation.y < 0)
			fCursorLocation.y = 0;
		SetCursor(fCursor);
	}

	_SetupDirectRender();

	// Both scanout buffers were just rebuilt: re-present the whole frame
	// through rotate_point(), or recycled pages keep the old rotation.
	{
		const BRect fullFrame(0, 0,
			fDisplayMode.virtual_width - 1, fDisplayMode.virtual_height - 1);

		fDirtyMutex.Lock();
		fAccumulatedDirty.MakeEmpty();
		fBackStale.MakeEmpty();
		fAccumulatedDirty.Include(fullFrame);
		fNeedsFlip = fPageFlipEnabled;
		fDirtyMutex.Unlock();

		// Single-buffered has no flip to ride in on: present now, or the
		// cleared buffers stay on screen until the first damage blit.
		if (!fPageFlipEnabled && fRenderBuffer != NULL)
			_BlitRect(&fSource, fWriteTarget, fullFrame);

		// The buffer they read from may have been replaced.
		_DamageOutputs();

		_Wake();
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
	return _GetPreferredMode(mode, true);
}


status_t
DrmHWInterface::GetPreferredModeCurrent(display_mode* mode)
{
	return _GetPreferredMode(mode, false);
}


status_t
DrmHWInterface::_GetPreferredMode(display_mode* mode, bool probe)
{
	CALLED();

	// The node can be freed by a concurrent udev rescan; callers include
	// the resize thread and the event thread's preferred-mode poll.
	ModesetGuard modesetGuard(&fModesetMutex);
	if (fDev == NULL || _PreferredModeFor(_FirstKey(),
			fStandIn != 0 ? OutputOrientation(fStandIn) : fPanelOrientation,
			probe, mode) != B_OK)
		*mode = fDisplayMode;
	return B_OK;
}


status_t
DrmHWInterface::_PreferredModeFor(uint32 connector, int32 orientation,
	bool probe, display_mode* mode)
{
	const int fd = _FdForKey(connector);
	connector = key_connector(connector);
	if (fd < 0)
		return B_NO_INIT;

	// drmModeGetConnector() forces a reprobe (DDC/EDID, load
	// detection). The poll must not do that every two seconds.
	drmModeConnector* conn = probe
		? drmModeGetConnector(fd, connector)
		: drmModeGetConnectorCurrent(fd, connector);
	const drmModeModeInfo* picked
		= conn != NULL ? modeset_pick_mode(fd, conn) : NULL;

	if (picked == NULL) {
		if (conn != NULL)
			drmModeFreeConnector(conn);
		return B_ERROR;
	}

	drmModeModeInfo m = *picked;
	drmModeFreeConnector(conn);

	_FillModeInfo(*mode, m);
	const bool swapped = orientation == PANEL_ORIENTATION_LEFT_UP
		|| orientation == PANEL_ORIENTATION_RIGHT_UP;
	mode->virtual_width = swapped ? m.vdisplay : m.hdisplay;
	mode->virtual_height = swapped ? m.hdisplay : m.vdisplay;
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

	if (fDev == NULL)
		return B_ERROR;

	return _ModeListFor(_FirstKey(),
		fStandIn != 0 ? OutputOrientation(fStandIn) : fPanelOrientation,
		_modeList, _count);
}


status_t
DrmHWInterface::_ModeListFor(uint32 connector, int32 orientation,
	display_mode** _modeList, uint32* _count)
{
	const int fd = _FdForKey(connector);
	connector = key_connector(connector);
	if (fd < 0)
		return B_ERROR;

	drmModeConnector* conn = drmModeGetConnector(fd, connector);
	if (conn == NULL)
		return B_ERROR;

	const bool swapped = orientation == PANEL_ORIENTATION_LEFT_UP
		|| orientation == PANEL_ORIENTATION_RIGHT_UP;

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
		dm.virtual_width = swapped ? m.vdisplay : m.hdisplay;
		dm.virtual_height = swapped ? m.hdisplay : m.vdisplay;
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

	struct modeset_dev* dev = fDev;
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

	err = ProposeModeFromList(modeList, count, candidate, low, high);
	delete[] modeList;
	return err;
}


static float
mode_refresh_rate(const display_mode& mode)
{
	if (mode.timing.h_total == 0 || mode.timing.v_total == 0)
		return 0;
	return float(mode.timing.pixel_clock * 1000.0)
		/ float(mode.timing.h_total * mode.timing.v_total);
}


// Whether a is a better pick than b among modes of one size: the refresh
// closest to the candidate's, else the highest not above the preferred
// mode's (the first of the list).
static bool
better_refresh(const display_mode& a, const display_mode& b,
	const display_mode& preferred, const display_mode& candidate)
{
	const float ra = mode_refresh_rate(a);
	const float rb = mode_refresh_rate(b);

	const float wanted = mode_refresh_rate(candidate);
	if (wanted > 0)
		return fabsf(ra - wanted) < fabsf(rb - wanted);

	const float limit = mode_refresh_rate(preferred);
	if (limit <= 0)
		return false;
	const bool fitsA = ra <= limit + 0.5f;
	const bool fitsB = rb <= limit + 0.5f;
	if (fitsA != fitsB)
		return fitsA;
	return fitsA ? ra > rb : ra < rb;
}


status_t
DrmHWInterface::ProposeModeFromList(display_mode* modeList, uint32 count,
	display_mode* candidate, const display_mode* low,
	const display_mode* high)
{
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
		if (diff < bestDiff || (diff == bestDiff && best >= 0
				&& better_refresh(m, modeList[best], modeList[0],
					*candidate))) {
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
			if (diff < bestDiff || (diff == bestDiff
					&& better_refresh(m, modeList[best], modeList[0],
						*candidate))) {
				bestDiff = diff;
				best = (int)i;
			}
		}
	}

	*candidate = modeList[best];

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
	// The node can be freed by a concurrent udev rescan.
	ModesetGuard modesetGuard(&fModesetMutex);
	struct modeset_dev* dev = fDev;
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
		// The other outputs follow, whatever the first one does: it may be
		// unplugged.
		for (size_t i = 0; i < fOutputs.size(); i++)
			_SetOutputDPMS(fOutputs[i], state);
		if (ret == 0 || !fOutputs.empty()) {
			fDpmsState = state;
			_RepaintAfterDPMS();
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
			for (size_t j = 0; j < fOutputs.size(); j++)
				_SetOutputDPMS(fOutputs[j], state);
			_RepaintAfterDPMS();
			return B_OK;
		}
		if (prop) drmModeFreeProperty(prop);
	}

	drmModeFreeConnector(conn);
	return B_UNSUPPORTED;
}


// Flips stop while the display is off and damage piles up; flip it all
// as soon as the display is back on.
void
DrmHWInterface::_RepaintAfterDPMS()
{
	if (fDpmsState != B_DPMS_ON)
		return;
	fDirtyMutex.Lock();
	fNeedsFlip = true;
	fDirtyMutex.Unlock();
	_Wake();
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


// The backlight node is root-only; logind writes it for our session.
// B_ERROR without a session, so the caller falls back to sysfs.
static status_t
_SetBrightnessViaLogind(const struct backlight* backlight, int value)
{
	sd_bus* bus = NULL;
	int r = sd_bus_open_system(&bus);
	if (r < 0)
		return B_ERROR;

	char* sid = NULL;
	r = sd_pid_get_session(0, &sid);
	if (r < 0 || sid == NULL) {
		sd_bus_unref(bus);
		return B_ERROR;
	}

	char sessionPath[256];
	snprintf(sessionPath, sizeof(sessionPath),
		"/org/freedesktop/login1/session/%s", sid);
	free(sid);

	// sysfs path is /sys/class/backlight/<name>; logind wants just <name>.
	const char* name = strrchr(backlight->path, '/');
	name = name != NULL ? name + 1 : backlight->path;

	sd_bus_error err = SD_BUS_ERROR_NULL;
	r = sd_bus_call_method(bus, "org.freedesktop.login1", sessionPath,
		"org.freedesktop.login1.Session", "SetBrightness", &err, NULL,
		"ssu", "backlight", name, (uint32_t)value);
	sd_bus_error_free(&err);
	sd_bus_unref(bus);
	return r >= 0 ? B_OK : B_ERROR;
}


status_t
DrmHWInterface::SetBrightness(float brightness)
{
	if (!fBacklight)
		return B_UNSUPPORTED;
	// Prefer the value cached at init; a re-read can fail on write-only nodes.
	int max = fBacklight->max_brightness;
	if (max <= 0)
		max = (int)backlight_get_max_brightness(fBacklight);
	if (max <= 0)
		return B_ERROR;
	int val = (int)(brightness * max + 0.5f);
	if (val < 0)
		val = 0;
	if (val > max)
		val = max;
	if (_SetBrightnessViaLogind(fBacklight, val) == B_OK)
		return B_OK;
	return backlight_set_brightness(fBacklight, val) == 0 ? B_OK : B_ERROR;
}


status_t
DrmHWInterface::GetBrightness(float* brightness)
{
	if (!fBacklight || !brightness)
		return B_UNSUPPORTED;
	int max = fBacklight->max_brightness;
	if (max <= 0)
		max = (int)backlight_get_max_brightness(fBacklight);
	if (max <= 0) {
		*brightness = 0.0f;
		return B_ERROR;
	}
	// actual_brightness tracks the panel; brightness can read 0 while lit.
	int cur = (int)backlight_get_actual_brightness(fBacklight);
	if (cur < 0)
		cur = (int)backlight_get_brightness(fBacklight);
	if (cur < 0) {
		*brightness = 0.0f;
		return B_OK;
	}
	*brightness = (float)cur / max;
	return B_OK;
}


static void
_kelvin_to_rgb(float kelvin, float& r, float& g, float& b)
{
	// Standard color-temperature-to-RGB approximation (Tanner Helland /
	// redshift).  Maps a Planckian-locus correlated color temperature in
	// Kelvin to R, G, B scale in [0..1], valid for 1000..9000 K. Above
	// ~6500 K blue pins at 1.0 and red/green fall, so the tint is cool.
	float temp = kelvin / 100.0f;

	// Red
	if (temp <= 66.0f) {
		r = 1.0f;
	} else {
		r = temp - 60.0f;
		r = 329.698727446f * powf(r, -0.1332047592f);
		r /= 255.0f;
	}
	r = std::max(0.0f, std::min(1.0f, r));

	// Green
	if (temp <= 66.0f) {
		g = temp;
		g = 99.4708025861f * logf(g) - 161.1195681661f;
	} else {
		g = temp - 60.0f;
		g = 288.1221695283f * powf(g, -0.0755148492f);
	}
	g = std::max(0.0f, std::min(1.0f, g / 255.0f));

	// Blue
	if (temp >= 66.0f) {
		b = 1.0f;
	} else if (temp <= 19.0f) {
		b = 0.0f;
	} else {
		b = temp - 10.0f;
		b = 138.5177312231f * logf(b) - 305.0447927307f;
		b /= 255.0f;
	}
	b = std::max(0.0f, std::min(1.0f, b));
}


// Drivers reject a ramp whose length differs from crtc->gamma_size;
// virtio-gpu and simpledrm report 0 and have no LUT at all.
static int
_drm_gamma_size(int fd, uint32_t crtc_id)
{
	drmModeCrtc* crtc = drmModeGetCrtc(fd, crtc_id);
	if (crtc == NULL)
		return 0;
	int size = crtc->gamma_size;
	drmModeFreeCrtc(crtc);
	return size;
}


status_t
DrmHWInterface::SetTemperature(float kelvin)
{
	if (fFd < 0)
		return B_ERROR;

	// The node can be freed by a concurrent udev rescan.
	ModesetGuard modesetGuard(&fModesetMutex);
	struct modeset_dev* dev = fDev;
	if (!dev)
		return B_ERROR;

	int size = _drm_gamma_size(fFd, dev->crtc);
	if (size <= 0) {
		fTemperatureSupported = false;
		return B_NOT_SUPPORTED;
	}

	if (kelvin < 1000.0f)
		kelvin = 1000.0f;
	else if (kelvin > 9000.0f)
		kelvin = 9000.0f;

	float rScale, gScale, bScale;
	_kelvin_to_rgb(kelvin, rScale, gScale, bScale);

	std::vector<uint16_t> rampR(size), rampG(size), rampB(size);
	for (int i = 0; i < size; i++) {
		float t = (size > 1) ? (float)i / (float)(size - 1) : 0.0f;
		rampR[i] = (uint16_t)(t * 65535.0f * rScale);
		rampG[i] = (uint16_t)(t * 65535.0f * gScale);
		rampB[i] = (uint16_t)(t * 65535.0f * bScale);
	}

	int ret = drmModeCrtcSetGamma(fFd, dev->crtc, size,
		rampR.data(), rampG.data(), rampB.data());
	if (ret == 0) {
		fTemperature = kelvin;
		fTemperatureSupported = true;
		return B_OK;
	}
	fTemperatureSupported = false;
	return B_ERROR;
}


status_t
DrmHWInterface::GetTemperature(float* kelvin)
{
	if (!kelvin)
		return B_BAD_VALUE;

	struct modeset_dev* dev = fDev;
	if (dev == NULL || fFd < 0)
		return B_NOT_SUPPORTED;

	if (!fTemperatureSupported || _drm_gamma_size(fFd, dev->crtc) <= 0)
		return B_NOT_SUPPORTED;

	*kelvin = fTemperature;
	return B_OK;
}


status_t
DrmHWInterface::GetConnectorName(BString& name)
{
	if (fDev == NULL)
		return B_NO_INIT;

	return _ConnectorNameFor(_FirstKey(), name);
}


status_t
DrmHWInterface::_ConnectorNameFor(uint32 connector, BString& name)
{
	const int slot = key_slot(connector);
	const int fd = _FdForKey(connector);
	connector = key_connector(connector);
	if (fd < 0)
		return B_NO_INIT;

	// Current: kernel already holds EDID/type; GetConnector would reprobe.
	drmModeConnector* conn = drmModeGetConnectorCurrent(fd, connector);
	if (conn == NULL)
		return B_ERROR;

	const char* typeName = drmModeGetConnectorTypeName(conn->connector_type);
	if (typeName == NULL || typeName[0] == '\0') {
		drmModeFreeConnector(conn);
		return B_UNSUPPORTED;
	}

	// The cards beside the first one say which they are, so that names stay
	// unique.
	if (slot != 0) {
		name.SetToFormat("card%d-%s-%u", _CardIndexForSlot(slot), typeName,
			conn->connector_type_id);
	} else
		name.SetToFormat("%s-%u", typeName, conn->connector_type_id);
	drmModeFreeConnector(conn);
	return B_OK;
}


// EDID vendor bytes 8-9 encode three 5-bit letters; detailed descriptors
// start at offset 54.  Only the identity fields matter for the caption.
static bool
_drm_edid_identity(const uint8_t* raw, size_t length, monitor_info& info)
{
	if (raw == NULL || length < 128)
		return false;
	if (raw[0] != 0x00 || raw[7] != 0xff)
		return false;

	uint16_t mfg = (uint16_t)((raw[8] << 8) | raw[9]);
	info.vendor[0] = (char)(((mfg >> 10) & 0x1f) + 'A' - 1);
	info.vendor[1] = (char)(((mfg >> 5) & 0x1f) + 'A' - 1);
	info.vendor[2] = (char)((mfg & 0x1f) + 'A' - 1);
	info.vendor[3] = '\0';

	size_t offset = 54;
	for (int i = 0; i < 4 && offset + 18 <= length; i++, offset += 18) {
		if (raw[offset] != 0 || raw[offset + 1] != 0
				|| raw[offset + 2] != 0xfc)
			continue;
		char name[14];
		memcpy(name, raw + offset + 5, 13);
		name[13] = '\0';
		for (int c = 0; c < 13; c++) {
			if (name[c] == '\n' || name[c] == '\r' || name[c] == ' ') {
				name[c] = '\0';
				break;
			}
		}
		strlcpy(info.name, name, sizeof(info.name));
		break;
	}

	return info.vendor[0] != '\0' || info.name[0] != '\0';
}


status_t
DrmHWInterface::GetMonitorInfo(monitor_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	if (fDev == NULL)
		return B_NO_INIT;

	return _MonitorInfoFor(_FirstKey(), info);
}


/*!	What the monitor on \a connector says about itself. Two monitors that say
	the same get the connector name appended to the serial number, so their
	stored settings do not run into each other.
*/
status_t
DrmHWInterface::_MonitorInfoFor(uint32 connector, monitor_info* info)
{
	status_t status = _EdidInfoFor(connector, info);
	if (status != B_OK)
		return status;

	bool shared = false;
	for (size_t card = 0; !shared && card <= fCards.size(); card++) {
		const int slot = card == 0 ? 0 : fCards[card - 1].slot;
		const int fd = card == 0 ? fFd : fCards[card - 1].fd;

		drmModeRes* res = drmModeGetResources(fd);
		if (res == NULL)
			continue;

		for (int i = 0; !shared && i < res->count_connectors; i++) {
			const uint32 other = output_key(slot, res->connectors[i]);
			if (other == connector)
				continue;

			drmModeConnector* conn = drmModeGetConnectorCurrent(fd,
				res->connectors[i]);
			if (conn == NULL)
				continue;
			const bool connected = conn->connection == DRM_MODE_CONNECTED;
			drmModeFreeConnector(conn);

			monitor_info otherInfo;
			shared = connected && _EdidInfoFor(other, &otherInfo) == B_OK
				&& strcasecmp(otherInfo.vendor, info->vendor) == 0
				&& strcasecmp(otherInfo.name, info->name) == 0
				&& otherInfo.product_id == info->product_id
				&& strcmp(otherInfo.serial_number, info->serial_number) == 0;
		}
		drmModeFreeResources(res);
	}

	if (shared) {
		BString name;
		_ConnectorNameFor(connector, name);
		const size_t used = strlen(info->serial_number);
		snprintf(info->serial_number + used,
			sizeof(info->serial_number) - used, "@%s", name.String());
	}
	return B_OK;
}


status_t
DrmHWInterface::_EdidInfoFor(uint32 connector, monitor_info* info)
{
	const int fd = _FdForKey(connector);
	connector = key_connector(connector);
	if (fd < 0)
		return B_NO_INIT;

	// Current: kernel already holds EDID/type; GetConnector would reprobe.
	drmModeConnector* conn = drmModeGetConnectorCurrent(fd, connector);
	if (conn == NULL)
		return B_ERROR;

	drmModeObjectProperties* props = drmModeObjectGetProperties(fd,
		connector, DRM_MODE_OBJECT_CONNECTOR);
	if (props == NULL) {
		drmModeFreeConnector(conn);
		return B_NOT_SUPPORTED;
	}

	status_t status = B_NOT_SUPPORTED;
	memset(info, 0, sizeof(*info));
	info->version = B_ACCELERANT_VERSION;

	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
		if (prop == NULL)
			continue;

		if (strcmp(prop->name, "EDID") == 0
				&& (prop->flags & DRM_MODE_PROP_BLOB)) {
			drmModePropertyBlobRes* blob
				= drmModeGetPropertyBlob(fd, props->prop_values[i]);
			if (blob != NULL) {
				if (_drm_edid_identity((const uint8_t*)blob->data,
						blob->length, *info))
					status = B_OK;
				drmModeFreePropertyBlob(blob);
			}
		}
		drmModeFreeProperty(prop);
	}

	if (conn->mmWidth != 0 || conn->mmHeight != 0) {
		info->width = (float)conn->mmWidth;
		info->height = (float)conn->mmHeight;
	}

	drmModeFreeObjectProperties(props);
	drmModeFreeConnector(conn);
	return status;
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
	if (fDirectRender && fWriteTarget != NULL)
		return fWriteTarget;
	if (fRenderBuffer != NULL)
		return fRenderBuffer;
	return fFrontBuffer;
}


bool
DrmHWInterface::IsDoubleBuffered() const
{
	return fRenderBuffer != NULL;
}


// The rotation convention lives in PanelOrientationTransform.h.


void
DrmHWInterface::_BlitRect(RenderingBuffer* src, RenderingBuffer* dst,
	const BRect& frame)
{
	_BlitRect(src, dst, frame, fPanelOrientation, fPanelReflection);
}


void
DrmHWInterface::_BlitRect(RenderingBuffer* src, RenderingBuffer* dst,
	const BRect& frame, int32 orientation, int32 reflection)
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

	if (orientation != PANEL_ORIENTATION_NORMAL
			|| reflection != B_PANEL_REFLECTION_NONE) {
		const bool swapped = orientation == PANEL_ORIENTATION_LEFT_UP
			|| orientation == PANEL_ORIENTATION_RIGHT_UP;
		if ((int32)dst->Width() != (swapped ? srcH : srcW)
				|| (int32)dst->Height() != (swapped ? srcW : srcH))
			return;

		// Every transform is affine: one logical pixel to the right is a
		// constant physical step, so rotate_point() only places each row.
		int32 px0, py0, px1, py1;
		rotate_point(orientation, 0, 0, srcW, srcH, px0, py0, reflection);
		rotate_point(orientation, 1, 0, srcW, srcH, px1, py1, reflection);
		const ptrdiff_t step = (ptrdiff_t)(py1 - py0) * dstBpr
			+ (px1 - px0) * 4;

		for (int32 sy = y; sy < y + h; sy++) {
			const uint8* srow = srcBase + sy * srcBpr + x * 4;
			int32 px, py;
			rotate_point(orientation, x, sy, srcW, srcH, px, py,
				reflection);
			uint8* d = dstBase + (ptrdiff_t)py * dstBpr + px * 4;
			for (int32 i = 0; i < w; i++, srow += 4, d += step)
				memcpy(d, srow, 4);
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
	// Streaming stores only pay off past the cache.
	if (sAvx2 && bytes >= 64 && (int64)bytes * h >= 64 * 1024) {
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
		const int32 head = (int32)(-(uintptr_t)dp & 31);
		if (head > 0) {
			memcpy(dp, sp, head);
			sp += head; dp += head; r -= head;
		}
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


IntRect
DrmHWInterface::_SoftCursorFrame() const
{
	IntRect frame(0, 0, -1, -1);
	if (fCursorAndDragBitmap && fCursorVisible) {
		frame = fCursorAndDragBitmap->Bounds();
		frame.OffsetTo(fCursorLocation - fCursorAndDragBitmap->GetHotSpot());
	}
	return frame;
}


void
DrmHWInterface::_BlendCursor(RenderingBuffer* srcBg, RenderingBuffer* dst,
	IntRect area, const BPoint& origin, int32 orientation,
	int32 reflection) const
{
	if (srcBg == NULL || dst == NULL || !area.IsValid()
			|| fCursorAndDragBitmap == NULL
			|| fCursorAndDragBitmap->Bits() == NULL || !fCursorVisible)
		return;

	IntRect cf = _SoftCursorFrame();
	if (!cf.IsValid())
		return;
	cf.OffsetBy(-(int32)origin.x, -(int32)origin.y);
	if (!area.Intersects(cf))
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

	// Single-buffered presents run on several threads at once.
	pthread_mutex_lock(&fCursorBlendMutex);

	if (orientation == PANEL_ORIENTATION_NORMAL
			&& reflection == B_PANEL_REFLECTION_NONE) {
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
		pthread_mutex_unlock(&fCursorBlendMutex);
		return;
	}

	const int32 logicalW = srcBg->Width();
	const int32 logicalH = srcBg->Height();

	for (int32 y = top; y <= bottom; y++) {
		uint8* s = bg;
		uint8* c = crs;
		for (int32 x = left; x <= right; x++) {
			int32 px, py;
			rotate_point(orientation, x, y, logicalW, logicalH,
				px, py, reflection);
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
	pthread_mutex_unlock(&fCursorBlendMutex);
}


status_t
DrmHWInterface::CopyBackToFront(const BRect& frame)
{
	if (fFrontBuffer == NULL)
		return B_ERROR;

	// The event thread composes and presents the frame.
	if (fPageFlipEnabled) {
		_AddDamage(frame);
		_Wake();
		return B_OK;
	}

	if (fRenderBuffer == NULL)
		return HWInterface::CopyBackToFront(frame);

	BRect local = frame;
	if (!_SingleOutput()) {
		_AddOutputDamage(frame);
		_Wake();
		local = frame & _Frame();
		if (!local.IsValid())
			return B_OK;
		local.OffsetBy(-fOrigin.x, -fOrigin.y);
	}

	_BlitRect(&fSource, fWriteTarget, local);

	if (!fHardwareCursorEnabled) {
		bool overlaysLocked = fFloatingOverlaysLock.Lock();
		_BlendCursor(&fSource, fWriteTarget, IntRect(local), fOrigin,
			fPanelOrientation, fPanelReflection);
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

	int32 count = region.CountRects();
	if (count == 0)
		return B_OK;

	if (fPageFlipEnabled) {
		_AddDamage(region);
		_Wake();
		return B_OK;
	}

	if (fRenderBuffer == NULL)
		return HWInterface::CopyBackToFront(region);

	for (int32 i = 0; i < count; i++)
		CopyBackToFront(region.RectAt(i));

	return B_OK;
}


void
DrmHWInterface::_DisableHardwareCursor()
{
	if (fFd < 0)
		return;

	// The node can be freed by a concurrent udev rescan.
	ModesetGuard modesetGuard(&fModesetMutex);
	struct modeset_dev* dev = fDev;
	if (dev == NULL)
		return;

	// Legacy ioctl can return success without clearing the plane FB on
	// atomic-capable drivers, so branch the same way the plane was armed.
	if (fCursorUsesAtomic)
		_ApplyCursorPlane(0, 0, 0);
	else
		drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
}


void
DrmHWInterface::SetCursor(ServerCursor* cursor)
{
	if (fDragBitmap.IsSet())
		return;

	// A software/hardware switch repaints what the old pointer covered.
	struct Switched {
		DrmHWInterface* self; bool was;
		~Switched() { if (self->fHardwareCursorEnabled != was) self->_Repaint(); }
	} switched = { this, fHardwareCursorEnabled };
	const IntRect oldFrame = _SoftCursorFrame();
	HWInterface::SetCursor(cursor);
	_AddOutputDamage((BRect)oldFrame);
	_AddOutputDamage((BRect)_SoftCursorFrame());
	_RefreshOutputSprites();

	// The node can be freed by a concurrent udev rescan. Taken after the
	// base call so the lock order stays modeset-before-overlays everywhere.
	ModesetGuard modesetGuard(&fModesetMutex);
	struct modeset_dev* dev = fDev;
	if (!dev)
		return;

	// The adopted output shows the pointer.
	if (fStandIn != 0) {
		fHardwareCursorEnabled = false;
		return;
	}

	if (cursor == NULL) {
		if (dev->cursor_ok)
			_DisableHardwareCursor();
		fHardwareCursorEnabled = false;
		return;
	}

	// A cursor bigger than the BO (large-font UI scaling) cannot be
	// cropped into the sprite without silently chopping it, so it is
	// declined the same way an unusable plane is: fall back to software.
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
	const uint32 dstStride = dev->cursor_pitch;
	for (int32 row = 0; row < ch; row++) {
		memcpy(dst + row * dstStride, src + row * cursor->BytesPerRow(),
			cw * 4);
	}

	const BPoint hot = cursor->GetHotSpot();
	const int32  px  = (int32)fCursorLocation.x - (int32)hot.x
		- (int32)fOrigin.x;
	const int32  py  = (int32)fCursorLocation.y - (int32)hot.y
		- (int32)fOrigin.y;

	int ret;
	if (fCursorUsesAtomic) {
		ret = _ApplyCursorPlane(dev->cursor_fb, px, py);
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
		// Another output has the pointer: this sprite stays hidden.
		fFirstSpriteParked = false;
		if (!fOutputs.empty()
				&& !_Frame().Intersects(_SpriteFrame(fCursorLocation))) {
			_DisableHardwareCursor();
			fFirstSpriteParked = true;
		}
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

	const IntRect oldFrame = _SoftCursorFrame();
	HWInterface::SetCursorVisible(visible);
	_AddOutputDamage((BRect)oldFrame);
	_AddOutputDamage((BRect)_SoftCursorFrame());
	_RefreshOutputSprites();

	// The node can be freed by a concurrent udev rescan.
	ModesetGuard modesetGuard(&fModesetMutex);
	struct modeset_dev* dev = fDev;
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
		const int32 px = (int32)fCursorLocation.x - (int32)hot.x
			- (int32)fOrigin.x;
		const int32 py = (int32)fCursorLocation.y - (int32)hot.y
			- (int32)fOrigin.y;

		// fHardwareCursorEnabled must track the actual result, or
		// _DrawCursor() and the hardware plane both draw at once.
		int ret;
		if (fCursorUsesAtomic) {
			ret = _ApplyCursorPlane(dev->cursor_fb, px, py);
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
	// The node can be freed by a concurrent udev rescan.
	ModesetGuard modesetGuard(&fModesetMutex);
	struct modeset_dev* dev = fDev;

	_AddOutputDamage((BRect)_SoftCursorFrame());

	if (bitmap != NULL) {
		// Disable the HW sprite before delegating to base so the
		// software composite path owns the drag visual.
		if (dev && dev->cursor_ok && fHardwareCursorEnabled)
			_DisableHardwareCursor();
		fHardwareCursorEnabled = false;
		fCursorObscured = false;
		fCursorVisible = true;
		HWInterface::SetDragBitmap(bitmap, offsetFromCursor);
		_Wake();
	} else {
		HWInterface::SetDragBitmap(bitmap, offsetFromCursor);
		// Force a full sprite content re-arm. SetCursor always
		// re-uploads cursor_map + drmModeSetCursor2 / _AtomicSetCursor
		// regardless of pointer identity, so even if fCursor is the
		// same ServerCursor the sprite is refreshed.
		if (fCursor.IsSet())
			SetCursor(fCursor);
	}

	_AddOutputDamage((BRect)_SoftCursorFrame());
}


void
DrmHWInterface::MoveCursorTo(float x, float y)
{
	BPoint hot(0, 0);
	if (fCursor.IsSet())
		hot = fCursor->GetHotSpot();

	const IntRect oldFrame = _SoftCursorFrame();

	// The software cursor blits into fWriteTarget; SetMode() replaces it
	// under the exclusive lock. HW lock first, then overlays, as SetMode().
	if (!LockParallelAccess())
		return;
	HWInterface::MoveCursorTo(x, y);
	_SyncOutputSprites();

	// Only an output blending the pointer needs it redrawn; one with a
	// sprite shown just moves it.
	if (fHardwareCursorEnabled && !fOutputs.empty()) {
		const BRect frames[2] = { (BRect)oldFrame, (BRect)_SoftCursorFrame() };
		fDirtyMutex.Lock();
		bool damaged = false;
		for (size_t i = 0; i < fOutputs.size(); i++) {
			Output* output = fOutputs[i];
			if (output->spriteShown)
				continue;

			for (int32 j = 0; j < 2; j++) {
				BRect part = frames[j] & output->Frame();
				if (part.IsValid()) {
					part.OffsetBy(-output->origin.x, -output->origin.y);
					output->dirty.Include(part);
					damaged = true;
				}
			}
		}
		fDirtyMutex.Unlock();
		if (damaged)
			_Wake();
	}
	UnlockParallelAccess();

	// When the HW sprite is off (software composite / drag), the base
	// MoveCursorTo already handled the cursor draw — no plane update.
	if (!fHardwareCursorEnabled)
		return;

	// The node can be freed by a concurrent udev rescan.
	ModesetGuard modesetGuard(&fModesetMutex);
	struct modeset_dev* dev = fDev;
	if (!dev || !dev->cursor_ok)
		return;

	// With other outputs, the sprite of the first one hides while the
	// pointer is on one of them.
	if (!fOutputs.empty() && !_Frame().Intersects(_SpriteFrame(BPoint(x, y)))) {
		if (!fFirstSpriteParked) {
			_DisableHardwareCursor();
			fFirstSpriteParked = true;
		}
		return;
	}
	if (fFirstSpriteParked) {
		fFirstSpriteParked = false;
		if (!fCursorUsesAtomic) {
			drmModeSetCursor(fFd, dev->crtc, dev->cursor_handle,
				dev->cursor_w, dev->cursor_h);
		}
	}

	const int32 px = (int32)floorf(x - hot.x) - (int32)fOrigin.x;
	const int32 py = (int32)floorf(y - hot.y) - (int32)fOrigin.y;

	if (fCursorUsesAtomic) {
		if (fAtomicBatch && _CommitInFlight()) {
			// A second non-blocking commit on a CRTC with a flip in
			// flight is answered with EBUSY; stage the update so it
			// rides the next frame commit instead.
			fDirtyMutex.Lock();
			fStagedCursor.valid = true;
			fStagedCursor.fb_id = dev->cursor_fb;
			fStagedCursor.x = px;
			fStagedCursor.y = py;
			fStagedCursor.w = dev->cursor_w;
			fStagedCursor.h = dev->cursor_h;
			fDirtyMutex.Unlock();
			// The completion event wakes the thread; a wake per move
			// made it spin on the exclusive lock.
		} else if (fAtomicBatch) {
			CursorUpdate update;
			update.valid = true;
			update.fb_id = dev->cursor_fb;
			update.x = px;
			update.y = py;
			update.w = dev->cursor_w;
			update.h = dev->cursor_h;
			// fb_id 0 leaves the primary plane out of the request;
			// committing the back buffer's fb would scan it out early.
			if (system_time() < fCursorRetryAt
					|| !_CommitCursorOnly(update)) {
				// A flip became pending between the check and
				// this commit; stage the move so the next frame
				// commit carries it instead of dropping it.
				fDirtyMutex.Lock();
				fStagedCursor = update;
				fDirtyMutex.Unlock();

				_Wake();
			}
		} else {
			_AtomicSetCursor(dev->cursor_fb, dev->crtc,
				px, py, dev->cursor_w, dev->cursor_h);
		}
	} else {
		drmModeMoveCursor(fFd, dev->crtc, px, py);
	}
}


void
DrmHWInterface::_DrawCursor(IntRect area) const
{
	if (fHardwareCursorEnabled)
		return;

	// Render-buffer scanout presents via _BlendCursor, which rotates.
	// The base path writes logical coords into the physical front buffer.
	if (fRenderBuffer != NULL)
		return;

	HWInterface::_DrawCursor(area);
}


// x..bottom are logical; the front buffer is physical. When the panel is
// transformed, writing them straight leaves unrotated patches on screen.
void
DrmHWInterface::_CopyToFront(uint8* src, uint32 srcBPR, int32 x, int32 y,
	int32 right, int32 bottom) const
{
	if (fPanelOrientation == PANEL_ORIENTATION_NORMAL
			&& fPanelReflection == B_PANEL_REFLECTION_NONE) {
		HWInterface::_CopyToFront(src, srcBPR, x, y, right, bottom);
		return;
	}

	RenderingBuffer* frontBuffer = FrontBuffer();
	if (frontBuffer == NULL || src == NULL)
		return;

	const int32 logicalW = fDisplayMode.virtual_width;
	const int32 logicalH = fDisplayMode.virtual_height;
	const int32 physW = (int32)frontBuffer->Width();
	const int32 physH = (int32)frontBuffer->Height();
	uint8* dstBase = (uint8*)frontBuffer->Bits();
	uint32 dstBPR = frontBuffer->BytesPerRow();

	for (int32 ly = y; ly <= bottom; ly++) {
		const uint8* s = src + (uint32)(ly - y) * srcBPR;
		for (int32 lx = x; lx <= right; lx++, s += 4) {
			int32 px, py;
			rotate_point(fPanelOrientation, lx, ly, logicalW, logicalH,
				px, py, fPanelReflection);
			if (px < 0 || py < 0 || px >= physW || py >= physH)
				continue;
			memcpy(dstBase + (uint32)py * dstBPR + (uint32)px * 4, s, 4);
		}
	}
}


// Rescans every connector and adds/removes modeset_dev entries to match; returns true if the primary
// connector's mode changed. Shared by hotplug and resume, as a monitor can change while asleep.
bool
DrmHWInterface::_RescanConnectors()
{
	if (fFd < 0)
		return false;

	struct modeset_dev* primary = fDev;
	uint32_t primaryConn = primary != NULL ? primary->conn : 0;
	bool resizePrimary = false;
	// The other outputs come and go with their screens: ScreenManager
	// adds and removes them, here it is only told that something moved.
	bool outputsChanged = false;

	drmModeRes* res = drmModeGetResources(fFd);
	if (res) {
		for (int i = 0; i < res->count_connectors; i++) {
			drmModeConnector* conn = drmModeGetConnector(fFd,
				res->connectors[i]);
			if (conn == NULL)
				continue;

			const uint32_t id = conn->connector_id;
			const bool connected = conn->connection == DRM_MODE_CONNECTED
				&& conn->count_modes > 0;
			drmModeFreeConnector(conn);

			// The first output keeps its node while unplugged: the
			// buffers we present with live in it.
			if (id == primaryConn) {
				if (connected && modeset_add_connector(fFd, id) == 1)
					resizePrimary = true;
				else if (!connected)
					outputsChanged = true;
				// Back from the adopted output.
				if (connected && fStandIn != 0)
					outputsChanged = true;
				continue;
			}

			fDirtyMutex.Lock();
			Output* output = _FindOutput(id);
			fDirtyMutex.Unlock();

			if (connected != (output != NULL))
				outputsChanged = true;
			else if (output != NULL && modeset_add_connector(fFd, id) == 1) {
				fDirtyMutex.Lock();
				output->modeChanged = true;
				fDirtyMutex.Unlock();
				outputsChanged = true;
			}
		}
		drmModeFreeResources(res);
	}

	// The other cards only have outputs beside the first one.
	for (size_t card = 0; card < fCards.size(); card++) {
		drmModeRes* cardRes = drmModeGetResources(fCards[card].fd);
		if (cardRes == NULL)
			continue;

		for (int i = 0; i < cardRes->count_connectors; i++) {
			drmModeConnector* conn = drmModeGetConnector(fCards[card].fd,
				cardRes->connectors[i]);
			if (conn == NULL)
				continue;

			const uint32 key = output_key(fCards[card].slot,
				conn->connector_id);
			const bool connected = conn->connection == DRM_MODE_CONNECTED
				&& conn->count_modes > 0;
			drmModeFreeConnector(conn);

			fDirtyMutex.Lock();
			Output* output = _FindOutput(key);
			fDirtyMutex.Unlock();

			if (connected != (output != NULL))
				outputsChanged = true;
			else if (output != NULL
					&& modeset_add_connector(fCards[card].fd,
						key_connector(key)) == 1) {
				fDirtyMutex.Lock();
				output->modeChanged = true;
				fDirtyMutex.Unlock();
				outputsChanged = true;
			}
		}
		drmModeFreeResources(cardRes);
	}

	if (outputsChanged && gScreenManager != NULL)
		gScreenManager->PostMessage(kMsgOutputsChanged);

	return resizePrimary;
}


void
DrmHWInterface::_HandleHotplug()
{
	struct udev_device* dev = udev_monitor_receive_device(fUdevMonitor);
	if (!dev)
		return;

	const char* hotplug = udev_device_get_property_value(dev, "HOTPLUG");
	const char* action = udev_device_get_property_value(dev, "ACTION");
	// QEMU resize and some docks send ACTION=change without HOTPLUG=1;
	// modeset_add_connector() decides whether anything moved.
	bool isHotplug = (hotplug != NULL && strcmp(hotplug, "1") == 0)
		|| (action != NULL && strcmp(action, "change") == 0);
	if (isHotplug && fFd >= 0) {
		fprintf(stderr, "DRM hotplug event\n");
		_ResetFlipState();

		// The uevent carries no CONNECTOR=, so every connector is
		// rescanned; scope the resize reaction to the one we render to.
		// The rescan adds and frees modeset_dev nodes: hold the modeset
		// mutex so a SetMode() on another thread cannot be left holding
		// a pointer this loop just freed.
		bool resizePrimary;
		pthread_mutex_lock(&fModesetMutex);
		resizePrimary = _RescanConnectors();
		pthread_mutex_unlock(&fModesetMutex);

		// Not master while inactive (VT-switched away); applying a mode
		// here would fail or steal master from whoever now owns it.
		if (resizePrimary && fSessionActive.load())
			_ScheduleResize();
		else if (fSessionActive.load())
			_RearmCursor();
	}

	udev_device_unref(dev);
}


// CLOCK_MONOTONIC stalls across a sleep and CLOCK_BOOTTIME does not, so a widening gap proves
// a suspend even without a DRM "change" uevent. Cheap enough to check on every event thread wakeup.
bool
DrmHWInterface::_CheckResume()
{
	struct timespec mono, boot;
	if (clock_gettime(CLOCK_MONOTONIC, &mono) != 0
			|| clock_gettime(CLOCK_BOOTTIME, &boot) != 0)
		return false;

	bigtime_t monoUs = (bigtime_t)mono.tv_sec * 1000000LL
		+ mono.tv_nsec / 1000;
	bigtime_t bootUs = (bigtime_t)boot.tv_sec * 1000000LL
		+ boot.tv_nsec / 1000;
	bigtime_t gap = bootUs - monoUs;

	bool resumed = fLastSuspendCheck != 0
		&& (gap - fLastSuspendCheck) > 2000000LL;
	fLastSuspendCheck = gap;
	return resumed;
}


// Runs on the DRM event thread after a detected resume or a post-resume hotplug uevent.
// Conservative: each step only acts if its piece of state looks stale.
void
DrmHWInterface::_OnResume()
{
	if (fFd < 0 || !fInitialized || !fSessionActive.load())
		return;

	fprintf(stderr, "[drm] resume detected, reconciling display state\n");
	_ResetFlipState();

	// The reconcile mutates the modeset list and re-commits buffers: it
	// must not overlap a SetMode() on the resize or app thread. If a mode
	// change is in flight the drain inside it bounds our wait at 250 ms,
	// after which its generation bump makes our late restore a no-op.
	if (!LockExclusiveAccess()) {
		// Mode lock unusable: at least re-arm the flip so a pending
		// damage does not sit on screen forever.
		fDirtyMutex.Lock();
		fNeedsFlip = true;
		fDirtyMutex.Unlock();
		_Wake();
		return;
	}

	// Order matches SetMode(): mode lock, then modeset mutex.
	ModesetGuard modesetGuard(&fModesetMutex);

	// Monitors may change while asleep with no guaranteed HOTPLUG uevent, so rescan unconditionally.
	// A changed mode on a known connector is not picked up (see modeset_add_connector()).
	bool resizePrimary = _RescanConnectors();
	if (resizePrimary) {
		UnlockExclusiveAccess();
		_ScheduleResize();
		return;
	}

	// Re-apply the mode only if the CRTC lost it: no mode or no fb means nothing is displayed.
	struct modeset_dev* dev = fDev;
	if (dev != NULL) {
		drmModeCrtc* crtc = drmModeGetCrtc(fFd, dev->crtc);
		bool crtcActive = crtc != NULL && crtc->mode_valid
			&& crtc->buffer_id != 0;
		if (crtc)
			drmModeFreeCrtc(crtc);
		// The commit state may differ from the hardware: remodeset.
		if (!crtcActive || fAtomicBatch)
			_RestoreDisplay();
		else
			_RestoreOutputs(true);
	}

	// The driver resets gamma LUTs on some paths, so re-apply night light if one was active.
	SetTemperature(fTemperature);

	// Force a full repaint, unless DPMS had the display deliberately off before sleeping.
	if (fDpmsState == B_DPMS_ON) {
		Invalidate(_DesktopBounds());
		UnlockExclusiveAccess();

		// The planes may have lost their state with the CRTC.
		_RearmCursor();
		_RearmOverlayPlanes();

		fDirtyMutex.Lock();
		fNeedsFlip = true;
		fDirtyMutex.Unlock();
		_Wake();
		return;
	}

	UnlockExclusiveAccess();
	_RearmCursor();
	_RearmOverlayPlanes();
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
		// Desktop::ScreenChanged() applies the preferred mode under the window
		// write lock and reclips every window before drawing resumes. Calling
		// SetMode() here first let windows draw into the new, smaller buffer
		// with clipping still sized for the old screen.
		fUserSetMode = false;
		_NotifyScreenChanged();
	}

	fResizeBusy.store(false);
}


status_t
DrmHWInterface::CreateLease(uint32_t* connectors, int connCount,
	uint32_t* crtcs, int crtcCount, int* leaseFd)
{
#ifndef HAVE_DRM_LEASE
	// drmModeCreateLease needs libdrm >= 2.4.95; there is no caller
	// inside app_server today, so declining is the whole fallback.
	return B_UNSUPPORTED;
#else
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
#endif
}


void
DrmHWInterface::RevokeLease(int leaseFd)
{
	if (leaseFd >= 0)
		close(leaseFd);
}


// virtio_gpu and vmwgfx keep a host copy of each framebuffer and ignore
// damage clips whenever the framebuffer changes.
void
DrmHWInterface::_SelectPresentMode()
{
	fDamageFlush = false;

	// VOS_DRM_PRESENT=flip|damage overrides the driver match.
	const char* forced = getenv("VOS_DRM_PRESENT");
	if (forced != NULL && forced[0] != '\0') {
		fDamageFlush = strcmp(forced, "damage") == 0;
	} else {
		drmVersionPtr version = drmGetVersion(fFd);
		if (version != NULL) {
			fDamageFlush = version->name != NULL
				&& (strcmp(version->name, "virtio_gpu") == 0
					|| strcmp(version->name, "vmwgfx") == 0);
			drmFreeVersion(version);
		}
	}

	printf("DRM: presenting by %s\n",
		fDamageFlush ? "damage flush" : "page flip");
}


void
DrmHWInterface::_Wake()
{
	if (fWakeFd < 0 || fWakeQueued.exchange(true))
		return;

	uint64_t v = 1;
	if (write(fWakeFd, &v, sizeof(v)) < 0)
		fWakeQueued = false;
}


uint32
DrmHWInterface::_DamageClips(const BRegion& damage, drm_mode_rect* clips,
	uint32 max) const
{
	return _DamageClips(damage, clips, max, fDisplayMode.virtual_width,
		fDisplayMode.virtual_height, fPanelOrientation, fPanelReflection);
}


uint32
DrmHWInterface::_DamageClips(const BRegion& damage, drm_mode_rect* clips,
	uint32 max, int32 logicalW, int32 logicalH, int32 orientation,
	int32 reflection)
{
	// Past the cap the bounding box stands in.
	BRegion box;
	const BRegion* region = &damage;
	if ((uint32)damage.CountRects() > max) {
		box.Set(damage.Frame());
		region = &box;
	}

	// The kernel rejects the whole commit for one clip outside the fb.
	uint32 count = 0;
	for (int32 i = 0; i < region->CountRects(); i++) {
		const BRect rect = region->RectAt(i);
		const int32 left = std::max((int32)rect.left, (int32)0);
		const int32 top = std::max((int32)rect.top, (int32)0);
		const int32 right = std::min((int32)rect.right, logicalW - 1);
		const int32 bottom = std::min((int32)rect.bottom, logicalH - 1);
		if (left > right || top > bottom)
			continue;

		int32 x1, y1, x2, y2;
		rotate_point(orientation, left, top, logicalW, logicalH,
			x1, y1, reflection);
		rotate_point(orientation, right, bottom, logicalW, logicalH,
			x2, y2, reflection);
		clips[count].x1 = std::min(x1, x2);
		clips[count].y1 = std::min(y1, y2);
		clips[count].x2 = std::max(x1, x2) + 1;
		clips[count].y2 = std::max(y1, y2) + 1;
		count++;
	}
	return count;
}


status_t
DrmHWInterface::_DirtyFlush(uint32_t fb_id, const BRegion& damage)
{
	drm_mode_rect rects[kMaxDamageClips];
	const uint32 count = _DamageClips(damage, rects, kMaxDamageClips);

	drm_clip_rect clips[kMaxDamageClips];
	for (uint32 i = 0; i < count; i++) {
		clips[i].x1 = rects[i].x1;
		clips[i].y1 = rects[i].y1;
		clips[i].x2 = rects[i].x2;
		clips[i].y2 = rects[i].y2;
	}

	// No clips flushes it all. ENOSYS: the scanout needs no flush.
	int ret = drmModeDirtyFB(fFd, fb_id, count > 0 ? clips : NULL, count);
	return ret == 0 || ret == -ENOSYS ? B_OK : B_ERROR;
}


void
DrmHWInterface::_ProbeAtomic()
{
	fAtomicSupported = false;
#ifdef HAVE_DRM_ATOMIC
	drmSetClientCap(fFd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	fAtomicSupported = (drmSetClientCap(fFd, DRM_CLIENT_CAP_ATOMIC, 1) == 0);
#endif
	printf("DRM: atomic modesetting %s\n",
		fAtomicSupported ? "supported" : "not supported (legacy fallback)");
}


bool
DrmHWInterface::_CommitInFlight()
{
	if (fPageFlipPending.load())
		return true;
	const bigtime_t at = fCursorInFlightAt.load();
	return at != 0 && system_time() - at < 4 * _FramePeriod();
}


// False when the update must be staged. Only a frame commit may count
// against the flip; a refused cursor stays on the hardware as it was.
bool
DrmHWInterface::_CommitCursorOnly(const CursorUpdate& update)
{
#ifdef HAVE_DRM_ATOMIC
	FlipToken* token = new FlipToken;
	token->hw = this;
	token->generation = 0;
	token->output = 0;
	token->cursorOnly = true;
	pthread_mutex_lock(&fFlipMutex);
	fFlipTokens.push_back(token);
	fCursorInFlightAt = system_time();
	pthread_mutex_unlock(&fFlipMutex);

	if (_AtomicCommitFrame(0, NULL, &update, NULL, token) == B_OK)
		return true;

	const int err = errno;
	pthread_mutex_lock(&fFlipMutex);
	for (size_t i = 0; i < fFlipTokens.size(); i++) {
		if (fFlipTokens[i] == token) {
			fFlipTokens.erase(fFlipTokens.begin() + i);
			break;
		}
	}
	fCursorInFlightAt = 0;
	pthread_mutex_unlock(&fFlipMutex);
	delete token;
	if (err == EINVAL) {
		static bool sLogged = false;
		if (!sLogged) {
			sLogged = true;
			fprintf(stderr, "[drm] cursor commit EINVAL, staged\n");
		}
		fCursorRetryAt = system_time() + _FramePeriod();
	} else
		_NoteCursorBusy(err);
#endif
	return false;
}


status_t
DrmHWInterface::SetVariableRefresh(bool enable)
{
	// VOS_VRR=1 wins over the setting.
	const char* vrr = getenv("VOS_VRR");
	const bool forced = vrr != NULL && vrr[0] != '\0'
		&& strcmp(vrr, "0") != 0;
	const bool wanted = enable || forced;
	if (!fVRRSupported)
		return B_UNSUPPORTED;
	if (wanted == fVRRWanted && wanted == fVRREnabled)
		return B_OK;

	fVRRWanted = wanted;
	if (!LockExclusiveAccess())
		return B_ERROR;
	_ApplyVRR();
	UnlockExclusiveAccess();
	return B_OK;
}


void
DrmHWInterface::_ApplyVRR()
{
	struct modeset_dev* dev = fDev;
	if (dev == NULL || !fAtomicSupported || !fCrtcProps.vrr_enabled
			|| fVRREnabled == fVRRWanted || !fSessionActive.load())
		return;

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	if (req == NULL)
		return;

	drmModeAtomicAddProperty(req, dev->crtc, fCrtcProps.vrr_enabled,
		fVRRWanted ? 1 : 0);
	const int ret = drmModeAtomicCommit(fFd, req,
		DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
	drmModeAtomicFree(req);
	if (ret == 0) {
		fVRREnabled = fVRRWanted;
		fprintf(stderr, "DRM: variable refresh %s\n",
			fVRREnabled ? "on" : "off");
	} else {
		fprintf(stderr, "DRM: variable refresh refused (%s)\n",
			strerror(errno));
		fVRRWanted = fVRREnabled;
	}
}


void
DrmHWInterface::_NoteCursorBusy(int err)
{
	fCursorRetryAt = system_time() + _FramePeriod();
	static bigtime_t sLast = 0;
	static uint32 sCount = 0;
	sCount++;
	const bigtime_t now = system_time();
	if (now - sLast < 1000000)
		return;
	sLast = now;
	fprintf(stderr, "[drm] cursor commit refused (%s), retrying; %u since "
		"start\n", strerror(err), (unsigned)sCount);
}


// Batched mode never fails over to the software pointer on a busy CRTC:
// the update is staged and rides the next commit. fb_id 0 hides the plane.
int
DrmHWInterface::_ApplyCursorPlane(uint32_t fb_id, int32 x, int32 y)
{
	struct modeset_dev* dev = fDev;
	if (dev == NULL)
		return -1;

	if (!fCursorUsesAtomic) {
		if (fb_id == 0)
			drmModeSetCursor(fFd, dev->crtc, 0, 0, 0);
		return 0;
	}

	if (!fAtomicBatch) {
		return _AtomicSetCursor(fb_id, fb_id != 0 ? dev->crtc : 0, x, y,
			dev->cursor_w, dev->cursor_h) == B_OK ? 0 : -1;
	}

	CursorUpdate update;
	update.valid = true;
	update.fb_id = fb_id;
	update.x = x;
	update.y = y;
	update.w = dev->cursor_w;
	update.h = dev->cursor_h;
	if (!_CommitInFlight() && system_time() >= fCursorRetryAt
			&& _CommitCursorOnly(update))
		return 0;

	fDirtyMutex.Lock();
	fStagedCursor = update;
	fDirtyMutex.Unlock();
	_Wake();
	return 0;
}


void
DrmHWInterface::_Repaint()
{
	// Drops a software pointer blended into the scanout buffers.
	fBackStale.MakeEmpty();
	_AddOutputDamage((BRect)_DesktopBounds());
	if (fOutputs.empty())
		Invalidate(_DesktopBounds());
}


void
DrmHWInterface::_ProbeCursor()
{
	struct modeset_dev* dev = fDev;
	if (dev == NULL || dev->cursor_handle == 0) {
		fprintf(stderr, "DRM: no cursor buffer, using software cursor\n");
		fHardwareCursorEnabled = false;
		return;
	}

	fprintf(stderr, "DRM: cursor buffer %ux%u pitch %u\n",
		dev->cursor_w, dev->cursor_h, dev->cursor_pitch);

	// An atomic cursor commit during a flip is answered with EBUSY; batching
	// merges the cursor into the frame's commit. VOS_ATOMIC_CURSOR tests the
	// unbatched path; VOS_LEGACY_CURSOR forces the legacy ioctls.
	static const char* sAtomicCursor = getenv("VOS_ATOMIC_CURSOR");
	static const char* sLegacyCursor = getenv("VOS_LEGACY_CURSOR");
	const bool legacyForced = sLegacyCursor != NULL && sLegacyCursor[0] != '\0'
		&& strcmp(sLegacyCursor, "0") != 0;
	const bool canAtomic = !legacyForced
		&& ((sAtomicCursor != NULL && sAtomicCursor[0] != '\0')
			|| fAtomicBatch)
		&& fAtomicSupported && fCursorPlaneId != 0 && dev->cursor_fb != 0;

	int r = -1;
	if (canAtomic) {
		r = (_AtomicSetCursor(dev->cursor_fb, dev->crtc,
			0, 0, dev->cursor_w, dev->cursor_h, true) == B_OK) ? 0 : -1;
		// Test only: a real commit would leave the plane showing at 0,0
		// when the disabling one is refused as busy.
		if (r == 0) {
			fprintf(stderr, "DRM: hardware cursor via atomic plane\n");
			fCursorUsesAtomic = true;
			fHardwareCursorEnabled = true;
			_Repaint();
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
		_Repaint();
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


// The (format, modifier) pairs of the plane's IN_FORMATS blob.
static void
_ReadInFormats(int fd, uint32_t planeId, std::vector<PlaneFormat>* _list)
{
	_list->clear();
	drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, planeId,
		DRM_MODE_OBJECT_PLANE);
	if (props == NULL)
		return;

	for (uint32_t i = 0; i < props->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[i]);
		if (prop == NULL)
			continue;

		const bool match = strcmp(prop->name, "IN_FORMATS") == 0;
		drmModeFreeProperty(prop);
		if (!match)
			continue;

		drmModePropertyBlobPtr blob = drmModeGetPropertyBlob(fd,
			(uint32_t)props->prop_values[i]);
		if (blob == NULL || blob->length < sizeof(drm_format_modifier_blob)) {
			if (blob != NULL)
				drmModeFreePropertyBlob(blob);
			break;
		}

		const uint8* base = (const uint8*)blob->data;
		const drm_format_modifier_blob* header
			= (const drm_format_modifier_blob*)base;
		const uint32_t* formats = (const uint32_t*)(base
			+ header->formats_offset);
		const drm_format_modifier* mods
			= (const drm_format_modifier*)(base + header->modifiers_offset);
		const size_t end = (size_t)header->modifiers_offset
			+ (size_t)header->count_modifiers * sizeof(drm_format_modifier);
		if (end <= blob->length && (size_t)header->formats_offset
				+ (size_t)header->count_formats * sizeof(uint32_t)
				<= blob->length) {
			for (uint32_t m = 0; m < header->count_modifiers; m++) {
				for (uint32_t b = 0; b < 64; b++) {
					if ((mods[m].formats & (1ull << b)) == 0)
						continue;

					const uint32_t index = mods[m].offset + b;
					if (index >= header->count_formats)
						continue;

					PlaneFormat entry = { formats[index], mods[m].modifier };
					_list->push_back(entry);
				}
			}
		}
		drmModeFreePropertyBlob(blob);
		break;
	}
	drmModeFreeObjectProperties(props);
}


// The plane's type and size limits.
static void
_ReadPlaneLimits(int fd, uint32_t planeId, OverlayPlaneInfo* info,
	uint64_t* _type)
{
	drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, planeId,
		DRM_MODE_OBJECT_PLANE);
	if (props == NULL)
		return;

	for (uint32_t j = 0; j < props->count_props; j++) {
		drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[j]);
		if (prop == NULL)
			continue;
		if (strcmp(prop->name, "type") == 0)
			*_type = props->prop_values[j];
		else if (strcmp(prop->name, "min") == 0)
			info->minWidth = info->minHeight = (uint32)props->prop_values[j];
		else if (strcmp(prop->name, "max") == 0)
			info->maxWidth = info->maxHeight = (uint32)props->prop_values[j];
		drmModeFreeProperty(prop);
	}
	drmModeFreeObjectProperties(props);
}


void
DrmHWInterface::_DiscoverProperties()
{
	struct modeset_dev* dev = fDev;
	if (!dev)
		return;

	int crtcIdx = _CrtcIndex(dev->crtc);
	std::vector<OverlayPlaneInfo> overlayPlanes;
	drmModePlaneResPtr planes = drmModeGetPlaneResources(fFd);
	if (planes) {
		for (uint32_t i = 0; i < planes->count_planes; i++) {
			drmModePlanePtr plane = drmModeGetPlane(fFd, planes->planes[i]);
			if (!plane)
				continue;
			bool forOurCrtc = (plane->possible_crtcs & (1u << crtcIdx)) != 0;
			if (!forOurCrtc) {
				drmModeFreePlane(plane);
				continue;
			}

			uint64_t typeValue = 0;
			OverlayPlaneInfo info = {};
			info.planeId = planes->planes[i];
			info.possibleCrtcs = plane->possible_crtcs;
			info.output = 0;
			info.fd = fFd;
			info.crtcId = dev->crtc;
			_ReadPlaneLimits(fFd, planes->planes[i], &info, &typeValue);

			if (typeValue == DRM_PLANE_TYPE_PRIMARY && !fPrimaryPlaneId) {
				fPrimaryPlaneId = planes->planes[i];
				_ReadInFormats(fFd, planes->planes[i], &fPrimaryFormats);
				_DiscoverPlaneProps(fFd, planes->planes[i], fPlaneProps);
			} else if (typeValue == DRM_PLANE_TYPE_CURSOR && !fCursorPlaneId) {
				fCursorPlaneId = planes->planes[i];
				_DiscoverPlaneProps(fFd, planes->planes[i], fCursorPlaneProps);
			} else if (typeValue == DRM_PLANE_TYPE_OVERLAY) {
				_DiscoverPlaneProps(fFd, planes->planes[i], info.props);
				_ReadInFormats(fFd, planes->planes[i], &info.modifiers);
				info.formats.assign(plane->formats,
					plane->formats + plane->count_formats);
				overlayPlanes.push_back(info);
			}
			drmModeFreePlane(plane);
		}
		drmModeFreePlaneResources(planes);
	}

	// Hotplug re-runs this; keep live claims, drop stale free entries.
	pthread_mutex_lock(&fOverlayMutex);
	std::vector<OverlayPlaneInfo> merged;
	for (size_t j = 0; j < fOverlayPlanes.size(); j++) {
		if (fOverlayPlanes[j].output != 0)
			merged.push_back(fOverlayPlanes[j]);
	}
	const size_t others = merged.size();
	for (size_t i = 0; i < overlayPlanes.size(); i++) {
		OverlayPlaneInfo* existing = NULL;
		for (size_t j = 0; j < fOverlayPlanes.size(); j++) {
			if (fOverlayPlanes[j].output == 0
					&& fOverlayPlanes[j].planeId == overlayPlanes[i].planeId)
				existing = &fOverlayPlanes[j];
		}
		if (existing != NULL && existing->claimed != NULL)
			merged.push_back(*existing);
		else
			merged.push_back(overlayPlanes[i]);
	}
	for (size_t j = 0; j < fOverlayPlanes.size(); j++) {
		if (fOverlayPlanes[j].claimed == NULL || fOverlayPlanes[j].output != 0)
			continue;
		bool stillThere = false;
		for (size_t i = others; i < merged.size(); i++) {
			if (merged[i].planeId == fOverlayPlanes[j].planeId)
				stillThere = true;
		}
		if (!stillThere) {
			fOverlayPlanes[j].claimed->Abandon();
			fOverlayPlanes[j].claimed->Disable();
			fOverlayPlanes[j].claimed->Free();
			delete fOverlayPlanes[j].claimed;
		}
	}
	fOverlayPlanes = merged;
	pthread_mutex_unlock(&fOverlayMutex);

	_DiscoverCrtcProps(dev->crtc);
	_DiscoverConnProps(dev->conn);

	// Scanout buffers of the gbm path take what the primary plane lists.
	free(dev->scanout_mods);
	dev->scanout_mods = NULL;
	dev->scanout_mod_count = 0;
	for (size_t i = 0; i < fPrimaryFormats.size(); i++) {
		if (fPrimaryFormats[i].format != DRM_FORMAT_XRGB8888
				|| fPrimaryFormats[i].modifier == DRM_FORMAT_MOD_INVALID)
			continue;

		uint64_t* grown = (uint64_t*)realloc(dev->scanout_mods,
			(dev->scanout_mod_count + 1) * sizeof(uint64_t));
		if (grown == NULL)
			break;

		dev->scanout_mods = grown;
		dev->scanout_mods[dev->scanout_mod_count++]
			= fPrimaryFormats[i].modifier;
	}

	printf("DRM: primary plane=%u cursor plane=%u overlay planes=%u "
		"crtc=%u conn=%u VRR=%s\n", fPrimaryPlaneId, fCursorPlaneId,
		(uint32)fOverlayPlanes.size(), dev->crtc, dev->conn,
		fVRRSupported ? "yes" : "no");
}


void
DrmHWInterface::_DiscoverPlaneProps(int fd, uint32_t plane_id,
	PlaneProps& props)
{
	memset(&props, 0, sizeof(props));
	drmModeObjectPropertiesPtr oprops = drmModeObjectGetProperties(
		fd, plane_id, DRM_MODE_OBJECT_PLANE);
	if (!oprops)
		return;
	for (uint32_t i = 0; i < oprops->count_props; i++) {
		drmModePropertyPtr prop = drmModeGetProperty(fd, oprops->props[i]);
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
			fVRREnabled   = fVRRWanted;
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

	struct modeset_dev* dev = fDev;
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

	// Everything above found the mounting; the user's rotation goes on top.
	fMountingOrientation = fPanelOrientation;
	fPanelOrientation = (enum PanelOrientation)panel_compose_orientation(
		fMountingOrientation, fUserOrientation);
}


int32
DrmHWInterface::PanelOrientation() const
{
	return fUserOrientation;
}


int32
DrmHWInterface::PanelScanoutOrientation() const
{
	return (int32)fPanelOrientation;
}


status_t
DrmHWInterface::SetPanelOrientation(int32 orientation)
{
	if (orientation < -1 || orientation > PANEL_ORIENTATION_RIGHT_UP)
		return B_BAD_VALUE;
	if (orientation < 0)
		orientation = B_PANEL_ORIENTATION_NORMAL;

	// The orientation must change under the mode lock: a page-flip blit
	// between a bare field write and SetMode()'s teardown would run
	// rotate_point() with the new orientation against the old buffer
	// sizes, and _BlendCursor()'s transformed path writes where the
	// unrotated destination does not reach. Nested write locks are
	// supported, so SetMode() can take it again inside.
	if (!LockExclusiveAccess())
		return B_ERROR;

	enum PanelOrientation previous = fPanelOrientation;
	int32 previousUser = fUserOrientation;
	fUserOrientation = orientation;
	fPanelOrientation = (enum PanelOrientation)panel_compose_orientation(
		fMountingOrientation, fUserOrientation);

	if (fPanelOrientation == previous) {
		UnlockExclusiveAccess();
		return B_OK;
	}

	struct modeset_dev* dev = fDev;
	if (dev == NULL) {
		fPanelOrientation = previous;
		fUserOrientation = previousUser;
		UnlockExclusiveAccess();
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
		fUserOrientation = previousUser;
		UnlockExclusiveAccess();
		return status;
	}

	UnlockExclusiveAccess();

	// SetMode() stays silent for app-initiated callers; nothing else will
	// tell Desktop that the frame and the input orientation just changed.
	// Outside the mode lock: the listener takes the window lock, and that
	// order must not invert against the drawing paths.
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

	// Same discipline as SetPanelOrientation: the blitters read this under
	// the mode lock, so the write must take it too or a flip can present
	// half the frame mirrored and half not.
	if (!LockExclusiveAccess())
		return B_ERROR;

	if (reflection == fPanelReflection) {
		UnlockExclusiveAccess();
		return B_OK;
	}

	fPanelReflection = reflection;
	UnlockExclusiveAccess();

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
	// No Kootigers N100 entry until its 90-degree direction is known;
	// VOS_PANEL_ORIENTATION=left-up|right-up overrides it at runtime.
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


#ifndef HAVE_DRM_ATOMIC
// Without the atomic API the backend builds legacy-only: _ProbeAtomic()
// never sets the cap, so every caller keeps to the SetCrtc/PageFlip path.
status_t
DrmHWInterface::_AtomicModeset(uint32_t /*fb_id*/, drmModeModeInfo* /*mode*/)
{
	return B_UNSUPPORTED;
}


status_t
DrmHWInterface::_AtomicSetCursor(uint32_t /*fb_id*/, uint32_t /*crtc_id*/,
	int32 /*x*/, int32 /*y*/, uint32_t /*w*/, uint32_t /*h*/)
{
	return B_UNSUPPORTED;
}


status_t
DrmHWInterface::_AtomicCommitFrame(uint32_t /*fb_id*/,
	const BRegion* /*damage*/, const CursorUpdate* /*cursor*/,
	const std::vector<PlaneUpdate>* /*planes*/, FlipToken* /*token*/)
{
	return B_UNSUPPORTED;
}
#else
status_t
DrmHWInterface::_AtomicModeset(uint32_t fb_id, drmModeModeInfo* mode)
{
	struct modeset_dev* dev = fDev;
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

	if (fCrtcProps.vrr_enabled)
		drmModeAtomicAddProperty(req, dev->crtc,     fCrtcProps.vrr_enabled,
			fVRREnabled ? 1 : 0);

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
	int32 x, int32 y, uint32_t w, uint32_t h, bool testOnly)
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

	int ret = drmModeAtomicCommit(fFd, req, testOnly
		? DRM_MODE_ATOMIC_TEST_ONLY : DRM_MODE_ATOMIC_NONBLOCK, NULL);
	drmModeAtomicFree(req);
	return ret == 0 ? B_OK : B_ERROR;
}


status_t
DrmHWInterface::_AtomicCommitFrame(uint32_t fb_id, const BRegion* damage,
	const CursorUpdate* cursor, const std::vector<PlaneUpdate>* planes,
	FlipToken* token)
{
	struct modeset_dev* dev = fDev;
	if (dev == NULL)
		return B_ERROR;

	// fb_id 0 means cursor-only: the primary plane keeps scanning out
	// whatever it has. Committing the back buffer's fb here would show
	// a half-drawn frame.
	if (fb_id == 0 && damage != NULL)
		return B_BAD_VALUE;

	drmModeAtomicReq* req = drmModeAtomicAlloc();
	if (!req)
		return B_NO_MEMORY;

	if (fb_id != 0) {
		if (!fPrimaryPlaneId || !fPlaneProps.fb_id) {
			drmModeAtomicFree(req);
			return B_ERROR;
		}
		drmModeAtomicAddProperty(req, fPrimaryPlaneId,
			fPlaneProps.crtc_id, dev->crtc);
		drmModeAtomicAddProperty(req, fPrimaryPlaneId,
			fPlaneProps.fb_id, fb_id);
	}

	// Without the property the plane counts as fully damaged.
	uint32_t blobId = 0;
	if (fb_id != 0 && damage != NULL && fPlaneProps.fb_damage_clips != 0) {
		drm_mode_rect clips[kMaxDamageClips];
		const uint32 count = _DamageClips(*damage, clips, kMaxDamageClips);
		if (count > 0 && drmModeCreatePropertyBlob(fFd, clips,
				count * sizeof(clips[0]), &blobId) == 0)
			drmModeAtomicAddProperty(req, fPrimaryPlaneId,
				fPlaneProps.fb_damage_clips, blobId);
	}

	if (cursor != NULL && cursor->valid && fCursorPlaneId != 0
			&& fCursorPlaneProps.fb_id != 0) {
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.crtc_id, cursor->fb_id != 0 ? dev->crtc : 0);
		drmModeAtomicAddProperty(req, fCursorPlaneId,
			fCursorPlaneProps.fb_id, cursor->fb_id);

		if (cursor->fb_id != 0) {
			drmModeAtomicAddProperty(req, fCursorPlaneId,
				fCursorPlaneProps.crtc_x,
				(uint64_t)(int64_t)cursor->x);
			drmModeAtomicAddProperty(req, fCursorPlaneId,
				fCursorPlaneProps.crtc_y,
				(uint64_t)(int64_t)cursor->y);
			drmModeAtomicAddProperty(req, fCursorPlaneId,
				fCursorPlaneProps.crtc_w, cursor->w);
			drmModeAtomicAddProperty(req, fCursorPlaneId,
				fCursorPlaneProps.crtc_h, cursor->h);
			drmModeAtomicAddProperty(req, fCursorPlaneId,
				fCursorPlaneProps.src_x, 0);
			drmModeAtomicAddProperty(req, fCursorPlaneId,
				fCursorPlaneProps.src_y, 0);
			drmModeAtomicAddProperty(req, fCursorPlaneId,
				fCursorPlaneProps.src_w,
				(uint64_t)cursor->w << 16);
			drmModeAtomicAddProperty(req, fCursorPlaneId,
				fCursorPlaneProps.src_h,
				(uint64_t)cursor->h << 16);
		}
	}

	for (size_t i = 0; planes != NULL && i < planes->size(); i++) {
		const PlaneUpdate& p = (*planes)[i];
		const PlaneProps& pp = p.props;
		if (pp.crtc_id == 0 || pp.fb_id == 0)
			continue;
		drmModeAtomicAddProperty(req, p.planeId, pp.crtc_id,
			p.enable ? dev->crtc : 0);
		drmModeAtomicAddProperty(req, p.planeId, pp.fb_id,
			p.enable ? p.fb_id : 0);
		if (!p.enable)
			continue;
		drmModeAtomicAddProperty(req, p.planeId, pp.src_x, p.srcX);
		drmModeAtomicAddProperty(req, p.planeId, pp.src_y, p.srcY);
		drmModeAtomicAddProperty(req, p.planeId, pp.src_w, p.srcW);
		drmModeAtomicAddProperty(req, p.planeId, pp.src_h, p.srcH);
		drmModeAtomicAddProperty(req, p.planeId, pp.crtc_x,
			(uint64_t)(int64_t)p.crtcX);
		drmModeAtomicAddProperty(req, p.planeId, pp.crtc_y,
			(uint64_t)(int64_t)p.crtcY);
		drmModeAtomicAddProperty(req, p.planeId, pp.crtc_w, p.crtcW);
		drmModeAtomicAddProperty(req, p.planeId, pp.crtc_h, p.crtcH);
	}

	uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK;
	if (token != NULL)
		flags |= DRM_MODE_PAGE_FLIP_EVENT;

	int ret = drmModeAtomicCommit(fFd, req, flags, token);
	// The caller classifies the failure by errno.
	const int commitErrno = errno;
	static bool sLoggedInvalid = false;
	if (ret != 0 && commitErrno == EINVAL && !sLoggedInvalid) {
		sLoggedInvalid = true;
		fprintf(stderr, "[drm] atomic commit EINVAL: crtc %u fb %u "
			"cursor %d (fb %u) planes %u damage %u\n", dev->crtc, fb_id,
			cursor != NULL && cursor->valid,
			cursor != NULL ? cursor->fb_id : 0,
			(unsigned)(planes != NULL ? planes->size() : 0), blobId);
	}
	drmModeAtomicFree(req);
	if (blobId != 0)
		drmModeDestroyPropertyBlob(fFd, blobId);
	errno = commitErrno;

	return ret == 0 ? B_OK : B_ERROR;
}
#endif // HAVE_DRM_ATOMIC


// #pragma mark - overlay planes


// DRM fourcc codes describe memory layout; the legacy little-endian names
// match the XRGB/ARGB layouts on a LE host.
static bool
_FourccForColorSpace(color_space space, uint32* _fourcc, uint32* _bpp)
{
	uint32 fourcc = 0;
	uint32 bpp = 32;

	switch (space) {
		case B_RGB32:
			fourcc = DRM_FORMAT_XRGB8888;
			break;
		case B_RGBA32:
			fourcc = DRM_FORMAT_ARGB8888;
			break;
		case B_RGB24:
			fourcc = DRM_FORMAT_BGR888;
			bpp = 24;
			break;
		case B_RGB16:
			fourcc = DRM_FORMAT_RGB565;
			bpp = 16;
			break;
		case B_RGB15:
			fourcc = DRM_FORMAT_XRGB1555;
			bpp = 16;
			break;
		case B_RGBA15:
			fourcc = DRM_FORMAT_ARGB1555;
			bpp = 16;
			break;
		case B_GRAY8:
			fourcc = DRM_FORMAT_C8;
			bpp = 8;
			break;
		case B_YUV422:
			fourcc = DRM_FORMAT_YUYV;
			bpp = 16;
			break;
		case B_YUV420:
			fourcc = DRM_FORMAT_YUV420;
			bpp = 8;
			break;
		case B_YUV444:
			fourcc = DRM_FORMAT_YUV444;
			bpp = 24;
			break;
		default:
			return false;
	}

	*_fourcc = fourcc;
	*_bpp = bpp;
	return true;
}


static void
_DestroyDumb(int fd, uint32_t handle)
{
	if (handle == 0)
		return;

	struct drm_mode_destroy_dumb destroy = {};
	destroy.handle = handle;
	drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}


static void
_CloseGem(int fd, uint32 handle)
{
	struct drm_gem_close close = {};
	close.handle = handle;
	drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &close);
}


DrmOverlayPlane::DrmOverlayPlane(DrmHWInterface* interface, uint32_t planeId,
	const PlaneProps& props, uint32_t output, int fd, uint32_t crtc)
	:
	fInterface(interface),
	fFd(fd),
	fPlaneId(planeId),
	fProps(props),
	fOutput(output),
	fCrtc(crtc),
	fSharedDevice(true),
	fHandle(0),
	fFbId(0),
	fShowFb(0),
	fSerial(0),
	fListener(NULL),
	fAdopted(false),
	fCloseHandle(false),
	fBits(NULL),
	fSize(0),
	fStride(0),
	fWidth(0),
	fHeight(0),
	fColorSpace(B_RGB32),
	fArmed(false),
	fHasGeometry(false),
	fSource(0, 0, -1, -1),
	fDestination(0, 0, -1, -1)
{
}


DrmOverlayPlane::~DrmOverlayPlane()
{
	Free();
}


status_t
DrmOverlayPlane::Allocate(uint32 width, uint32 height, uint32 fourcc,
	uint32 bpp, color_space space)
{
	if (width == 0 || height == 0)
		return B_BAD_VALUE;

	struct drm_mode_create_dumb create = {};
	create.width = width;
	create.height = height;
	create.bpp = bpp;
	if (drmIoctl(fFd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
		return errno == ENOMEM ? B_NO_MEMORY : B_ERROR;

	struct drm_mode_map_dumb map = {};
	map.handle = create.handle;
	if (drmIoctl(fFd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
		_DestroyDumb(fFd, create.handle);
		return B_ERROR;
	}

	void* bits = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED,
		fFd, map.offset);
	if (bits == MAP_FAILED) {
		_DestroyDumb(fFd, create.handle);
		return B_ERROR;
	}

	uint32 handles[4] = { create.handle, 0, 0, 0 };
	uint32 pitches[4] = { create.pitch, 0, 0, 0 };
	uint32 offsets[4] = { 0, 0, 0, 0 };
	uint32 fbId = 0;
	if (drmModeAddFB2(fFd, width, height, fourcc, handles, pitches,
			offsets, &fbId, 0) < 0) {
		munmap(bits, create.size);
		_DestroyDumb(fFd, create.handle);
		return B_ERROR;
	}

	fHandle = create.handle;
	fFbId = fbId;
	fBits = (uint8_t*)bits;
	fSize = create.size;
	fStride = create.pitch;
	fWidth = width;
	fHeight = height;
	fColorSpace = space;
	return B_OK;
}


status_t
DrmOverlayPlane::Adopt(int dmabufFd, uint32 width, uint32 height,
	uint32 fourcc, uint32 stride, color_space space, uint32 sharedHandle)
{
	if (dmabufFd < 0 || width == 0 || height == 0 || stride == 0
			|| fFbId != 0)
		return B_BAD_VALUE;

	uint32 handle = 0;
	if (drmPrimeFDToHandle(fFd, dmabufFd, &handle) < 0)
		return B_ERROR;

	bool closeHandle = handle != sharedHandle;

	uint32 handles[4] = { handle, 0, 0, 0 };
	uint32 pitches[4] = { stride, 0, 0, 0 };
	uint32 offsets[4] = { 0, 0, 0, 0 };
	uint32 fbId = 0;
	if (drmModeAddFB2(fFd, width, height, fourcc, handles, pitches,
			offsets, &fbId, 0) < 0) {
		if (closeHandle)
			_CloseGem(fFd, handle);
		return B_ERROR;
	}

	fHandle = handle;
	fFbId = fbId;
	fAdopted = true;
	fCloseHandle = closeHandle;
	fStride = stride;
	fWidth = width;
	fHeight = height;
	fColorSpace = space;
	return B_OK;
}


void
DrmOverlayPlane::Free()
{
	// Drop any staged change and unbind at once; the fb goes away below.
	fListener = NULL;
	fInterface->_DropStagedPlane(fOutput, fPlaneId);
	if (fFbId != 0 && fHasGeometry)
		_UnbindNow();
	fArmed = false;

	for (size_t i = 0; i < fExtra.size(); i++) {
		drmModeRmFB(fFd, fExtra[i].fbId);
		if (fExtra[i].closeHandle)
			_CloseGem(fFd, fExtra[i].handle);
	}
	fExtra.clear();
	fShowFb = 0;

	if (fFbId != 0) {
		drmModeRmFB(fFd, fFbId);
		fFbId = 0;
	}
	if (fBits != NULL && fSize != 0) {
		munmap(fBits, fSize);
		fBits = NULL;
		fSize = 0;
	}
	if (fAdopted) {
		if (fCloseHandle)
			_CloseGem(fFd, fHandle);
		fAdopted = false;
		fCloseHandle = false;
	} else
		_DestroyDumb(fFd, fHandle);
	fHandle = 0;
	fStride = 0;
	fWidth = 0;
	fHeight = 0;
}


void*
DrmOverlayPlane::Bits() const
{
	return fBits;
}


uint32
DrmOverlayPlane::BytesPerRow() const
{
	return fStride;
}


uint32
DrmOverlayPlane::Width() const
{
	return fWidth;
}


uint32
DrmOverlayPlane::Height() const
{
	return fHeight;
}


color_space
DrmOverlayPlane::ColorSpace() const
{
	return fColorSpace;
}


status_t
DrmOverlayPlane::Configure(const BRect& source, const BRect& destination)
{
	if (fFbId == 0)
		return B_NO_INIT;

	fSource = source;
	fDestination = destination;
	fHasGeometry = true;

	if (fProps.crtc_id == 0 || fProps.fb_id == 0)
		return B_NO_INIT;

	// The compositor only rotates in software; a plane would scan out
	// untransformed. Decline so the caller stays on the CPU path.
	if (fOutput == 0 && (fInterface->fPanelOrientation
			!= PANEL_ORIENTATION_NORMAL || fInterface->fPanelReflection != 0))
		return B_UNSUPPORTED;

	int fd;
	uint32_t crtc;
	BPoint origin;
	if (!fInterface->_PlaneTarget(fOutput, &fd, &crtc, &origin))
		return B_NO_INIT;

	uint32 srcX = (uint32)source.left << 16;
	uint32 srcY = (uint32)source.top << 16;
	uint32 srcW = (uint32)(source.IntegerWidth() + 1) << 16;
	uint32 srcH = (uint32)(source.IntegerHeight() + 1) << 16;
	int32 crtcX = (int32)destination.left - (int32)origin.x;
	int32 crtcY = (int32)destination.top - (int32)origin.y;
	uint32 crtcW = (uint32)destination.IntegerWidth() + 1;
	uint32 crtcH = (uint32)destination.IntegerHeight() + 1;

	PlaneUpdate update = {};
	update.planeId = fPlaneId;
	update.props = fProps;
	update.output = fOutput;
	update.fd = fd;
	update.crtc = crtc;
	update.enable = true;
	update.fb_id = fShowFb != 0 ? fShowFb : fFbId;
	update.serial = fSerial;
	fSerial = 0;
	update.crtcX = crtcX;
	update.crtcY = crtcY;
	update.crtcW = crtcW;
	update.crtcH = crtcH;
	update.srcX = srcX;
	update.srcY = srcY;
	update.srcW = srcW;
	update.srcH = srcH;

	// Staged changes ride the next frame commit; otherwise apply now.
	if (!fInterface->_StagePlane(update)) {
		bool locked = fInterface->LockExclusiveAccess();
		fInterface->_SetPlaneNow(update);
		if (locked)
			fInterface->UnlockExclusiveAccess();

		if (update.serial != 0) {
			std::vector<PlaneNote> notes;
			PlaneNote note = { fPlaneId, update.serial, fOutput };
			notes.push_back(note);
			fInterface->_NotifyPlanes(notes, true, system_time());
		}
	}

	fArmed = true;
	return B_OK;
}


status_t
DrmOverlayPlane::AddBuffer(int dmabufFd, uint32 width, uint32 height,
	uint32 fourcc, uint32 stride, uint32 sharedHandle, int32* _index)
{
	if (fFbId == 0 || !fAdopted || dmabufFd < 0 || stride == 0
			|| width != fWidth || height != fHeight || _index == NULL
			|| fExtra.size() >= 7)
		return B_BAD_VALUE;

	uint32 handle = 0;
	if (drmPrimeFDToHandle(fFd, dmabufFd, &handle) < 0)
		return B_ERROR;

	const bool closeHandle = !fSharedDevice || handle != sharedHandle;
	uint32 handles[4] = { handle, 0, 0, 0 };
	uint32 pitches[4] = { stride, 0, 0, 0 };
	uint32 offsets[4] = { 0, 0, 0, 0 };
	uint32 fbId = 0;
	if (drmModeAddFB2(fFd, width, height, fourcc, handles, pitches,
			offsets, &fbId, 0) < 0) {
		if (closeHandle)
			_CloseGem(fFd, handle);
		return B_ERROR;
	}

	ExtraBuffer extra = { fbId, handle, closeHandle };
	try {
		fExtra.push_back(extra);
	} catch (std::bad_alloc&) {
		drmModeRmFB(fFd, fbId);
		if (closeHandle)
			_CloseGem(fFd, handle);
		return B_NO_MEMORY;
	}

	*_index = (int32)fExtra.size();
	return B_OK;
}


status_t
DrmOverlayPlane::Show(int32 index, uint64 serial, const BRect& source,
	const BRect& destination)
{
	if (index < 0 || (size_t)index > fExtra.size())
		return B_BAD_VALUE;

	fShowFb = index == 0 ? fFbId : fExtra[index - 1].fbId;
	fSerial = serial;
	status_t status = Configure(source, destination);
	if (status != B_OK)
		fSerial = 0;
	return status;
}


void
DrmOverlayPlane::SetListener(OverlayPlaneListener* listener)
{
	fListener = listener;
}


void
DrmOverlayPlane::Abandon()
{
	OverlayPlaneListener* listener = fListener;
	fListener = NULL;
	if (listener != NULL)
		listener->PlaneLost();
}


status_t
DrmOverlayPlane::Reapply()
{
	if (!fHasGeometry)
		return B_OK;

	return Configure(fSource, fDestination);
}


void
DrmOverlayPlane::Disable()
{
	if (!fArmed)
		return;

	// CRTC_ID 0 unbinds the plane; the buffer and claim stay.
	PlaneUpdate update = {};
	update.planeId = fPlaneId;
	update.props = fProps;
	update.output = fOutput;
	update.fd = fFd;
	update.crtc = fCrtc;
	if (!fInterface->_StagePlane(update))
		_UnbindNow();

	fArmed = false;
}


void
DrmOverlayPlane::_UnbindNow()
{
	bool locked = fInterface->LockExclusiveAccess();
	PlaneUpdate update = {};
	update.planeId = fPlaneId;
	update.output = fOutput;
	update.fd = fFd;
	update.crtc = fCrtc;
	fInterface->_SetPlaneNow(update);
	if (locked)
		fInterface->UnlockExclusiveAccess();
}


void
DrmHWInterface::_DropStagedPlane(uint32_t output, uint32_t planeId)
{
	fDirtyMutex.Lock();
	for (size_t i = 0; i < fStagedPlanes.size(); i++) {
		if (fStagedPlanes[i].planeId == planeId
				&& fStagedPlanes[i].output == output) {
			fStagedPlanes.erase(fStagedPlanes.begin() + i);
			break;
		}
	}
	fDirtyMutex.Unlock();
}


bool
DrmHWInterface::_StagePlane(const PlaneUpdate& update)
{
	// The other outputs flip on their own; their planes are set at once.
	if (update.output != 0 || !fAtomicSupported || fPrimaryPlaneId == 0
			|| fVblankPacedFlip
			|| !fSessionActive || !fRunning || !fPageFlipEnabled)
		return false;

	std::vector<PlaneNote> dropped;
	PlaneUpdate stamped = update;
	stamped.staged = system_time();
	fDirtyMutex.Lock();
	bool replaced = false;
	for (size_t i = 0; i < fStagedPlanes.size(); i++) {
		if (fStagedPlanes[i].planeId == update.planeId) {
			PlaneUpdate merged = stamped;
			if (fStagedPlanes[i].serial != 0) {
				if (update.serial == 0 && update.enable
						&& update.fb_id == fStagedPlanes[i].fb_id) {
					// Same buffer again: it still has to be reported.
					merged.serial = fStagedPlanes[i].serial;
				} else {
					// Never reached the screen.
					PlaneNote note = { update.planeId,
						fStagedPlanes[i].serial, 0 };
					dropped.push_back(note);
				}
			}
			fStagedPlanes[i] = merged;
			replaced = true;
		}
	}
	if (!replaced)
		fStagedPlanes.push_back(stamped);
	fNeedsFlip = true;
	fDirtyMutex.Unlock();

	_Wake();
	if (!dropped.empty())
		_NotifyPlanes(dropped, false, 0);
	return true;
}


void
DrmHWInterface::_NotifyPlanes(const std::vector<PlaneNote>& notes, bool shown,
	bigtime_t when)
{
	pthread_mutex_lock(&fOverlayMutex);
	for (size_t i = 0; i < notes.size(); i++) {
		OverlayPlaneInfo* info = _FindOverlayPlaneInfo(notes[i].output,
			notes[i].planeId);
		if (info == NULL || info->claimed == NULL
				|| info->claimed->Listener() == NULL)
			continue;

		if (shown)
			info->claimed->Listener()->PlaneShown(notes[i].serial, when);
		else
			info->claimed->Listener()->PlaneDropped(notes[i].serial);
	}
	pthread_mutex_unlock(&fOverlayMutex);
}


BRect
DrmHWInterface::OverlayOutputFrame(const BRect& where)
{
	uint32 output;
	BRect frame;
	return _OutputFor(where, &output, &frame) ? frame : BRect();
}


bool
DrmHWInterface::HasOverlayPlane(const BRect& where)
{
	uint32 output;
	BRect frame;
	if (!_OutputFor(where, &output, &frame))
		return false;

	pthread_mutex_lock(&fOverlayMutex);
	bool found = false;
	for (size_t i = 0; i < fOverlayPlanes.size() && !found; i++) {
		const OverlayPlaneInfo& info = fOverlayPlanes[i];
		found = info.output == output
			&& std::find(info.formats.begin(), info.formats.end(),
				(uint32)DRM_FORMAT_XRGB8888) != info.formats.end()
			&& _PlaneAccepts(info, DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR);
	}
	pthread_mutex_unlock(&fOverlayMutex);
	return found;
}


// An empty rectangle is the first output. Outputs that rotate, sleep or
// are not set up cannot take a plane.
bool
DrmHWInterface::_OutputFor(const BRect& where, uint32* _output, BRect* _frame)
{
	const bool any = !where.IsValid();

	if (fStandIn == 0 && fDev != NULL && fPanelOrientation
			== PANEL_ORIENTATION_NORMAL && fPanelReflection == 0
			&& fDpmsState == B_DPMS_ON) {
		const BRect frame = _Frame();
		if (any || frame.Contains(where)) {
			*_output = 0;
			*_frame = frame;
			return true;
		}
	}

	bool found = false;
	fDirtyMutex.Lock();
	for (size_t i = 0; i < fOutputs.size() && !found; i++) {
		Output* output = fOutputs[i];
		if (output->target == NULL || output->dpms != B_DPMS_ON
				|| output->orientation != PANEL_ORIENTATION_NORMAL
				|| output->reflection != B_PANEL_REFLECTION_NONE)
			continue;

		const BRect frame = output->Frame();
		if (any || frame.Contains(where)) {
			*_output = output->connector;
			*_frame = frame;
			found = true;
		}
	}
	fDirtyMutex.Unlock();
	return found;
}


// Card, CRTC and place of an output the planes scan out on.
bool
DrmHWInterface::_PlaneTarget(uint32 output, int* _fd, uint32_t* _crtc,
	BPoint* _origin)
{
	if (output == 0) {
		struct modeset_dev* dev = fDev;
		if (dev == NULL)
			return false;

		*_fd = fFd;
		*_crtc = dev->crtc;
		*_origin = fOrigin;
		return true;
	}

	bool found = false;
	fDirtyMutex.Lock();
	Output* target = _FindOutput(output);
	if (target != NULL && target->orientation == PANEL_ORIENTATION_NORMAL
			&& target->reflection == B_PANEL_REFLECTION_NONE) {
		*_fd = target->fd;
		*_crtc = target->dev->crtc;
		*_origin = target->origin;
		found = true;
	}
	fDirtyMutex.Unlock();
	return found;
}


#ifdef HAVE_GBM
struct gbm_device*
DrmHWInterface::GbmDeviceFor(const BRect& where)
{
	int slot = 0;
	if (where.IsValid()) {
		fDirtyMutex.Lock();
		for (size_t i = 0; i < fOutputs.size(); i++) {
			if (fOutputs[i]->Frame().Contains(where))
				slot = fOutputs[i]->slot;
		}
		fDirtyMutex.Unlock();
	}
	if (slot == 0)
		return GbmDevice();

	struct gbm_device* device = NULL;
	fDirtyMutex.Lock();
	for (size_t i = 0; i < fCards.size(); i++) {
		if (fCards[i].slot != slot)
			continue;
		if (fCards[i].gbm == NULL)
			fCards[i].gbm = gbm_create_device(fCards[i].fd);
		device = fCards[i].gbm;
	}
	fDirtyMutex.Unlock();
	return device != NULL ? device : GbmDevice();
}
#endif


// Exclusive lock held; the output has its buffers.
void
DrmHWInterface::_DiscoverOutputPlanes(Output* output)
{
	if (output->dev == NULL)
		return;

	drmSetClientCap(output->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	int crtcIdx = -1;
	drmModeRes* res = drmModeGetResources(output->fd);
	if (res != NULL) {
		for (int i = 0; i < res->count_crtcs; i++) {
			if (res->crtcs[i] == output->dev->crtc)
				crtcIdx = i;
		}
		drmModeFreeResources(res);
	}
	if (crtcIdx < 0)
		return;

	std::vector<OverlayPlaneInfo> found;
	drmModePlaneResPtr planes = drmModeGetPlaneResources(output->fd);
	for (uint32_t i = 0; planes != NULL && i < planes->count_planes; i++) {
		drmModePlanePtr plane = drmModeGetPlane(output->fd, planes->planes[i]);
		if (plane == NULL)
			continue;

		uint64_t type = 0;
		OverlayPlaneInfo info = {};
		info.planeId = planes->planes[i];
		info.possibleCrtcs = plane->possible_crtcs;
		info.output = output->connector;
		info.fd = output->fd;
		info.crtcId = output->dev->crtc;
		if ((plane->possible_crtcs & (1u << crtcIdx)) != 0) {
			_ReadPlaneLimits(output->fd, planes->planes[i], &info, &type);
			if (type == DRM_PLANE_TYPE_OVERLAY) {
				_DiscoverPlaneProps(output->fd, planes->planes[i],
					info.props);
				_ReadInFormats(output->fd, planes->planes[i], &info.modifiers);
				info.formats.assign(plane->formats,
					plane->formats + plane->count_formats);
				found.push_back(info);
			}
		}
		drmModeFreePlane(plane);
	}
	if (planes != NULL)
		drmModeFreePlaneResources(planes);

	pthread_mutex_lock(&fOverlayMutex);
	for (size_t i = 0; i < found.size(); i++)
		fOverlayPlanes.push_back(found[i]);
	pthread_mutex_unlock(&fOverlayMutex);

	fprintf(stderr, "DRM: connector %u has %u overlay planes\n",
		(unsigned)output->connector, (unsigned)found.size());
}


void
DrmHWInterface::_SetPlaneNow(const PlaneUpdate& update)
{
	if (update.fd < 0)
		return;

	if (update.enable) {
		drmModeSetPlane(update.fd, update.planeId, update.crtc, update.fb_id,
			0, update.crtcX, update.crtcY, update.crtcW, update.crtcH,
			update.srcX, update.srcY, update.srcW, update.srcH);
	} else {
		drmModeSetPlane(update.fd, update.planeId, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0);
	}
}


// #pragma mark - HWInterface overlay plumbing on top of the planes


overlay_token
DrmHWInterface::AcquireOverlayChannel()
{
	// Identity only; the plane claim happens in AllocateOverlayBuffer().
	pthread_mutex_lock(&fOverlayMutex);
	overlay_token token = (overlay_token)(intptr_t)++fOverlayChannelCounter;
	fOverlayChannels.push_back(token);
	pthread_mutex_unlock(&fOverlayMutex);
	return token;
}


void
DrmHWInterface::ReleaseOverlayChannel(overlay_token token)
{
	if (token == NULL)
		return;

	pthread_mutex_lock(&fOverlayMutex);
	for (size_t i = 0; i < fOverlayChannels.size(); i++) {
		if (fOverlayChannels[i] == token) {
			fOverlayChannels.erase(fOverlayChannels.begin() + i);
			break;
		}
	}
	pthread_mutex_unlock(&fOverlayMutex);
}


status_t
DrmHWInterface::AcquireOverlayPlane(color_space space, uint32 width,
	uint32 height, OverlayPlane** _plane)
{
	if (_plane == NULL)
		return B_BAD_VALUE;
	*_plane = NULL;

	uint32 fourcc = 0;
	uint32 bpp = 0;
	if (!_FourccForColorSpace(space, &fourcc, &bpp))
		return B_UNSUPPORTED;

	return _ClaimOverlayPlane(width, height, fourcc, bpp, space, -1, 0, 0,
		_plane);
}


status_t
DrmHWInterface::AcquireOverlayPlaneForBitmap(ServerBitmap* bitmap,
	OverlayPlane** _plane, const BRect& destination)
{
	if (_plane == NULL)
		return B_BAD_VALUE;
	*_plane = NULL;

#ifdef HAVE_GBM
	GbmMemory* memory = bitmap != NULL ? bitmap->GbmBuffer() : NULL;
	if (memory == NULL)
		return B_UNSUPPORTED;

	// A view on two outputs stays with the painter.
	uint32 output;
	BRect frame;
	if (!_OutputFor(destination, &output, &frame))
		return B_BUSY;

	int fd = memory->ExportFd();
	if (fd < 0)
		return B_ERROR;

	status_t status = _ClaimOverlayPlane(memory->Width(), memory->Height(),
		memory->Format(), 0, bitmap->ColorSpace(), fd, memory->Stride(),
		memory->Handle(), _plane, output, memory->DeviceFd());
	close(fd);
	return status;
#else
	return B_UNSUPPORTED;
#endif
}


status_t
DrmHWInterface::_ClaimOverlayPlane(uint32 width, uint32 height, uint32 fourcc,
	uint32 bpp, color_space space, int dmabufFd, uint32 stride,
	uint32 sharedHandle, OverlayPlane** _plane, uint32 output, int sharedFd)
{
	if (fFd < 0 || !fInitialized)
		return B_NO_INIT;

	// See DrmOverlayPlane::Configure(): a rotated or reflected output
	// would scan the plane out untransformed.
	if (output == 0 && (fPanelOrientation != PANEL_ORIENTATION_NORMAL
			|| fPanelReflection != 0))
		return B_UNSUPPORTED;

	pthread_mutex_lock(&fOverlayMutex);
	for (size_t i = 0; i < fOverlayPlanes.size(); i++) {
		OverlayPlaneInfo& info = fOverlayPlanes[i];
		if (info.claimed != NULL || info.output != output)
			continue;
		if (std::find(info.formats.begin(), info.formats.end(), fourcc)
				== info.formats.end())
			continue;
		// Chains and dumb buffers are linear.
		if (!_PlaneAccepts(info, fourcc, DRM_FORMAT_MOD_LINEAR))
			continue;
		if ((info.minWidth != 0 && width < info.minWidth)
				|| (info.maxWidth != 0 && width > info.maxWidth)
				|| (info.minHeight != 0 && height < info.minHeight)
				|| (info.maxHeight != 0 && height > info.maxHeight))
			continue;

		DrmOverlayPlane* plane = new(std::nothrow) DrmOverlayPlane(this,
			info.planeId, info.props, info.output, info.fd, info.crtcId);
		if (plane == NULL) {
			pthread_mutex_unlock(&fOverlayMutex);
			return B_NO_MEMORY;
		}
		plane->SetSharedDevice(sharedFd < 0 || sharedFd == info.fd);
		status_t status = dmabufFd >= 0
			? plane->Adopt(dmabufFd, width, height, fourcc, stride, space,
				plane->SharedDevice() ? sharedHandle : 0)
			: plane->Allocate(width, height, fourcc, bpp, space);
		if (status != B_OK) {
			delete plane;
			continue;
		}

		info.claimed = plane;
		*_plane = plane;
		pthread_mutex_unlock(&fOverlayMutex);
		printf("DRM: overlay plane %u claimed (%ux%u)\n",
			plane->PlaneId(), width, height);
		return B_OK;
	}
	pthread_mutex_unlock(&fOverlayMutex);

	// All planes claimed, or none takes this format/size. Either way the
	// caller must fall back to CPU compositing.
	return B_BUSY;
}


void
DrmHWInterface::ReleaseOverlayPlane(OverlayPlane* plane)
{
	if (plane == NULL)
		return;

	DrmOverlayPlane* drmPlane = static_cast<DrmOverlayPlane*>(plane);
	uint32_t planeId = drmPlane->PlaneId();

	pthread_mutex_lock(&fOverlayMutex);
	OverlayPlaneInfo* info = _FindOverlayPlaneInfo(drmPlane->Output(),
		planeId);
	if (info != NULL && info->claimed == drmPlane)
		info->claimed = NULL;
	pthread_mutex_unlock(&fOverlayMutex);

	drmPlane->Disable();
	drmPlane->Free();
	delete drmPlane;
	printf("DRM: overlay plane %u released\n", planeId);
}


bool
DrmHWInterface::CheckOverlayRestrictions(int32 width, int32 height,
	color_space space)
{
	if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
		return false;

	uint32 fourcc = 0;
	uint32 bpp = 0;
	if (!_FourccForColorSpace(space, &fourcc, &bpp))
		return false;

	pthread_mutex_lock(&fOverlayMutex);
	for (size_t i = 0; i < fOverlayPlanes.size(); i++) {
		const OverlayPlaneInfo& info = fOverlayPlanes[i];
		if (std::find(info.formats.begin(), info.formats.end(), fourcc)
				== info.formats.end())
			continue;
		if ((info.minWidth != 0 && (uint32)width < info.minWidth)
				|| (info.maxWidth != 0 && (uint32)width > info.maxWidth)
				|| (info.minHeight != 0 && (uint32)height < info.minHeight)
				|| (info.maxHeight != 0 && (uint32)height > info.maxHeight))
			continue;

		pthread_mutex_unlock(&fOverlayMutex);
		return true;
	}
	pthread_mutex_unlock(&fOverlayMutex);
	return false;
}


status_t
DrmHWInterface::GetOverlayRestrictions(const Overlay* overlay,
	overlay_restrictions* restrictions)
{
	if (overlay == NULL || overlay->OverlayBuffer() == NULL
			|| restrictions == NULL)
		return B_BAD_VALUE;

	pthread_mutex_lock(&fOverlayMutex);
	DrmOverlayBuffer* entry = _FindOverlayBuffer(overlay->OverlayBuffer());
	pthread_mutex_unlock(&fOverlayMutex);
	if (entry == NULL)
		return B_ERROR;

	memset(restrictions, 0, sizeof(*restrictions));
	// Source is exactly the claimed buffer; the driver scales to the
	// window. Hardware scale bounds are rarely advertised, so allow
	// what every KMS overlay does in practice.
	restrictions->source.min_width = restrictions->source.max_width
		= entry->plane->Width();
	restrictions->source.min_height = restrictions->source.max_height
		= entry->plane->Height();
	restrictions->destination.min_width = 1;
	restrictions->destination.max_width = 65535;
	restrictions->destination.min_height = 1;
	restrictions->destination.max_height = 65535;
	restrictions->min_width_scale = 1.0f;
	restrictions->max_width_scale = 32.0f;
	restrictions->min_height_scale = 1.0f;
	restrictions->max_height_scale = 32.0f;
	return B_OK;
}


const overlay_buffer*
DrmHWInterface::AllocateOverlayBuffer(int32 width, int32 height,
	color_space space)
{
	if (width <= 0 || height <= 0)
		return NULL;

	OverlayPlane* plane = NULL;
	status_t status = AcquireOverlayPlane(space, (uint32)width,
		(uint32)height, &plane);
	if (status != B_OK)
		return NULL;

	DrmOverlayBuffer* entry = new(std::nothrow) DrmOverlayBuffer;
	if (entry == NULL) {
		ReleaseOverlayPlane(plane);
		return NULL;
	}

	entry->plane = static_cast<DrmOverlayPlane*>(plane);
	entry->buffer.space = space;
	entry->buffer.width = (uint16)width;
	entry->buffer.height = (uint16)height;
	entry->buffer.bytes_per_row = plane->BytesPerRow();
	entry->buffer.buffer = plane->Bits();
	entry->buffer.buffer_dma = NULL;

	pthread_mutex_lock(&fOverlayMutex);
	fOverlayBuffers.push_back(entry);
	pthread_mutex_unlock(&fOverlayMutex);

	return &entry->buffer;
}


void
DrmHWInterface::FreeOverlayBuffer(const overlay_buffer* buffer)
{
	if (buffer == NULL)
		return;

	DrmOverlayBuffer* entry = NULL;
	pthread_mutex_lock(&fOverlayMutex);
	for (size_t i = 0; i < fOverlayBuffers.size(); i++) {
		if (&fOverlayBuffers[i]->buffer == buffer) {
			entry = fOverlayBuffers[i];
			fOverlayBuffers.erase(fOverlayBuffers.begin() + i);
			break;
		}
	}
	pthread_mutex_unlock(&fOverlayMutex);

	if (entry == NULL)
		return;

	ReleaseOverlayPlane(entry->plane);
	delete entry;
}


void
DrmHWInterface::ConfigureOverlay(Overlay* overlay)
{
	if (overlay == NULL || overlay->OverlayBuffer() == NULL)
		return;

	pthread_mutex_lock(&fOverlayMutex);
	DrmOverlayBuffer* entry = _FindOverlayBuffer(overlay->OverlayBuffer());
	pthread_mutex_unlock(&fOverlayMutex);
	if (entry == NULL)
		return;

	const overlay_view* view = overlay->OverlayView();
	const overlay_window* window = overlay->OverlayWindow();
	BRect source(view->h_start, view->v_start,
		view->h_start + view->width - 1, view->v_start + view->height - 1);
	BRect destination(window->h_start, window->v_start,
		window->h_start + window->width - 1,
		window->v_start + window->height - 1);

	status_t status = entry->plane->Configure(source, destination);
	if (status != B_OK)
		fprintf(stderr, "DRM: overlay configure failed: %#x\n", status);
}


void
DrmHWInterface::HideOverlay(Overlay* overlay)
{
	if (overlay == NULL || overlay->OverlayBuffer() == NULL)
		return;

	pthread_mutex_lock(&fOverlayMutex);
	DrmOverlayBuffer* entry = _FindOverlayBuffer(overlay->OverlayBuffer());
	pthread_mutex_unlock(&fOverlayMutex);
	if (entry == NULL)
		return;

	entry->plane->Disable();
}


bool
DrmHWInterface::_PlaneAccepts(const OverlayPlaneInfo& info, uint32 format,
	uint64 modifier)
{
	if (info.modifiers.empty())
		return modifier == DRM_FORMAT_MOD_LINEAR;

	for (size_t i = 0; i < info.modifiers.size(); i++) {
		if (info.modifiers[i].format == format
				&& info.modifiers[i].modifier == modifier)
			return true;
	}
	return false;
}


OverlayPlaneInfo*
DrmHWInterface::_FindOverlayPlaneInfo(uint32_t output, uint32_t planeId)
{
	for (size_t i = 0; i < fOverlayPlanes.size(); i++) {
		if (fOverlayPlanes[i].planeId == planeId
				&& fOverlayPlanes[i].output == output)
			return &fOverlayPlanes[i];
	}
	return NULL;
}


DrmHWInterface::DrmOverlayBuffer*
DrmHWInterface::_FindOverlayBuffer(const overlay_buffer* buffer)
{
	for (size_t i = 0; i < fOverlayBuffers.size(); i++) {
		if (&fOverlayBuffers[i]->buffer == buffer)
			return fOverlayBuffers[i];
	}
	return NULL;
}


// The hardware lost or dropped the plane state; stage the pointer again.
void
DrmHWInterface::_RearmCursor()
{
	if (fHardwareCursorEnabled && fCursor.IsSet())
		SetCursor(fCursor);
}


void
DrmHWInterface::_RearmOverlayPlanes()
{
	pthread_mutex_lock(&fOverlayMutex);
	std::vector<DrmOverlayPlane*> claimed;
	for (size_t i = 0; i < fOverlayPlanes.size(); i++) {
		if (fOverlayPlanes[i].claimed != NULL)
			claimed.push_back(fOverlayPlanes[i].claimed);
	}
	pthread_mutex_unlock(&fOverlayMutex);

	for (size_t i = 0; i < claimed.size(); i++)
		claimed[i]->Reapply();
}


void
DrmHWInterface::_ReleaseOverlayPlanes(uint32 output, bool dropInfo)
{
	pthread_mutex_lock(&fOverlayMutex);
	std::vector<DrmOverlayPlane*> claimed;
	std::vector<OverlayPlaneInfo> kept;
	for (size_t i = 0; i < fOverlayPlanes.size(); i++) {
		OverlayPlaneInfo& info = fOverlayPlanes[i];
		const bool mine = output == kAllOutputs || info.output == output;
		if (mine && info.claimed != NULL) {
			claimed.push_back(info.claimed);
			info.claimed = NULL;
		}
		if (!mine || !dropInfo)
			kept.push_back(info);
	}
	fOverlayPlanes = kept;

	// The legacy buffers sit on planes of their own.
	std::vector<DrmOverlayBuffer*> buffers;
	for (size_t i = 0; i < fOverlayBuffers.size(); i++) {
		DrmOverlayPlane* plane = fOverlayBuffers[i]->plane;
		if (std::find(claimed.begin(), claimed.end(), plane)
				!= claimed.end()) {
			buffers.push_back(fOverlayBuffers[i]);
			fOverlayBuffers.erase(fOverlayBuffers.begin() + i);
			i--;
		}
	}
	pthread_mutex_unlock(&fOverlayMutex);

	for (size_t i = 0; i < buffers.size(); i++)
		delete buffers[i];

	// The users hear of it, and go back to the painter.
	for (size_t i = 0; i < claimed.size(); i++) {
		claimed[i]->Abandon();
		claimed[i]->Disable();
		claimed[i]->Free();
		delete claimed[i];
	}
}
