/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "FBDevBuffer.h"

#include <sys/mman.h>


FBDevBuffer::FBDevBuffer(int fd, struct fb_var_screeninfo vInfo,
	struct fb_fix_screeninfo finfo)
	:
	fBuffer((uint8_t*)MAP_FAILED),
	fSpace(B_RGB32)
{
	fVInfo = vInfo;
	fInfo = finfo;

	if (!fbdev_color_space(fVInfo.bits_per_pixel, &fSpace))
		fSpace = B_RGB32;

	uint32 bytes = fbdev_bytes_per_row(fVInfo.bits_per_pixel, fVInfo.xres,
		fInfo.line_length);
	if (bytes == 0 || fVInfo.yres_virtual == 0)
		return;

	fBuffer = (uint8_t*)mmap(0, bytes * fVInfo.yres_virtual,
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0);
	if (fBuffer == MAP_FAILED)
		fBuffer = NULL;
}


FBDevBuffer::~FBDevBuffer()
{
	CALLED();
	if (fBuffer != NULL && fBuffer != (uint8_t*)MAP_FAILED)
		munmap(fBuffer, BytesPerRow() * fVInfo.yres_virtual);
}


status_t
FBDevBuffer::InitCheck() const
{
	CALLED();
	if (fBuffer != NULL)
		return B_OK;

	return B_ERROR;
}


color_space
FBDevBuffer::ColorSpace() const
{
	CALLED();
	return fSpace;
}


void*
FBDevBuffer::Bits() const
{
	CALLED();
	return (void*)fBuffer;
}


uint32
FBDevBuffer::BytesPerRow() const
{
	CALLED();
	return fbdev_bytes_per_row(fVInfo.bits_per_pixel, fVInfo.xres,
		fInfo.line_length);
}


uint32
FBDevBuffer::Width() const
{
	CALLED();
	return fVInfo.xres;
}


uint32
FBDevBuffer::Height() const
{
	CALLED();
	return fVInfo.yres;
}
