/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * fbdev panning (double/triple buffering) probe and present helpers
 * shared by app_server and the host tests.
 */
#ifndef FBDEV_PANNING_H
#define FBDEV_PANNING_H

#include <linux/fb.h>

#include <stdint.h>


// Outcome of the panning probe. pageCount 1 means unsupported: the
// caller keeps the RAM shadow copy path unchanged.
struct fbdev_panning_caps {
	bool		supported;
	uint32_t	pageCount;
	bool		vsyncSupported;
};


// The probe runs against these so the host tests can drive the same
// code with a fake driver behind each personality.
struct fbdev_panning_ops {
	void*	context;
	int	(*getVar)(void* context, struct fb_var_screeninfo* var);
	int	(*getFix)(void* context, struct fb_fix_screeninfo* fix);
	int	(*setVar)(void* context, struct fb_var_screeninfo* var);
	int	(*pan)(void* context, const struct fb_var_screeninfo* var);
	int	(*waitVsync)(void* context);
};


// smem must hold pageCount pages at line_length; drivers that clamped
// yres_virtual advertise the shortfall here first.
static inline bool
fbdev_pan_pages_fit(const struct fb_fix_screeninfo& fix,
	uint32_t yres, uint32_t pageCount)
{
	return fix.line_length > 0
		&& (uint64_t)fix.line_length * yres * pageCount <= fix.smem_len;
}


// Page k sits at yoffset = k * yres, which the kernel's fb_pan_display
// only accepts on a ypanstep boundary. ypanstep 0 is the driver's own
// "no hardware panning" advertisement.
static inline bool
fbdev_pan_page_offset_ok(uint32_t yres, uint32_t ypanStep,
	uint32_t yresVirtual, uint32_t page)
{
	if (ypanStep == 0 || yres == 0)
		return false;
	if (yres % ypanStep != 0)
		return false;
	return page * yres < yresVirtual;
}


// Build the FBIOPAN_DISPLAY var. Double buffering (2 pages) must not
// scan out a half-written page and has no free page to fall on, so
// without a working WAITFORVSYNC the pan asks for the next vblank with
// FB_ACTIVATE_VBL. Triple buffering always has a free page and never
// blocks the drawing thread on vsync.
static inline void
fbdev_prepare_pan(struct fb_var_screeninfo& var,
	const struct fb_var_screeninfo& current, uint32_t yoffset,
	uint32_t pageCount, bool vsyncSupported)
{
	var = current;
	var.xoffset = 0;
	var.yoffset = yoffset;
	var.activate = FB_ACTIVATE_NOW;
	if (pageCount < 3 && !vsyncSupported)
		var.activate |= FB_ACTIVATE_VBL;
}


// The pan stuck only if the read-back yoffset says so; drivers that
// ignore panning answer 0 and leave the var alone.
static inline bool
fbdev_pan_read_back_ok(const struct fb_var_screeninfo& readBack,
	uint32_t yoffset)
{
	return readBack.yoffset == yoffset;
}


// Presents cycle through the pages; the page after the displayed one
// is the hidden one.
static inline uint32_t
fbdev_next_page(uint32_t frontPage, uint32_t pageCount)
{
	return pageCount > 1 ? (frontPage + 1) % pageCount : 0;
}


// Probe: request yres_virtual = N * yres for N = 3 then 2, first as an
// FB_ACTIVATE_TEST dry run, then for real, and finally pan to page 1
// and back checking the read-back yoffset. Fixed-mode drivers (efifb,
// simplefb) have no fb_check_var: the kernel copies the current var
// over the request and returns 0, so the read-back comparison is what
// refuses them and a 0 return would not. ypanstep/ywrapstep from the
// fix info are honored. On success the device is left at yoffset 0
// with pageCount pages of virtual height; on failure the original var
// is restored.
static inline struct fbdev_panning_caps
fbdev_probe_panning(const struct fb_var_screeninfo& current,
	const struct fb_fix_screeninfo& fix, struct fbdev_panning_ops* ops)
{
	struct fbdev_panning_caps caps;
	caps.supported = false;
	caps.pageCount = 1;
	caps.vsyncSupported = ops->waitVsync(ops->context) == 0;

	if (current.yres == 0)
		return caps;

	// Snapshot: the ops may write read-backs over the state the caller
	// passed in, and the restore paths must land on the opening var.
	struct fb_var_screeninfo base = current;

	static const uint32_t kCandidates[2] = { 3, 2 };
	uint32_t i;
	for (i = 0; i < 2; i++) {
		uint32_t pages = kCandidates[i];

		// Dry run: fb_check_var adjusts the request in place when the
		// driver has one; nothing is programmed yet.
		struct fb_var_screeninfo var = base;
		var.xoffset = 0;
		var.yoffset = 0;
		var.yres_virtual = pages * base.yres;
		var.activate = FB_ACTIVATE_TEST;
		if (ops->setVar(ops->context, &var) != 0)
			continue;
		if (var.yres_virtual < pages * base.yres)
			continue;

		// For real; the read-back is the truth.
		var.activate = FB_ACTIVATE_NOW;
		if (ops->setVar(ops->context, &var) != 0)
			continue;
		struct fb_var_screeninfo readBack;
		if (ops->getVar(ops->context, &readBack) != 0)
			continue;
		if (readBack.yres_virtual < pages * base.yres)
			continue;

		struct fb_fix_screeninfo readFix = fix;
		if (ops->getFix(ops->context, &readFix) != 0)
			continue;
		if (!fbdev_pan_pages_fit(readFix, base.yres, pages))
			continue;
		if (!fbdev_pan_page_offset_ok(base.yres, readFix.ypanstep,
				readBack.yres_virtual, 1))
			continue;

		// Pan to page 1 and back; a driver that accepts the ioctl and
		// ignores it fails the read-back here.
		struct fb_var_screeninfo panVar;
		bool panned = true;
		uint32_t step;
		for (step = 0; step < 2 && panned; step++) {
			uint32_t yoffset = (step == 0) ? base.yres : 0;
			fbdev_prepare_pan(panVar, readBack, yoffset, pages,
				caps.vsyncSupported);
			panned = ops->pan(ops->context, &panVar) == 0
				&& ops->getVar(ops->context, &readBack) == 0
				&& fbdev_pan_read_back_ok(readBack, yoffset);
		}
		if (!panned) {
			// Put the single-page var back before giving up on it.
			var = base;
			var.xoffset = 0;
			var.yoffset = 0;
			var.activate = FB_ACTIVATE_NOW;
			ops->setVar(ops->context, &var);
			continue;
		}

		caps.supported = true;
		caps.pageCount = pages;
		return caps;
	}

	// Nothing stuck: restore the virtual size the driver opened with.
	struct fb_var_screeninfo restore = base;
	restore.xoffset = 0;
	restore.yoffset = 0;
	restore.activate = FB_ACTIVATE_NOW;
	ops->setVar(ops->context, &restore);
	return caps;
}

#endif // FBDEV_PANNING_H
