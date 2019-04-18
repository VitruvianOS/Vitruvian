/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "FBDevBuffer.h"

#include <sys/mman.h>


inline uint32_t pixel_color(uint8_t r, uint8_t g, uint8_t b, struct fb_var_screeninfo *vinfo)
{
	return (r<<vinfo->red.offset) | (g<<vinfo->green.offset) | (b<<vinfo->blue.offset);
}


FBDevBuffer::FBDevBuffer(int fd, struct fb_var_screeninfo vInfo,
	struct fb_fix_screeninfo finfo)
{
	fVInfo = vInfo;
	fInfo = finfo;
	fBuffer = mmap(0, fInfo.line_length * fVInfo.yres_virtual,
		PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)0);
	
	/*int x,y;

	for (x=0;x<fVInfo.xres;x++)
		for (y=0;y<fVInfo.yres;y++)
		{
			long location = (x+fVInfo.xoffset) * (fVInfo.bits_per_pixel/8) + (y+fVInfo.yoffset) * fInfo.line_length;
			*((uint32_t*)(fBuffer + location)) = pixel_color(0xFF,0x00,0xFF, &fVInfo);
		}*/
}


FBDevBuffer::~FBDevBuffer()
{
	UNIMPLEMENTED();
}


status_t
FBDevBuffer::InitCheck() const
{
	UNIMPLEMENTED();
	if (fBuffer != MAP_FAILED)
		return B_OK;

	printf("fbbuf err\n");
	return B_ERROR;
}


color_space
FBDevBuffer::ColorSpace() const
{
	UNIMPLEMENTED();
	return B_RGB32;
}


void*
FBDevBuffer::Bits() const
{
	UNIMPLEMENTED();
	return (void*)fBuffer;
}


uint32
FBDevBuffer::BytesPerRow() const
{
	UNIMPLEMENTED();
	return fInfo.line_length;
}


uint32
FBDevBuffer::Width() const
{
	UNIMPLEMENTED();
	return fVInfo.xres;
}


uint32
FBDevBuffer::Height() const
{
	UNIMPLEMENTED();
	return fVInfo.yres;
}
