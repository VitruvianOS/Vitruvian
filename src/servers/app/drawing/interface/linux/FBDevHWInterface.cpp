/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "FBDevHWInterface.h"

#include "FBDevBuffer.h"

#include <sys/ioctl.h>
#include <sys/mman.h>


FBDevHWInterface::FBDevHWInterface()
	:
	HWInterface(false, false),
	fFrontBuffer(NULL),
	fBackBuffer(NULL)
{
	fFrameBuffer = open("/dev/fb0", O_RDWR);

	// Get screen informations
	ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo);

	// Get fixed screen informations
	ioctl(fFrameBuffer, FBIOGET_FSCREENINFO, &fInfo);

	fVInfo.grayscale = 0;
	fVInfo.bits_per_pixel = 32;
	ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &fVInfo);
	ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo);

	//printf("%ld\n", fScreenSize);

	fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);
	//fBackBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);
}


FBDevHWInterface::~FBDevHWInterface()
{
	UNIMPLEMENTED();
}


status_t
FBDevHWInterface::Initialize()
{
	status_t ret = HWInterface::Initialize();
	if (ret < B_OK)
		return ret;

	printf("hw init ok\n");

	ret = fFrontBuffer->InitCheck();
	if (ret < B_OK)
		return ret;

	return B_OK;
}


status_t
FBDevHWInterface::Shutdown()
{
	UNIMPLEMENTED();
	return B_OK;
}


status_t
FBDevHWInterface::SetMode(const display_mode &mode)
{
	UNIMPLEMENTED();
	return B_UNSUPPORTED;
}


void
FBDevHWInterface::GetMode(display_mode *mode)
{
	UNIMPLEMENTED();
	if (mode != NULL)
		memset(mode, 0, sizeof(display_mode));
}


status_t
FBDevHWInterface::GetDeviceInfo(accelerant_device_info *info)
{
	UNIMPLEMENTED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetFrameBufferConfig(frame_buffer_config &config)
{
	UNIMPLEMENTED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetModeList(display_mode **_modeList, uint32 *_count)
{
	UNIMPLEMENTED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetPixelClockLimits(display_mode *mode, uint32 *_low, uint32 *_high)
{
	UNIMPLEMENTED();
}


status_t
FBDevHWInterface::GetTimingConstraints(display_timing_constraints *constraints)
{
	UNIMPLEMENTED();
}


status_t
FBDevHWInterface::ProposeMode(display_mode *candidate, const display_mode *low, const display_mode *high)
{
	UNIMPLEMENTED();
}


sem_id
FBDevHWInterface::RetraceSemaphore()
{
	UNIMPLEMENTED();
}


status_t
FBDevHWInterface::WaitForRetrace(bigtime_t timeout)
{
	UNIMPLEMENTED();
}


status_t
FBDevHWInterface::SetDPMSMode(uint32 state)
{
	UNIMPLEMENTED();
}


uint32
FBDevHWInterface::DPMSMode()
{
	UNIMPLEMENTED();
}


uint32
FBDevHWInterface::DPMSCapabilities()
{
	UNIMPLEMENTED();
}


status_t
FBDevHWInterface::SetBrightness(float)
{
	UNIMPLEMENTED();
}


status_t
FBDevHWInterface::GetBrightness(float *)
{
	UNIMPLEMENTED();
}


RenderingBuffer *
FBDevHWInterface::FrontBuffer() const
{
	UNIMPLEMENTED();
	return fFrontBuffer;
}


RenderingBuffer *
FBDevHWInterface::BackBuffer() const
{
	UNIMPLEMENTED();
	return fBackBuffer;
}


bool
FBDevHWInterface::IsDoubleBuffered() const
{
	// TODO: This will be supported
	return false;
}


status_t
FBDevHWInterface::Invalidate(const BRect &frame)
{
	UNIMPLEMENTED();
	return B_OK;
}


status_t
FBDevHWInterface::CopyBackToFront(const BRect &frame)
{
	UNIMPLEMENTED();
	return B_OK;
}
