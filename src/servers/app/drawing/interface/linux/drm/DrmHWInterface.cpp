/*
 * Copyright 2021-2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmHWInterface.h"

#include "DrmBuffer.h"

#include <poll.h>
#include <time.h>

#include <thread>

#include "modeset.h"


static const char* sDriPath = "/dev/dri/card0";
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
	HWInterface(false, false),
	fFrontBuffer(NULL),
	fBackBuffer(NULL),
	fEventStream(NULL),
	fSeat(NULL),
	fDeviceId(-1),
	fSessionActive(false),
	fInitialized(false),
	fRunning(false),
	fSessionGeneration(0)
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
	fEventThread = std::thread(&DrmHWInterface::EventThreadMain, this);
}


void
DrmHWInterface::_OnSessionEnable()
{
	printf("Session enabled\n");

	if (fInitialized) {
		{
			std::lock_guard<std::mutex> lock(fSessionMutex);
			fSessionActive = true;
			fSessionGeneration++;
		}

		fSessionCondition.notify_all();
	
		_RestoreDisplay();

		if (fEventStream)		
			fEventStream->Resume();
		return;
	}

	fDeviceId = libseat_open_device(fSeat, sDriPath, &fFd);

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

	fFrontBuffer = new DrmBuffer(fFd, get_dev());
	#ifdef DRM_BACK_BUFFER
	fBackBuffer = new DrmBuffer(fFd, get_dev());
	#endif

	fEventStream = new LibInputEventStream(get_dev()->width, get_dev()->height, fSeat);
	fEventStream->SetSeatMutex(&fSeatMutex);

	fDisplayMode.virtual_width = get_dev()->width;
	fDisplayMode.virtual_height = get_dev()->height;
	fDisplayMode.space = B_RGB32;

	{
		std::lock_guard<std::mutex> lock(fSessionMutex);
		fSessionActive = true;
		fInitialized = true;
	}
	fSessionCondition.notify_all();
}


void
DrmHWInterface::_OnSessionDisable()
{
	printf("Session disabled\n");

	if (fEventStream)
		fEventStream->Suspend();

	{
		std::lock_guard<std::mutex> lock(fSessionMutex);
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


void DrmHWInterface::EventThreadMain()
{
	int seatErrorCount = 0;

	while (fRunning) {
		int seat_fd;
		{
			std::lock_guard<std::mutex> seatLock(fSeatMutex);
			seat_fd = fSeat ? libseat_get_fd(fSeat) : -1;
		}

		if (seat_fd < 0) {
			seatErrorCount++;
			snooze(100);
			continue;
		}
		seatErrorCount = 0;

		bool active;
		uint32_t gen;
		{
			std::lock_guard<std::mutex> lock(fSessionMutex);
			active = fSessionActive;
			gen = fSessionGeneration;
		}

		if (active) {
			struct pollfd pfd;
			pfd.fd = seat_fd;
			pfd.events = POLLIN;
			pfd.revents = 0;

			int ret = poll(&pfd, 1, 100);
			if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
			
				std::lock_guard<std::mutex> seatLock(fSeatMutex);
				int dret = libseat_dispatch(fSeat, 0);
			
			}
		} else {
			struct pollfd pfd;
			pfd.fd = seat_fd;
			pfd.events = POLLIN;
			pfd.revents = 0;

			int ret = poll(&pfd, 1, 100);
			if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
			
				std::lock_guard<std::mutex> seatLock(fSeatMutex);
				int dret = libseat_dispatch(fSeat, 0);
				if (dret < 0)
					printf("libseat_dispatch: error\n");
			}
		}
	}


}


DrmHWInterface::~DrmHWInterface()
{
	CALLED();


	fRunning = false;
	fSessionCondition.notify_all();

	if (fEventThread.joinable())
		fEventThread.join();	

	if (fSeat && fDeviceId >= 0)
		libseat_close_device(fSeat, fDeviceId);

	if (fSeat)
		libseat_close_seat(fSeat);

	modeset_cleanup(fFd);

	delete fFrontBuffer;
	delete fEventStream;
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
	return fEventStream;
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
#ifdef DRM_BACK_BUFFER
	CALLED();

	if (fFd < 0)
		return B_ERROR;

	// TODO we should check if the session is active to avoid having
	// someone stuck on this.

	struct drm_wait_vblank wait;
	memset(&wait, 0, sizeof(wait));
	wait.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT;
	wait.request.sequence = 1;

	if (ioctl(fFd, DRM_IOCTL_WAIT_VBLANK, &wait) < 0)
		return B_ERROR;

	struct pollfd pollFd;
	pollFd.fd = fFd;
	pollFd.events = POLLIN | POLLPRI;
	pollFd.revents = 0;

	bool infinite = (timeout < 0);
	bigtime_t start_us = 0;
	if (!infinite)
		start_us = system_time();

	for (;;) {
		int timeout_ms;
		if (infinite) {
			timeout_ms = -1;
		} else {
			int64_t elapsed_us = (int64_t)(system_time() - start_us);
			int64_t rem_us = (int64_t)timeout - elapsed_us;
			if (rem_us <= 0) {
				return B_TIMED_OUT;
			}
			timeout_ms = static_cast<int>((rem_us + 999) / 1000);
		}

		int ret = poll(&pollFd, 1, timeout_ms);
		if (ret > 0) {
			drmEventContext evctx;
			memset(&evctx, 0, sizeof(evctx));
			evctx.version = 2;
			evctx.vblank_handler
				= [](int, unsigned int, unsigned int, unsigned int, void*) {};

			if (drmHandleEvent(fFd, &evctx) < 0)
				return B_ERROR;

			return B_OK;
		} else if (ret == 0) {
			if (infinite)
				continue;
			int64_t elapsed_us = (int64_t)(system_time() - start_us);
			if (elapsed_us >= (int64_t)timeout)
				return B_TIMED_OUT;

			continue;
		} else {
			if (errno == EINTR)
				continue;
			return B_ERROR;
		}
	}

	return B_ERROR;
#endif
	UNIMPLEMENTED();
	return B_UNSUPPORTED;
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
	#ifdef DRM_BACK_BUFFER
	return fBackBuffer;
	#else
	return NULL;
	#endif
}


bool
DrmHWInterface::IsDoubleBuffered() const
{
	return fBackBuffer != NULL;
}


status_t
DrmHWInterface::CopyBackToFront(const BRect& frame)
{
	#ifdef DRM_BACK_BUFFER
    //memcpy(fFrontBuffer->Bits(), fBackBuffer->Bits(),
    //       fFrontBuffer->BitsLength());

    drmModePageFlip(fFd, crtc, fBackBuffer->GetFbId(),
                    DRM_MODE_PAGE_FLIP_EVENT, this);
    std::swap(fFrontBuffer, fBackBuffer);

    return B_OK;
    #else
	return B_UNSUPPORTED;
	#endif
}
