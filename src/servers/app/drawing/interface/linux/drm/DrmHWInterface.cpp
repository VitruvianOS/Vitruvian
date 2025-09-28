/*
 * Copyright 2021, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmHWInterface.h"

#include "DrmBuffer.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <libseat.h>

#include "modeset.h"

static const char* sDriPath = "/dev/dri/card0";
int DrmHWInterface::fFd = -1;


DrmHWInterface::DrmHWInterface()
	:
	HWInterface(false, false),
	fFrontBuffer(NULL),
	fBackBuffer(NULL),
	fEventStream(NULL)
{
	int ret;
	struct modeset_dev *iter;

	fprintf(stderr, "using card '%s'\n", sDriPath);

	/* open the DRM device */
	ret = modeset_open(&fFd, sDriPath);
	if (ret)
		return;

	/* prepare all connectors and CRTCs */
	ret = modeset_prepare(fFd);
	if (ret)
		return;

	//TTy::InitTTy(4, SwitchVt);

	drmSetMaster(fFd);

	/* perform actual modesetting on each found connector+CRTC */
	for (iter = get_dev(); iter; iter = iter->next) {
		iter->saved_crtc = drmModeGetCrtc(fFd, iter->crtc);
		ret = drmModeSetCrtc(fFd, iter->crtc, iter->fb, 0, 0,
				     &iter->conn, 1, &iter->mode);
		if (ret)
			fprintf(stderr, "cannot set CRTC for connector %u (%d): %m\n",
				iter->conn, errno);
	}

	fFrontBuffer = new DrmBuffer(fFd, get_dev());
	fEventStream = new LibInputEventStream(get_dev()->width, get_dev()->height);
	fDisplayMode.virtual_width = get_dev()->width;
	fDisplayMode.virtual_height = get_dev()->height;
	fDisplayMode.space = B_RGB32;

	//ioctl(TTy::gTTy, KDSETMODE, KD_GRAPHICS);
}


DrmHWInterface::~DrmHWInterface()
{
	CALLED();

	drmDropMaster(fFd);
	modeset_cleanup(fFd);

	//TTy::DeinitTTy();

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
	CALLED();
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
	return NULL;
}


bool
DrmHWInterface::IsDoubleBuffered() const
{
	// TODO
	return false;
}


status_t
DrmHWInterface::CopyBackToFront(const BRect& frame)
{
	CALLED();
	return B_ERROR;
}


void
DrmHWInterface::SwitchVt(int sig)
{
	switch (sig)
	{
		case SIGUSR1:
			ioctl(TTy::gTTy, VT_RELDISP, 1);
			drmDropMaster(fFd);
			break;
		case SIGUSR2:
			ioctl(TTy::gTTy, VT_RELDISP, VT_ACTIVATE);
			ioctl(TTy::gTTy, VT_RELDISP, VT_ACKACQ);
			drmSetMaster(fFd);
			break;
	}
}
