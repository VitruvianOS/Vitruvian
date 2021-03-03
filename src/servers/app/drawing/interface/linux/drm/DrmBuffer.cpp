/*
 * Copyright 2021, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmBuffer.h"

#include <sys/mman.h>


DrmBuffer::DrmBuffer(int fd, DrmDevice* dev)
	:
	fDev(dev),
	fErr(B_ERROR),
	fColorSpace(B_RGB32)
{
	struct drm_mode_create_dumb createReq;
	memset(&createReq, 0, sizeof(struct drm_mode_create_dumb));

	createReq.width = fDev->width;
	createReq.height = fDev->height;
	createReq.bpp = 32;

	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &createReq) < 0)
		return;

	fDev->pitch = createReq.pitch;
	fDev->size = createReq.size;
	fDev->handle = createReq.handle;

	if (drmModeAddFB(fd, fDev->width, fDev->height,
			24, 32, fDev->pitch, fDev->handle, &fDev->fb_id))
		return;

	struct drm_mode_map_dumb mapReq;
	memset(&mapReq, 0, sizeof(struct drm_mode_map_dumb));
	mapReq.handle = fDev->handle;

	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mapReq))
		return;

	if ((fDev->buffer = (uint32_t*)mmap(0, fDev->size, PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, mapReq.offset)) == MAP_FAILED) {
		fErr = B_NO_MEMORY;
		return;
	}

	fDev->crtc = drmModeGetCrtc(fd, fDev->crtc_id);
	if (drmModeSetCrtc(fd, fDev->crtc_id, fDev->fb_id, 0, 0,
			&fDev->conn_id, 1, &fDev->mode)) {
		return;
	}

	fErr = B_OK;
}


DrmBuffer::~DrmBuffer()
{
	CALLED();
}


status_t
DrmBuffer::InitCheck() const
{
	CALLED();
	return fErr;
}


color_space
DrmBuffer::ColorSpace() const
{
	CALLED();
	return fColorSpace;
}


void*
DrmBuffer::Bits() const
{
	CALLED();
	return (void*)fDev->buffer;
}


uint32
DrmBuffer::BytesPerRow() const
{
	CALLED();
	return fDev->pitch;
}


uint32
DrmBuffer::Width() const
{
	CALLED();
	return fDev->width;
}


uint32
DrmBuffer::Height() const
{
	CALLED();
	return fDev->height;
}
