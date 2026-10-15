/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "FBDevBuffer.h"

#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>


FBDevBuffer::FBDevBuffer(int fd, struct fb_var_screeninfo vInfo)
	:
	fBuffer((uint8_t*)MAP_FAILED),
	fSpace(B_RGB32)
{
	fVInfo = vInfo;
	memset(&fInfo, 0, sizeof(fInfo));

	// A mode change can move line_length, so the stride and the mmap
	// size must come from the fix info as it is right now, not from
	// whatever the caller last cached.
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fInfo) != 0)
		memset(&fInfo, 0, sizeof(fInfo));

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


FBDevPage::FBDevPage()
	:
	fBuffer(NULL),
	fPage(0)
{
}


FBDevPage::FBDevPage(FBDevBuffer* buffer, uint32 page)
	:
	fBuffer(buffer),
	fPage(page)
{
}


FBDevPage::~FBDevPage()
{
}


void
FBDevPage::SetTo(FBDevBuffer* buffer, uint32 page)
{
	fBuffer = buffer;
	fPage = page;
}


status_t
FBDevPage::InitCheck() const
{
	if (fBuffer == NULL || fBuffer->InitCheck() != B_OK)
		return B_ERROR;
	if (fBuffer->Bits() == NULL)
		return B_ERROR;
	return B_OK;
}


color_space
FBDevPage::ColorSpace() const
{
	if (fBuffer == NULL)
		return B_RGB32;
	return fBuffer->ColorSpace();
}


void*
FBDevPage::Bits() const
{
	if (fBuffer == NULL || fBuffer->Bits() == NULL)
		return NULL;

	uint8_t* base = (uint8_t*)fBuffer->Bits();
	return base + (uint64)fPage * fBuffer->BytesPerRow()
		* fBuffer->Height();
}


uint32
FBDevPage::BytesPerRow() const
{
	return fBuffer != NULL ? fBuffer->BytesPerRow() : 0;
}


uint32
FBDevPage::Width() const
{
	return fBuffer != NULL ? fBuffer->Width() : 0;
}


uint32
FBDevPage::Height() const
{
	return fBuffer != NULL ? fBuffer->Height() : 0;
}
