/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "FBDevHWInterface.h"

#include "FBDevBuffer.h"
#include "FBDevFormat.h"
#include "IntRect.h"
#include "MallocBuffer.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/kd.h>
#include <linux/vt.h>


FBDevHWInterface::FBDevHWInterface()
	:
	HWInterface(),
	fFrontBuffer(NULL),
	fMemBackBuffer(NULL),
	fFrameBuffer(-1),
	fTTY(-1),
	fEventStream(NULL)
{
	memset(&fVInfo, 0, sizeof(fVInfo));
	memset(&fInfo, 0, sizeof(fInfo));

	const char* fbname = getenv("FRAMEBUFFER");
	if (!fbname)
		fbname = "/dev/fb0";

	fFrameBuffer = open(fbname, O_RDWR);
	if (fFrameBuffer < 0) {
		fprintf(stderr, "FBDevHWInterface: cannot open %s: %m\n", fbname);
		return;
	}

	if (ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo) != 0
		|| ioctl(fFrameBuffer, FBIOGET_FSCREENINFO, &fInfo) != 0) {
		fprintf(stderr, "FBDevHWInterface: FBIOGET_* on %s failed: %m\n",
			fbname);
		close(fFrameBuffer);
		fFrameBuffer = -1;
		return;
	}

	// Prefer what the firmware already programmed; only try 32 bpp if the
	// current depth is something app_server cannot draw into.
	color_space space;
	if (!fbdev_color_space(fVInfo.bits_per_pixel, &space)) {
		fVInfo.grayscale = 0;
		fVInfo.bits_per_pixel = 32;
		if (ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &fVInfo) != 0
			|| ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo) != 0
			|| !fbdev_color_space(fVInfo.bits_per_pixel, &space)) {
			fprintf(stderr,
				"FBDevHWInterface: unsupported bpp %u on %s\n",
				fVInfo.bits_per_pixel, fbname);
			close(fFrameBuffer);
			fFrameBuffer = -1;
			return;
		}
	}

	memset(&fDisplayMode, 0, sizeof(fDisplayMode));
	fDisplayMode.timing.h_display = fVInfo.xres;
	fDisplayMode.timing.h_sync_start = fVInfo.xres + 8;
	fDisplayMode.timing.h_sync_end = fVInfo.xres + 24;
	fDisplayMode.timing.h_total = fVInfo.xres + 160;
	fDisplayMode.timing.v_display = fVInfo.yres;
	fDisplayMode.timing.v_sync_start = fVInfo.yres + 2;
	fDisplayMode.timing.v_sync_end = fVInfo.yres + 8;
	fDisplayMode.timing.v_total = fVInfo.yres + 30;
	fDisplayMode.timing.pixel_clock = 65000;
	fDisplayMode.space = space;
	fDisplayMode.virtual_width = fVInfo.xres;
	fDisplayMode.virtual_height = fVInfo.yres;

	fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);
	if (fFrontBuffer == NULL || fFrontBuffer->InitCheck() != B_OK) {
		fprintf(stderr, "FBDevHWInterface: framebuffer mmap failed\n");
		delete fFrontBuffer;
		fFrontBuffer = NULL;
		close(fFrameBuffer);
		fFrameBuffer = -1;
		return;
	}

	// Drawing goes to an in-memory B_RGBA32 buffer; dirty rects are copied
	// to the mapped framebuffer on CopyBackToFront.
	if (fVInfo.xres > 0 && fVInfo.yres > 0)
		fMemBackBuffer = new MallocBuffer(fVInfo.xres, fVInfo.yres);

	_EnterGraphicsMode();
}


FBDevHWInterface::~FBDevHWInterface()
{
	CALLED();

	_LeaveGraphicsMode();

	delete fMemBackBuffer;
	fMemBackBuffer = NULL;

	delete fFrontBuffer;
	fFrontBuffer = NULL;

	if (fFrameBuffer >= 0) {
		close(fFrameBuffer);
		fFrameBuffer = -1;
	}
}


void
FBDevHWInterface::_EnterGraphicsMode()
{
	// Stop the kernel console from drawing over the desktop. janus owns VT
	// switching; this only marks the VT we are scanning out as graphics.
	fTTY = open("/dev/tty1", O_RDWR | O_CLOEXEC);
	if (fTTY < 0)
		fTTY = open("/dev/tty0", O_RDWR | O_CLOEXEC);
	if (fTTY < 0)
		return;

	if (ioctl(fTTY, KDSETMODE, KD_GRAPHICS) != 0)
		fprintf(stderr, "FBDevHWInterface: KDSETMODE KD_GRAPHICS: %m\n");
}


void
FBDevHWInterface::_LeaveGraphicsMode()
{
	if (fTTY < 0)
		return;

	ioctl(fTTY, KDSETMODE, KD_TEXT);
	close(fTTY);
	fTTY = -1;
}


status_t
FBDevHWInterface::InitCheck() const
{
	if (fFrameBuffer < 0 || fFrontBuffer == NULL)
		return B_ERROR;
	return fFrontBuffer->InitCheck();
}


status_t
FBDevHWInterface::Initialize()
{
	status_t ret = HWInterface::Initialize();
	if (ret < B_OK)
		return ret;

	if (fFrontBuffer == NULL)
		return B_NO_INIT;

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
	_LeaveGraphicsMode();
	return B_OK;
}


status_t
FBDevHWInterface::SetMode(const display_mode& mode)
{
	CALLED();
	// fbdev cannot change mode at runtime; accept the one we opened.
	if (mode.virtual_width == fDisplayMode.virtual_width
		&& mode.virtual_height == fDisplayMode.virtual_height
		&& mode.space == fDisplayMode.space)
		return B_OK;
	return B_ERROR;
}


void
FBDevHWInterface::GetMode(display_mode* mode)
{
	CALLED();
	*mode = fDisplayMode;
}


status_t
FBDevHWInterface::GetPreferredMode(display_mode* mode)
{
	CALLED();
	if (fFrameBuffer < 0)
		return B_NO_INIT;
	*mode = fDisplayMode;
	return B_OK;
}


status_t
FBDevHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	CALLED();
	if (info == NULL || fFrameBuffer < 0)
		return B_NO_INIT;

	memset(info, 0, sizeof(*info));
	info->version = B_ACCELERANT_VERSION;
	strlcpy(info->name, "V\\OS fbdev", sizeof(info->name));
	strlcpy(info->chipset, fInfo.id[0] != '\0' ? fInfo.id : "fbdev",
		sizeof(info->chipset));
	info->memory = fInfo.smem_len;
	info->dac_speed = 250;
	return B_OK;
}


status_t
FBDevHWInterface::GetFrameBufferConfig(frame_buffer_config& config)
{
	CALLED();
	if (fFrontBuffer == NULL || fFrontBuffer->InitCheck() != B_OK)
		return B_NO_INIT;

	config.frame_buffer = fFrontBuffer->Bits();
	config.frame_buffer_dma = fFrontBuffer->Bits();
	config.bytes_per_row = fFrontBuffer->BytesPerRow();
	return B_OK;
}


status_t
FBDevHWInterface::GetModeList(display_mode** _modeList, uint32* _count)
{
	CALLED();
	if (fFrameBuffer < 0)
		return B_NO_INIT;

	display_mode* modes = new display_mode[1];
	if (modes == NULL)
		return B_NO_MEMORY;

	modes[0] = fDisplayMode;
	*_modeList = modes;
	*_count = 1;
	return B_OK;
}


status_t
FBDevHWInterface::GetPixelClockLimits(display_mode* mode, uint32* _low,
	uint32* _high)
{
	CALLED();
	if (mode == NULL || _low == NULL || _high == NULL)
		return B_BAD_VALUE;
	*_low = 25000;
	*_high = 250000;
	return B_OK;
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
	if (candidate == NULL)
		return B_BAD_VALUE;
	if (candidate->virtual_width == fDisplayMode.virtual_width
		&& candidate->virtual_height == fDisplayMode.virtual_height)
		return B_OK;
	return B_ERROR;
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
	// fbdev has no DPMS; accept and ignore.
	return B_OK;
}


uint32
FBDevHWInterface::DPMSMode()
{
	CALLED();
	return B_DPMS_ON;
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
	return fMemBackBuffer;
}


bool
FBDevHWInterface::IsDoubleBuffered() const
{
	return fMemBackBuffer != NULL;
}
