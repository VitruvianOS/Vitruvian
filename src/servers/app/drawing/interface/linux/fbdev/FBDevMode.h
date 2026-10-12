/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * fbdev mode-probe helpers shared by app_server and the host tests.
 */
#ifndef FBDEV_MODE_H
#define FBDEV_MODE_H

#include <Accelerant.h>

#include <linux/fb.h>


// Mode identity: timing, size and space. Flags, panning start and
// activate are not part of what makes two modes the same mode.
static inline bool
fbdev_var_same_mode(const struct fb_var_screeninfo& a,
	const struct fb_var_screeninfo& b)
{
	return a.xres == b.xres && a.yres == b.yres
		&& a.xres_virtual == b.xres_virtual
		&& a.yres_virtual == b.yres_virtual
		&& a.xoffset == b.xoffset && a.yoffset == b.yoffset
		&& a.bits_per_pixel == b.bits_per_pixel
		&& a.pixclock == b.pixclock
		&& a.left_margin == b.left_margin
		&& a.right_margin == b.right_margin
		&& a.upper_margin == b.upper_margin
		&& a.lower_margin == b.lower_margin
		&& a.hsync_len == b.hsync_len
		&& a.vsync_len == b.vsync_len
		&& a.sync == b.sync && a.vmode == b.vmode;
}


// Fill an FB_ACTIVATE_TEST probe. Fixed-mode firmware framebuffers
// have no fb_check_var and answer 0 after copying the current var
// over the request, so the probe must carry a changed field to be
// distinguishable from that copy.
static inline void
fbdev_prepare_mode_probe(struct fb_var_screeninfo& probe,
	const struct fb_var_screeninfo& current)
{
	probe = current;
	if (probe.pixclock > 100)
		probe.pixclock -= probe.pixclock / 100;
	else
		probe.right_margin += 1;
	probe.activate = FB_ACTIVATE_TEST;
}


// Probe outcome: only a driver that ran its own check (the var moved)
// counts as mode-set capable.
static inline bool
fbdev_mode_probe_supported(bool ioctlOk,
	const struct fb_var_screeninfo& probe,
	const struct fb_var_screeninfo& current)
{
	return ioctlOk && !fbdev_var_same_mode(probe, current);
}


// SetMode read-back outcome: the driver kept its previous var, so the
// ioctl answered 0 without programming anything.
static inline bool
fbdev_mode_was_ignored(const struct fb_var_screeninfo& requested,
	const struct fb_var_screeninfo& previous,
	const struct fb_var_screeninfo& readBack)
{
	return !fbdev_var_same_mode(readBack, requested)
		&& fbdev_var_same_mode(readBack, previous);
}


// Fixed-mode accept: same space, virtual size and timing. The
// display_mode flags and the panning start do not make a different
// mode than the one the panel opened with.
static inline bool
fbdev_fixed_mode_equal(const display_mode& a, const display_mode& b)
{
	return a.space == b.space
		&& a.virtual_width == b.virtual_width
		&& a.virtual_height == b.virtual_height
		&& a.timing.pixel_clock == b.timing.pixel_clock
		&& a.timing.h_display == b.timing.h_display
		&& a.timing.h_sync_start == b.timing.h_sync_start
		&& a.timing.h_sync_end == b.timing.h_sync_end
		&& a.timing.h_total == b.timing.h_total
		&& a.timing.v_display == b.timing.v_display
		&& a.timing.v_sync_start == b.timing.v_sync_start
		&& a.timing.v_sync_end == b.timing.v_sync_end
		&& a.timing.v_total == b.timing.v_total
		&& a.timing.flags == b.timing.flags;
}

#endif // FBDEV_MODE_H
