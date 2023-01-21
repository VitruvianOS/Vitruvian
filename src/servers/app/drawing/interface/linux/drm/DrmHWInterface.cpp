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


static const char* sDriPath = "/dev/dri/card0";


DrmDevice*
DrmHWInterface::_FindDev(int fd) const
{
	drmModeRes* res = drmModeGetResources(fd);
	if (res == NULL)
		return NULL;

	DrmDevice* devHead = NULL;
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector* conn = drmModeGetConnector(fd, res->connectors[i]);

		if (conn != NULL && conn->connection == DRM_MODE_CONNECTED
				&& conn->count_modes > 0) {
			DrmDevice* dev = new DrmDevice();
			memset(dev, 0, sizeof(DrmDevice));

			dev->conn_id = conn->connector_id;
			dev->enc_id = conn->encoder_id;
			dev->next = NULL;

			memcpy(&dev->mode, &conn->modes[0], sizeof(drmModeModeInfo));
			dev->width = conn->modes[0].hdisplay;
			dev->height = conn->modes[0].vdisplay;

			drmModeEncoder* enc = drmModeGetEncoder(fd, dev->enc_id);
			if (enc  == NULL)
				return NULL;

			dev->crtc_id = enc->crtc_id;
			drmModeFreeEncoder(enc);

			dev->crtc = NULL;

			dev->next = devHead;
			devHead = dev;
		}
		drmModeFreeConnector(conn);
	}
	drmModeFreeResources(res);

	return devHead;
}


DrmHWInterface::DrmHWInterface()
	:
	HWInterface(false, false),
	fFd(-1),
	fDevList(NULL),
	fFrontBuffer(NULL),
	fBackBuffer(NULL),
	fEventStream(NULL)
{
	if ((fFd = open(sDriPath, O_RDWR)) < 0) {
		fprintf(stderr, "Error Opening \"%s\"\n", sDriPath);
		return;
	}

	int flags = fcntl(fFd, F_GETFD);
	if (flags < 0 || fcntl(fFd, F_SETFD, flags | FD_CLOEXEC) < 0)
		return;

	uint64_t hasDumbBuffer;
	if (drmGetCap(fFd, DRM_CAP_DUMB_BUFFER, &hasDumbBuffer) < 0
			|| hasDumbBuffer == 0) {
		return;
	}

	fDevList = _FindDev(fFd);

	if (fDevList == NULL) {
		fprintf(stderr, "Error: Can't find any DRM device\n");
		return;
	}

	printf("Connectors list:\n\n");
	for (DrmDevice* dev = fDevList; dev != NULL; dev = dev->next) {
		printf("connector id:%d\n", dev->conn_id);
		printf("\tencoder id:%d crtc id:%d fb id:%d\n",
			dev->enc_id, dev->crtc_id, dev->fb_id);
		printf("\twidth:%d height:%d\n", dev->width, dev->height);
	}

	fFrontBuffer = new DrmBuffer(fFd, fDevList);
	fEventStream = new LibInputEventStream(fDevList->width, fDevList->height);
}


DrmHWInterface::~DrmHWInterface()
{
	CALLED();

	struct drm_mode_destroy_dumb destroyReq;

	for (DrmDevice* device = fDevList; device != NULL;) {
		if (device->crtc) {
			drmModeSetCrtc(fFd, device->crtc->crtc_id,
				device->crtc->buffer_id,
				device->crtc->x,
				device->crtc->y,
				&device->conn_id, 1,
				&device->crtc->mode);
		}

		drmModeFreeCrtc(device->crtc);
		munmap(device->buffer, device->size);

		drmModeRmFB(fFd, device->fb_id);

		memset(&destroyReq, 0, sizeof(destroyReq));
		destroyReq.handle = device->handle;
		drmIoctl(fFd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroyReq);

		DrmDevice* tmp = device;
		device = device->next;
		delete tmp;
	}

	close(fFd);

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
