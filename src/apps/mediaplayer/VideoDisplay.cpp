/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "VideoDisplay.h"

#include <math.h>

#include <media2/MediaDefs.h>


video_display_size
compute_video_display_size(uint32 width, uint32 height,
	uint32 pixelWidthAspect, uint32 pixelHeightAspect, uint32 rotation)
{
	video_display_size size = { width, height };
	if (width == 0 || height == 0)
		return size;
	if (pixelWidthAspect == 0)
		pixelWidthAspect = 1;
	if (pixelHeightAspect == 0)
		pixelHeightAspect = 1;

	// DAR is width * PAR : height; a rotated image shows the swapped axes.
	double displayWidth = (double)width * pixelWidthAspect
		/ pixelHeightAspect;
	double displayHeight = (double)height;
	if (rotation == B_VIDEO_ROTATE_90 || rotation == B_VIDEO_ROTATE_270) {
		double swap = displayWidth;
		displayWidth = displayHeight;
		displayHeight = swap;
	}

	size.width = (uint32)floor(displayWidth + 0.5);
	size.height = (uint32)floor(displayHeight + 0.5);
	if (size.width == 0)
		size.width = 1;
	if (size.height == 0)
		size.height = 1;
	return size;
}


video_display_size
clamp_video_display_size(video_display_size size, float boundsWidth,
	float boundsHeight)
{
	if (size.width == 0 || size.height == 0)
		return size;
	if (boundsWidth <= 0 || boundsHeight <= 0)
		return size;

	double scale = boundsWidth / ((double)size.width);
	const double scaleY = boundsHeight / ((double)size.height);
	if (scaleY < scale)
		scale = scaleY;
	if (scale >= 1.0)
		return size;

	video_display_size clamped;
	clamped.width = (uint32)floor(size.width * scale + 0.5);
	clamped.height = (uint32)floor(size.height * scale + 0.5);
	if (clamped.width == 0)
		clamped.width = 1;
	if (clamped.height == 0)
		clamped.height = 1;
	return clamped;
}
