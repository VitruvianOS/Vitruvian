/*
 * Copyright 2021-2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmHWInterface.h"

#include "DrmBuffer.h"

#include <algorithm>
#include <poll.h>
#include <time.h>

#include <Autolock.h>

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
	fSeat(NULL),
	fDeviceId(-1),
	fSessionActive(false),
	fInitialized(false),
	fRunning(false),
	fSessionGeneration(0),
	fEventThread(-1),
	fSessionLock("drm session lock"),
	fSessionSem(create_sem(0, "drm session sem")),
	fUdev(NULL),
	fUdevMonitor(NULL),
	fUdevFd(-1)
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
		{
			BAutolock _(fSessionLock);
			fSessionActive = true;
			fSessionGeneration++;
		}

		release_sem(fSessionSem);

		_RestoreDisplay();
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

	struct modeset_dev *iter;
	for (iter = get_dev(); iter; iter = iter->next) {
		iter->saved_crtc = drmModeGetCrtc(fFd, iter->crtc);
		ret = drmModeSetCrtc(fFd, iter->crtc, iter->fb, 0, 0,
					 &iter->conn, 1, &iter->mode);
		if (ret) {
			fprintf(stderr, "cannot set CRTC for connector %u (%d): %m\n",
				iter->conn, errno);
		}
	}

	modeset_create_back_fb(fFd, get_dev());
	fFrontBuffer = new DrmBuffer(fFd, get_dev(), false);
	fBackBuffer  = new DrmBuffer(fFd, get_dev(), true);

	// Set up udev hotplug monitor for DRM connectors
	fUdev = udev_new();
	if (fUdev) {
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

	{
		BAutolock _(fSessionLock);
		fSessionActive = true;
		fInitialized = true;
	}
	release_sem(fSessionSem);
}


void
DrmHWInterface::_OnSessionDisable()
{
	printf("Session disabled\n");

	{
		BAutolock _(fSessionLock);
		fSessionActive = false;
		fSessionGeneration++;
	}

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

		bool active;
		{
			BAutolock _(fSessionLock);
			active = fSessionActive;
		}

		struct pollfd pfds[2];
		pfds[0].fd = seat_fd;
		pfds[0].events = POLLIN;
		pfds[0].revents = 0;
		pfds[1].fd = fUdevFd;
		pfds[1].events = POLLIN;
		pfds[1].revents = 0;
		int nfds = (fUdevFd >= 0) ? 2 : 1;

		int ret = poll(pfds, nfds, 100);
		if (ret > 0) {
			if (pfds[0].revents & (POLLIN | POLLHUP)) {
				int dret = libseat_dispatch(fSeat, 0);
				if (dret < 0 && !active)
					printf("libseat_dispatch: error\n");
			}
			if (nfds > 1 && (pfds[1].revents & POLLIN)) {
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

	if (fUdevMonitor)
		udev_monitor_unref(fUdevMonitor);
	if (fUdev)
		udev_unref(fUdev);

	if (fSeat && fDeviceId > 0)
		libseat_close_device(fSeat, fDeviceId);

	if (fSeat)
		libseat_close_seat(fSeat);

	modeset_cleanup(fFd);

	delete fFrontBuffer;
	delete fBackBuffer;
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


EventStream*
DrmHWInterface::CreateEventStream()
{
	// Input is handled by input_server add-ons via InputServerStream.
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
	return B_UNSUPPORTED;
}


status_t
DrmHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low, uint32* _high)
{
	CALLED();
	return B_UNSUPPORTED;
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
	return B_UNSUPPORTED;
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

	struct drm_wait_vblank wait;
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
	CALLED();
	return B_UNSUPPORTED;
}


uint32
DrmHWInterface::DPMSMode()
{
	CALLED();
	return B_UNSUPPORTED;
}


uint32
DrmHWInterface::DPMSCapabilities()
{
	CALLED();
	return 0;
}


status_t
DrmHWInterface::SetBrightness(float brightness)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
DrmHWInterface::GetBrightness(float* brightness)
{
	CALLED();
	return B_UNSUPPORTED;
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
	return fBackBuffer;
}


bool
DrmHWInterface::IsDoubleBuffered() const
{
	return fBackBuffer != NULL;
}


status_t
DrmHWInterface::CopyBackToFront(const BRect& frame)
{
	if (fBackBuffer == NULL)
		return B_ERROR;

	// Software double buffering: blit dirty rect from back (render target)
	// to front (displayed framebuffer). No page flip, no pointer swap.
	// FrontBuffer() must always point to the displayed framebuffer so that
	// app_server can draw the cursor directly there.
	uint32 bpr = fBackBuffer->BytesPerRow();
	int32  x   = (int32)frame.left;
	int32  y   = (int32)frame.top;
	int32  w   = (int32)(frame.right  - frame.left + 1);
	int32  h   = (int32)(frame.bottom - frame.top  + 1);

	// Clamp to buffer bounds
	int32 bufW = (int32)fFrontBuffer->Width();
	int32 bufH = (int32)fFrontBuffer->Height();
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > bufW) w = bufW - x;
	if (y + h > bufH) h = bufH - y;

	if (w <= 0 || h <= 0)
		return B_OK;

	void* backBits  = fBackBuffer->Bits();
	void* frontBits = fFrontBuffer->Bits();
	if (backBits == NULL || frontBits == NULL)
		return B_ERROR;

	uint8* src = (uint8*)backBits  + y * bpr + x * 4;
	uint8* dst = (uint8*)frontBits + y * bpr + x * 4;
	for (int32 row = 0; row < h; row++) {
		memcpy(dst, src, w * 4);
		src += bpr;
		dst += bpr;
	}

	// Composite software cursor on top of front buffer.
	// _DrawCursor reads from BackBuffer (DrawingBuffer), blends the cursor
	// bitmap, and writes the result directly into FrontBuffer via _CopyToFront.
	_DrawCursor(IntRect(frame));

	return B_OK;
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

	int fd = drmModeCreateLease(fFd, objects, total, 0, leaseFd);
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
