/*
 * Copyright 2021-2026, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "DrmBuffer.h"

#include <sys/mman.h>



DrmBuffer::DrmBuffer(int fd, modeset_dev* dev, bool isBack)
	:
	fDev(dev),
	fIsBack(isBack),
	fErr(B_ERROR),
	fColorSpace(B_RGB32),
	fBits(NULL),
	fStride(0),
	fFbId(0),
	fWidth(0),
	fHeight(0)
{
	fErr = B_OK;
}


DrmBuffer::DrmBuffer(uint8_t* bits, uint32_t stride, uint32_t fbId,
	uint32_t width, uint32_t height)
	:
	fDev(NULL),
	fIsBack(false),
	fErr(B_OK),
	fColorSpace(B_RGB32),
	fBits(bits),
	fStride(stride),
	fFbId(fbId),
	fWidth(width),
	fHeight(height)
{
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
	return fBits != NULL ? (void*)fBits
		: (fIsBack ? (void*)fDev->back_map : (void*)fDev->map);
}


uint32
DrmBuffer::BytesPerRow() const
{
	CALLED();
	return fStride != 0 ? fStride
		: (fIsBack ? fDev->back_stride : fDev->stride);
}


uint32
DrmBuffer::GetFbId() const
{
	return fFbId != 0 ? fFbId
		: (fIsBack ? fDev->back_fb : fDev->fb);
}


uint32
DrmBuffer::Width() const
{
	CALLED();
	return fWidth != 0 ? fWidth : fDev->width;
}


uint32
DrmBuffer::Height() const
{
	CALLED();
	return fHeight != 0 ? fHeight : fDev->height;
}
