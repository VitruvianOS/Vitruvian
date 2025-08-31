/*
 * Copyright 2021, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmBuffer.h"

#include <sys/mman.h>



DrmBuffer::DrmBuffer(int fd, modeset_dev* dev)
	:
	fDev(dev),
	fErr(B_ERROR),
	fColorSpace(B_RGB32)
{
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

/*
drmModeConnector *connector = drmModeGetConnector(fd, connector_id);
for (int i = 0; i < connector->count_props; i++) {
    drmModePropertyPtr prop = drmModeGetProperty(fd, connector->props[i]);
    if (strcmp(prop->name, "color_space") == 0) {
        // Found color_space property, query it
    }
}
	switch (drm_format) {
        // RGB formats
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_ARGB8888:
            return B_RGB32;  // 32-bit RGB, no alpha channel for XRGB8888, with alpha for ARGB8888
        case DRM_FORMAT_RGB565:
            return B_RGB16;  // 16-bit RGB format
        // YUV formats
        case DRM_FORMAT_NV12:
            return B_YUV420; // NV12 is a YUV420 format
        case DRM_FORMAT_YUV420:
            return B_YUV420; // YUV420
        case DRM_FORMAT_YUV422:
            return B_YUV422; // YUV422 format
        // Handle other formats as needed
        default:
            // Return a default or an error value if the format is unsupported
            return B_NO_COLORSPACE;
    }
*/
}


void*
DrmBuffer::Bits() const
{
	CALLED();
	return (void*)fDev->map;
}


uint32
DrmBuffer::BytesPerRow() const
{
	CALLED();
	return fDev->stride;
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
