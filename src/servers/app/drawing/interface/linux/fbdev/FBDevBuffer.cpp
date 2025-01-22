/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "FBDevBuffer.h"

#include <sys/mman.h>


FBDevBuffer::FBDevBuffer(int fd, struct fb_var_screeninfo vInfo,
	struct fb_fix_screeninfo finfo)
{
	fVInfo = vInfo;
	fInfo = finfo;
	fBuffer = mmap(0, fInfo.line_length * fVInfo.yres_virtual,
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0);
}


FBDevBuffer::~FBDevBuffer()
{
	CALLED();
}


status_t
FBDevBuffer::InitCheck() const
{
	CALLED();
	if (fBuffer != MAP_FAILED)
		return B_OK;

	return B_ERROR;
}


color_space
FBDevBuffer::ColorSpace() const
{
	CALLED();
	return B_RGB32;
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
	return fInfo.line_length;
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
