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
#include <math.h>
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
	fModeSetSupported(false),
	fBlankSupported(false),
	fDPMSState(B_DPMS_ON),
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

	// Derive the current mode from the firmware's real var: the preflet
	// computes the refresh from these totals, so constants misreport it.
	memset(&fDisplayMode, 0, sizeof(fDisplayMode));
	_AdoptVar(fVInfo);
	fDisplayMode.space = space;

	// FB_ACTIVATE_TEST is a dry run: a driver that cannot change modes
	// rejects it; the panel is never touched either way.
	struct fb_var_screeninfo probe = fVInfo;
	probe.activate = FB_ACTIVATE_TEST;
	fModeSetSupported = ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &probe) == 0;
	fBlankSupported = ioctl(fFrameBuffer, FBIOBLANK, FB_BLANK_UNBLANK) == 0;
	fprintf(stderr, "FBDevHWInterface: fb_set_var %s, blank %s\n",
		fModeSetSupported ? "supported" : "rejected (fixed mode)",
		fBlankSupported ? "supported" : "rejected");

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


void
FBDevHWInterface::_AdoptVar(const struct fb_var_screeninfo& var)
{
	// Inverse of SetMode's mapping; margins are sync-to-picture (left,
	// lower) and picture-to-sync (right, upper) per fb.h.
	fDisplayMode.timing.h_display = var.xres;
	fDisplayMode.timing.h_sync_start = var.xres + var.right_margin;
	fDisplayMode.timing.h_sync_end = fDisplayMode.timing.h_sync_start
		+ var.hsync_len;
	fDisplayMode.timing.h_total = fDisplayMode.timing.h_sync_end
		+ var.left_margin;
	fDisplayMode.timing.v_display = var.yres;
	fDisplayMode.timing.v_sync_start = var.yres + var.upper_margin;
	fDisplayMode.timing.v_sync_end = fDisplayMode.timing.v_sync_start
		+ var.vsync_len;
	fDisplayMode.timing.v_total = fDisplayMode.timing.v_sync_end
		+ var.lower_margin;
	fDisplayMode.timing.pixel_clock = (var.pixclock != 0)
		? (uint32)llround(1e12 / (double)var.pixclock / 1000.0) : 65000;
	fDisplayMode.timing.flags = 0;
	if (var.sync & FB_SYNC_HOR_HIGH_ACT)
		fDisplayMode.timing.flags |= B_POSITIVE_HSYNC;
	if (var.sync & FB_SYNC_VERT_HIGH_ACT)
		fDisplayMode.timing.flags |= B_POSITIVE_VSYNC;
	if (var.vmode & FB_VMODE_INTERLACED)
		fDisplayMode.timing.flags |= B_TIMING_INTERLACED;
	fDisplayMode.virtual_width = var.xres;
	fDisplayMode.virtual_height = var.yres;
	fDisplayMode.h_display_start = 0;
	fDisplayMode.v_display_start = 0;
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

	// Depth switching is out of scope; the 32 bpp fixup at Init is the
	// only move this backend makes.
	if (mode.space != fDisplayMode.space)
		return B_ERROR;

	if (!fModeSetSupported) {
		// Fixed-mode panel: only the opened mode is real. Refuse the
		// rest so the preflet shows its honest alert instead of a lie.
		if (memcmp(&mode, &fDisplayMode, sizeof(mode)) == 0)
			return B_OK;
		return B_UNSUPPORTED;
	}

	if (mode.timing.pixel_clock == 0 || mode.timing.h_display == 0
		|| mode.timing.v_display == 0
		|| mode.timing.h_sync_start < mode.timing.h_display
		|| mode.timing.h_sync_end < mode.timing.h_sync_start
		|| mode.timing.h_total < mode.timing.h_sync_end
		|| mode.timing.v_sync_start < mode.timing.v_display
		|| mode.timing.v_sync_end < mode.timing.v_sync_start
		|| mode.timing.v_total < mode.timing.v_sync_end)
		return B_BAD_VALUE;

	struct fb_var_screeninfo var = fVInfo;
	var.xres = mode.timing.h_display;
	var.yres = mode.timing.v_display;
	var.xres_virtual = var.xres;
	var.yres_virtual = var.yres;
	var.xoffset = 0;
	var.yoffset = 0;
	var.pixclock = (uint32)llround(1e12
		/ ((double)mode.timing.pixel_clock * 1000.0));
	var.right_margin = mode.timing.h_sync_start - mode.timing.h_display;
	var.hsync_len = mode.timing.h_sync_end - mode.timing.h_sync_start;
	var.left_margin = mode.timing.h_total - mode.timing.h_sync_end;
	var.upper_margin = mode.timing.v_sync_start - mode.timing.v_display;
	var.vsync_len = mode.timing.v_sync_end - mode.timing.v_sync_start;
	var.lower_margin = mode.timing.v_total - mode.timing.v_sync_end;
	var.sync = 0;
	if (mode.timing.flags & B_POSITIVE_HSYNC)
		var.sync |= FB_SYNC_HOR_HIGH_ACT;
	if (mode.timing.flags & B_POSITIVE_VSYNC)
		var.sync |= FB_SYNC_VERT_HIGH_ACT;
	var.vmode = (mode.timing.flags & B_TIMING_INTERLACED)
		? FB_VMODE_INTERLACED : FB_VMODE_NONINTERLACED;
	// FORCE is how fbset makes drivers reprogram a refresh-only change.
	var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

	// Refuse before the kernel can half-apply; depth stays the current one.
	if (var.pixclock < 1000 || var.pixclock > 1000000000
		|| var.left_margin < 1 || var.right_margin < 1
		|| var.hsync_len < 1 || var.upper_margin < 1
		|| var.lower_margin < 1 || var.vsync_len < 1
		|| (uint64)var.xres_virtual * var.yres_virtual
			* var.bits_per_pixel / 8 > fInfo.smem_len)
		return B_BAD_VALUE;

	if (ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &var) != 0) {
		fprintf(stderr, "FBDevHWInterface: FBIOPUT_VSCREENINFO "
			"failed: %m\n");
		return B_ERROR;
	}

	// Drivers round; GetMode must report what was programmed, not asked.
	struct fb_var_screeninfo oldVInfo = fVInfo;
	struct fb_var_screeninfo readBack;
	if (ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &readBack) != 0) {
		fprintf(stderr, "FBDevHWInterface: FBIOGET_VSCREENINFO "
			"failed: %m\n");
		// The driver may already have switched; undo it.
		ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &oldVInfo);
		return B_ERROR;
	}

	delete fMemBackBuffer;
	fMemBackBuffer = NULL;
	delete fFrontBuffer;
	fFrontBuffer = NULL;

	fVInfo = readBack;
	_AdoptVar(fVInfo);

	fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);
	if (fFrontBuffer == NULL || fFrontBuffer->InitCheck() != B_OK) {
		// Remap died: put the old mode back rather than leave a dead
		// mmap that SIGBUSes the next draw.
		delete fFrontBuffer;
		fFrontBuffer = NULL;
		ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &oldVInfo);
		fVInfo = oldVInfo;
		_AdoptVar(fVInfo);
		fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo, fInfo);
		if (fFrontBuffer == NULL || fFrontBuffer->InitCheck() != B_OK) {
			delete fFrontBuffer;
			fFrontBuffer = NULL;
		}
		return B_ERROR;
	}

	if (fVInfo.xres > 0 && fVInfo.yres > 0)
		fMemBackBuffer = new MallocBuffer(fVInfo.xres, fVInfo.yres);

	return B_OK;
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
	if (fModeSetSupported) {
		*_low = 25000;
		*_high = 250000;
	} else {
		// min==max==current clock makes the preflet disable its refresh
		// menu (single-frequency branch) instead of offering fiction.
		*_low = *_high = fDisplayMode.timing.pixel_clock;
	}
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
	if (candidate->space != fDisplayMode.space)
		return B_BAD_VALUE;
	if (candidate->virtual_width == fDisplayMode.virtual_width
		&& candidate->virtual_height == fDisplayMode.virtual_height)
		return B_OK;
	if (!fModeSetSupported)
		return B_BAD_VALUE;
	return B_OK;
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
	// Accept-and-ignore was the same silent-drop class as SetMode.
	if (!fBlankSupported)
		return B_UNSUPPORTED;

	uint32 blank;
	switch (state) {
		case B_DPMS_ON:
			blank = FB_BLANK_UNBLANK;
			break;
		case B_DPMS_STAND_BY:
			blank = FB_BLANK_VSYNC_SUSPEND;
			break;
		case B_DPMS_SUSPEND:
			blank = FB_BLANK_HSYNC_SUSPEND;
			break;
		case B_DPMS_OFF:
			blank = FB_BLANK_POWERDOWN;
			break;
		default:
			return B_BAD_VALUE;
	}

	if (ioctl(fFrameBuffer, FBIOBLANK, blank) != 0) {
		fprintf(stderr, "FBDevHWInterface: FBIOBLANK failed: %m\n");
		return B_ERROR;
	}

	fDPMSState = state;
	return B_OK;
}


uint32
FBDevHWInterface::DPMSMode()
{
	CALLED();
	return fDPMSState;
}


uint32
FBDevHWInterface::DPMSCapabilities()
{
	CALLED();
	if (!fBlankSupported)
		return 0;
	return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
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
