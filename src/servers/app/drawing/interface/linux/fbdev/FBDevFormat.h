/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * fbdev pixel-format and stride helpers shared by app_server and the host test.
 */
#ifndef FBDEV_FORMAT_H
#define FBDEV_FORMAT_H


#include <GraphicsDefs.h>
#include <linux/fb.h>

#include <stdint.h>


// Maps fbdev bits_per_pixel onto the color_space app_server expects.
static inline bool
fbdev_color_space(uint32 bitsPerPixel, color_space* space)
{
	if (bitsPerPixel == 32) {
		*space = B_RGB32;
		return true;
	}
	if (bitsPerPixel == 24) {
		*space = B_RGB24;
		return true;
	}
	return false;
}


// Stride must come from line_length; width*bpp is often wrong on padded fbdev.
static inline uint32
fbdev_bytes_per_row(uint32 bitsPerPixel, uint32 width, uint32 lineLength)
{
	if (lineLength > 0)
		return lineLength;
	return width * (bitsPerPixel == 24 ? 3 : 4);
}


#endif // FBDEV_FORMAT_H
