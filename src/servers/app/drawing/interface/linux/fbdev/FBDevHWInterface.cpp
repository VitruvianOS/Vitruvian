/*
 * Copyright 2019, Dario Casalinuovo.
 * Distributed under the terms of the GPL License.
 */

#include "FBDevHWInterface.h"

#include "FBDevBuffer.h"
#include "FBDevFormat.h"
#include "FBDevMode.h"
#include "FBDevPanning.h"
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
	fPanningSupported(false),
	fVsyncSupported(false),
	fPageCount(1),
	fFrontPage(0),
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
	// rejects it. Fixed-mode firmware framebuffers have no fb_check_var
	// and answer 0 after copying the current var over the request, so
	// probe with a changed field and treat an unchanged answer as the
	// refusal it is.
	struct fb_var_screeninfo probe;
	fbdev_prepare_mode_probe(probe, fVInfo);
	fModeSetSupported = fbdev_mode_probe_supported(
		ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &probe) == 0,
		probe, fVInfo);
	// The probe unblanks as a side effect, which is also what we want
	// once the desktop is coming up.
	fBlankSupported = ioctl(fFrameBuffer, FBIOBLANK, FB_BLANK_UNBLANK) == 0;
	fprintf(stderr, "FBDevHWInterface: fb_set_var %s, blank %s\n",
		fModeSetSupported ? "supported" : "rejected (fixed mode)",
		fBlankSupported ? "supported" : "rejected");

	// Before the mmap: on success the mapping has to cover all the
	// pages the probe just asked the driver for.
	_ProbePanning();

	fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo);
	if (fFrontBuffer == NULL || fFrontBuffer->InitCheck() != B_OK) {
		fprintf(stderr, "FBDevHWInterface: framebuffer mmap failed\n");
		delete fFrontBuffer;
		fFrontBuffer = NULL;
		close(fFrameBuffer);
		fFrameBuffer = -1;
		return;
	}
	fFrontPageView.SetTo(fFrontBuffer, 0);

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
	// The console resumes on page 0; hand the scanout back there before
	// text mode comes back, or the user reads whatever page we panned to.
	_RestoreScanoutOrigin();

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


void
FBDevHWInterface::_ProbePanning()
{
	fPanningSupported = false;
	fPageCount = 1;
	fFrontPage = 0;
	fFrontPageView.SetTo(NULL, 0);

	uint32 page;
	for (page = 0; page < kMaxPanningPages; page++)
		fPageDirty[page].MakeEmpty();

	if (fFrameBuffer < 0)
		return;

	struct fbdev_panning_ops ops;
	ops.context = this;
	ops.getVar = &FBDevHWInterface::_GetVarOp;
	ops.getFix = &FBDevHWInterface::_GetFixOp;
	ops.setVar = &FBDevHWInterface::_SetVarOp;
	ops.pan = &FBDevHWInterface::_PanOp;
	ops.waitVsync = &FBDevHWInterface::_WaitVsyncOp;

	struct fbdev_panning_caps caps
		= fbdev_probe_panning(fVInfo, fInfo, &ops);
	fVsyncSupported = caps.vsyncSupported;

	// Adopt whatever the probe left the device at: virtual size, fix
	// info, and the scanout at the origin.
	if (ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &fVInfo) != 0)
		fprintf(stderr, "FBDevHWInterface: FBIOGET_VSCREENINFO after "
			"the pan probe failed: %m\n");
	if (ioctl(fFrameBuffer, FBIOGET_FSCREENINFO, &fInfo) != 0)
		fprintf(stderr, "FBDevHWInterface: FBIOGET_FSCREENINFO after "
			"the pan probe failed: %m\n");

	if (!caps.supported) {
		fprintf(stderr, "FBDevHWInterface: panning unsupported, "
			"keeping the shadow copy path\n");
		return;
	}

	fPanningSupported = true;
	fPageCount = caps.pageCount;
	fFrontPage = 0;

	// Every page starts stale: the RAM shadow is the only current copy.
	BRegion full(BRect(0, 0, fVInfo.xres - 1, fVInfo.yres - 1));
	for (page = 0; page < fPageCount; page++)
		fPageDirty[page] = full;

	fprintf(stderr, "FBDevHWInterface: panning with %u pages, vsync "
		"%s\n", fPageCount,
		fVsyncSupported ? "available" : "unavailable");
}


void
FBDevHWInterface::_DropPanning(const char* reason)
{
	bool wasPanning = fPanningSupported;
	fPanningSupported = false;
	fPageCount = 1;
	fFrontPage = 0;

	uint32 page;
	for (page = 0; page < kMaxPanningPages; page++)
		fPageDirty[page].MakeEmpty();

	if (fFrontBuffer == NULL)
		return;
	fFrontPageView.SetTo(fFrontBuffer, 0);

	if (!wasPanning)
		return;

	fprintf(stderr, "FBDevHWInterface: panning disabled: %s\n", reason);

	// Rebuild page 0 from the RAM shadow so the one displayed page is
	// whole again, then put the scanout at the origin.
	if (fMemBackBuffer != NULL && fVInfo.xres > 0 && fVInfo.yres > 0) {
		BRegion full(BRect(0, 0, fVInfo.xres - 1, fVInfo.yres - 1));
		_BlitToPage(0, full);
	}
	_RestoreScanoutOrigin();
}


void
FBDevHWInterface::_RestoreScanoutOrigin()
{
	if (fFrameBuffer < 0)
		return;
	if (fVInfo.xoffset == 0 && fVInfo.yoffset == 0)
		return;

	struct fb_var_screeninfo var = fVInfo;
	var.xoffset = 0;
	var.yoffset = 0;
	var.activate = FB_ACTIVATE_NOW;
	if (ioctl(fFrameBuffer, FBIOPAN_DISPLAY, &var) == 0
		|| ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &var) == 0) {
		fVInfo.xoffset = 0;
		fVInfo.yoffset = 0;
	} else
		fprintf(stderr, "FBDevHWInterface: could not restore the "
			"scanout origin: %m\n");
}


bool
FBDevHWInterface::_PanTo(uint32 page)
{
	if (fFrameBuffer < 0 || fFrontBuffer == NULL)
		return false;

	struct fb_var_screeninfo var;
	fbdev_prepare_pan(var, fVInfo, page * fVInfo.yres, fPageCount,
		fVsyncSupported);

	if (fPageCount == 2 && fVsyncSupported) {
		// Pace the pan so the just-departed page is not left mid
		// rewrite. Cache the failure: FB_ACTIVATE_VBL is all that is
		// left then.
		uint32 sequence = 0;
		if (ioctl(fFrameBuffer, FBIO_WAITFORVSYNC, &sequence) != 0)
			fVsyncSupported = false;
	}

	if (ioctl(fFrameBuffer, FBIOPAN_DISPLAY, &var) != 0) {
		fprintf(stderr, "FBDevHWInterface: FBIOPAN_DISPLAY to page "
			"%u failed: %m\n", page);
		return false;
	}

	// The same trap as the mode probe: a driver that takes the ioctl
	// and ignores it fails the read-back, not the return value.
	struct fb_var_screeninfo readBack;
	if (ioctl(fFrameBuffer, FBIOGET_VSCREENINFO, &readBack) != 0) {
		fprintf(stderr, "FBDevHWInterface: FBIOGET_VSCREENINFO after "
			"pan failed: %m\n");
		return false;
	}
	if (!fbdev_pan_read_back_ok(readBack, page * fVInfo.yres)) {
		fprintf(stderr, "FBDevHWInterface: pan to page %u ignored "
			"(yoffset %u)\n", page, readBack.yoffset);
		return false;
	}

	fVInfo = readBack;
	fFrontPage = page;
	fFrontPageView.SetTo(fFrontBuffer, page);
	return true;
}


void
FBDevHWInterface::_PresentRegion(BRegion& region)
{
	if (!fPanningSupported || region.CountRects() == 0)
		return;

	// The page after the one on screen is the hidden one; it holds a
	// frame up to N-1 presents old, so it gets everything accumulated
	// for it, not just this damage.
	uint32 nextPage = fbdev_next_page(fFrontPage, fPageCount);
	fPageDirty[nextPage].Include(&region);
	BRegion toCopy = fPageDirty[nextPage];
	fPageDirty[nextPage].MakeEmpty();

	uint32 page;
	for (page = 0; page < fPageCount; page++) {
		if (page != nextPage)
			fPageDirty[page].Include(&toCopy);
	}

	_BlitToPage(nextPage, toCopy);

	if (!_PanTo(nextPage))
		_DropPanning("FBIOPAN_DISPLAY did not stick");
}


void
FBDevHWInterface::_BlitToPage(uint32 page, const BRegion& region)
{
	if (fMemBackBuffer == NULL || fFrontBuffer == NULL)
		return;

	FBDevPage target(fFrontBuffer, page);
	if (target.InitCheck() != B_OK)
		return;

	uint8* src = (uint8*)fMemBackBuffer->Bits();
	uint32 srcBPR = fMemBackBuffer->BytesPerRow();

	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		clipping_rect r = region.RectAtInt(i);
		uint8* srcOffset = src + (uint64)r.top * srcBPR
			+ (uint64)r.left * 4;
		_CopyToBuffer(&target, srcOffset, srcBPR, r.left, r.top, r.right,
			r.bottom);
	}
}


/*static*/ int
FBDevHWInterface::_GetVarOp(void* context, struct fb_var_screeninfo* var)
{
	FBDevHWInterface* self = (FBDevHWInterface*)context;
	return ioctl(self->fFrameBuffer, FBIOGET_VSCREENINFO, var);
}


/*static*/ int
FBDevHWInterface::_GetFixOp(void* context, struct fb_fix_screeninfo* fix)
{
	FBDevHWInterface* self = (FBDevHWInterface*)context;
	return ioctl(self->fFrameBuffer, FBIOGET_FSCREENINFO, fix);
}


/*static*/ int
FBDevHWInterface::_SetVarOp(void* context, struct fb_var_screeninfo* var)
{
	FBDevHWInterface* self = (FBDevHWInterface*)context;
	return ioctl(self->fFrameBuffer, FBIOPUT_VSCREENINFO, var);
}


/*static*/ int
FBDevHWInterface::_PanOp(void* context, const struct fb_var_screeninfo* var)
{
	FBDevHWInterface* self = (FBDevHWInterface*)context;
	return ioctl(self->fFrameBuffer, FBIOPAN_DISPLAY, var);
}


/*static*/ int
FBDevHWInterface::_WaitVsyncOp(void* context)
{
	FBDevHWInterface* self = (FBDevHWInterface*)context;
	uint32 sequence = 0;
	return ioctl(self->fFrameBuffer, FBIO_WAITFORVSYNC, &sequence);
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
		// Fixed-mode panel: only the opened mode is real. Compare
		// timing, size and space, so an equal mode from the preflet
		// with different flags or panning start is not refused.
		if (fbdev_fixed_mode_equal(mode, fDisplayMode))
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

	// A driver that kept its own var answered 0 without programming
	// anything; report that, not the old silent B_OK.
	if (fbdev_mode_was_ignored(var, oldVInfo, readBack)) {
		fprintf(stderr, "FBDevHWInterface: driver ignored "
			"FBIOPUT_VSCREENINFO\n");
		return B_UNSUPPORTED;
	}

	delete fMemBackBuffer;
	fMemBackBuffer = NULL;
	delete fFrontBuffer;
	fFrontBuffer = NULL;
	fFrontPageView.SetTo(NULL, 0);

	fVInfo = readBack;
	_AdoptVar(fVInfo);

	// A different xres can move line_length; GetDeviceInfo and the
	// smem_len guard read fInfo, so refresh it with the new var.
	if (ioctl(fFrameBuffer, FBIOGET_FSCREENINFO, &fInfo) != 0)
		fprintf(stderr, "FBDevHWInterface: FBIOGET_FSCREENINFO "
			"failed: %m\n");

	// The pages and the mapping change with the mode: re-derive
	// panning from the driver as it is now, before the mmap.
	_ProbePanning();

	fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo);
	if (fFrontBuffer == NULL || fFrontBuffer->InitCheck() != B_OK) {
		// Remap died: put the old mode back rather than leave a dead
		// mmap that SIGBUSes the next draw.
		delete fFrontBuffer;
		fFrontBuffer = NULL;
		ioctl(fFrameBuffer, FBIOPUT_VSCREENINFO, &oldVInfo);
		fVInfo = oldVInfo;
		_AdoptVar(fVInfo);
		if (ioctl(fFrameBuffer, FBIOGET_FSCREENINFO, &fInfo) != 0)
			fprintf(stderr, "FBDevHWInterface: "
				"FBIOGET_FSCREENINFO failed: %m\n");
		fFrontBuffer = new FBDevBuffer(fFrameBuffer, fVInfo);
		if (fFrontBuffer == NULL || fFrontBuffer->InitCheck() != B_OK) {
			delete fFrontBuffer;
			fFrontBuffer = NULL;
		}
		// The old mode is back on a single-page mapping; whatever
		// the probe found before the rollback is gone with it.
		_DropPanning("SetMode rollback");
		return B_ERROR;
	}
	fFrontPageView.SetTo(fFrontBuffer, 0);

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
	// With panning the displayed page, not the mapping base, is what
	// outside readers must see.
	RenderingBuffer* buffer = FrontBuffer();
	if (buffer == NULL || buffer->InitCheck() != B_OK)
		return B_NO_INIT;

	config.frame_buffer = buffer->Bits();
	config.frame_buffer_dma = buffer->Bits();
	config.bytes_per_row = buffer->BytesPerRow();
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
FBDevHWInterface::WaitForRetrace(bigtime_t /*timeout*/)
{
	CALLED();
	// Probed once at Init and after every SetMode; the ioctl blocks
	// until vblank where the driver implements it at all.
	if (!fVsyncSupported)
		return B_UNSUPPORTED;

	uint32 sequence = 0;
	if (ioctl(fFrameBuffer, FBIO_WAITFORVSYNC, &sequence) != 0)
		return B_ERROR;
	return B_OK;
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
	// With panning the page the scanout shows, not the mapping base;
	// ReadBitmap and the cursor read what is actually on screen.
	if (fPanningSupported)
		return const_cast<FBDevPage*>(&fFrontPageView);
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


void
FBDevHWInterface::OnSeatEnabled()
{
	if (!LockExclusiveAccess())
		return;

	// Whatever the old session knew about the driver may be stale, and
	// the scanout is wherever the console left it. Re-probe, then put
	// the whole shadow frame back on screen.
	_ProbePanning();

	if (fMemBackBuffer != NULL && fMemBackBuffer->Width() > 0
		&& fMemBackBuffer->Height() > 0) {
		BRegion full(BRect(0, 0, fMemBackBuffer->Width() - 1,
			fMemBackBuffer->Height() - 1));
		CopyBackToFront(full);
	}

	UnlockExclusiveAccess();
}


void
FBDevHWInterface::OnSeatDisabled()
{
	if (!LockExclusiveAccess())
		return;

	// The console drew to page 0 while we held graphics mode; show it
	// before janus hands the VT back to text.
	_RestoreScanoutOrigin();
	fFrontPage = 0;
	fFrontPageView.SetTo(fFrontBuffer, 0);

	uint32 page;
	for (page = 0; page < kMaxPanningPages; page++)
		fPageDirty[page].MakeEmpty();

	UnlockExclusiveAccess();
}


status_t
FBDevHWInterface::CopyBackToFront(const BRect& frame)
{
	if (!fPanningSupported || fMemBackBuffer == NULL)
		return HWInterface::CopyBackToFront(frame);

	IntRect area(frame);
	IntRect bufferClip(fMemBackBuffer->Bounds());
	if (!area.IsValid() || !area.Intersects(bufferClip))
		return B_BAD_VALUE;
	area = bufferClip & area;

	// Same dance as the shadow path: the cursor is blended into the
	// RAM shadow first, so the pan copy carries it into the hidden
	// page with the rest of the frame.
	bool cursorLocked = fFloatingOverlaysLock.Lock();
	_DrawCursor(area);
	if (cursorLocked)
		fFloatingOverlaysLock.Unlock();

	BRegion region((BRect)area);
	_PresentRegion(region);
	return B_OK;
}


status_t
FBDevHWInterface::CopyBackToFront(const BRegion& region)
{
	if (!fPanningSupported || fMemBackBuffer == NULL)
		return HWInterface::CopyBackToFront(region);

	if (fMemBackBuffer->Width() == 0 || fMemBackBuffer->Height() == 0)
		return B_BAD_VALUE;

	IntRect clip(fMemBackBuffer->Bounds());
	BRegion clipped;
	int32 count = region.CountRects();
	for (int32 i = 0; i < count; i++) {
		IntRect area(region.RectAt(i));
		if (area.IsValid() && area.Intersects(clip))
			clipped.Include((BRect)(clip & area));
	}
	if (clipped.CountRects() == 0)
		return B_BAD_VALUE;

	// One pan for the whole region, cursor blend included.
	bool cursorLocked = fFloatingOverlaysLock.Lock();
	_DrawCursor(IntRect(clipped.Frame()));
	if (cursorLocked)
		fFloatingOverlaysLock.Unlock();

	_PresentRegion(clipped);
	return B_OK;
}
