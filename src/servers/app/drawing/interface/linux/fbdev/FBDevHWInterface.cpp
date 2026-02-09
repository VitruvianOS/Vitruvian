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
	fEventStream(NULL),
	fFrontBuffer(NULL),
	fBackBuffer(NULL)
{
	//TTy::InitTTy(1);

	char* fbname = getenv("FRAMEBUFFER");
	if (!fbname)
		fbname = "/dev/fb0";

	fFrameBuffer = open(fbname, O_RDWR);

	ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo);
	ioctl(fFrameBuffer, FBIOGET_FSCREENINFO, &fInfo);

	fVInfo.grayscale = 0;
	fVInfo.bits_per_pixel = 32;
	ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &fVInfo);
	ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo);

	fDisplayMode.virtual_width = fVInfo.xres_virtual;
	fDisplayMode.virtual_height = fVInfo.yres_virtual;
	fDisplayMode.space = B_RGB32;

	fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);
	//fBackBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);

	fEventStream = new LibInputEventStream(fVInfo.xres_virtual, fVInfo.yres_virtual, NULL);

	//ioctl(TTy::gTTy, KDSETMODE, KD_GRAPHICS);
}


FBDevHWInterface::~FBDevHWInterface()
{
	CALLED();

	//TTy::DeinitTTy();
}


status_t
FBDevHWInterface::Initialize()
{
	status_t ret = HWInterface::Initialize();
	if (ret < B_OK)
		return ret;

	ret = fFrontBuffer->InitCheck();
	if (ret < B_OK)
		return ret;

	return B_OK;
}


EventStream*
FBDevHWInterface::CreateEventStream()
{
	return fEventStream;
}


status_t
FBDevHWInterface::Shutdown()
{
	CALLED();
	return B_OK;
}


status_t
FBDevHWInterface::SetMode(const display_mode& mode)
{
	CALLED();
	return B_OK;
}


void
FBDevHWInterface::GetMode(display_mode* mode)
{
	CALLED();
	*mode = fDisplayMode;
}


status_t
FBDevHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetFrameBufferConfig(frame_buffer_config& config)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetModeList(display_mode** _modeList, uint32* _count)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low, uint32* _high)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::ProposeMode(display_mode* candidate,
	const display_mode* low, const display_mode* high)
{
	CALLED();
	return B_UNSUPPORTED;
}


sem_id
FBDevHWInterface::RetraceSemaphore()
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::WaitForRetrace(bigtime_t timeout)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::SetDPMSMode(uint32 state)
{
	CALLED();
	return B_UNSUPPORTED;
}


uint32
FBDevHWInterface::DPMSMode()
{
	CALLED();
	return B_UNSUPPORTED;
}


uint32
FBDevHWInterface::DPMSCapabilities()
{
	CALLED();
	return 0;
}


status_t
FBDevHWInterface::SetBrightness(float)
{
	CALLED();
	return B_UNSUPPORTED;
}


status_t
FBDevHWInterface::GetBrightness(float*)
{
	CALLED();
	return B_UNSUPPORTED;
}


RenderingBuffer*
FBDevHWInterface::FrontBuffer() const
{
	CALLED();
	return fFrontBuffer;
}


RenderingBuffer*
FBDevHWInterface::BackBuffer() const
{
	CALLED();
	return fBackBuffer;
}


bool
FBDevHWInterface::IsDoubleBuffered() const
{
	// TODO: This will be supported
	return false;
}


status_t
FBDevHWInterface::CopyBackToFront(const BRect& frame)
{
	CALLED();
	return B_ERROR;
}
