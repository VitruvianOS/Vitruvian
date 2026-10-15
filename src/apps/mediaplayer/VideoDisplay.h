/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _VIDEO_DISPLAY_H
#define _VIDEO_DISPLAY_H


#include <SupportDefs.h>


// What the window should be sized to: pixel aspect folded in, rotation
// applied. Plain helpers so the sizing tests need no app_server.
struct video_display_size {
	uint32	width;
	uint32	height;
};


video_display_size compute_video_display_size(uint32 width, uint32 height,
	uint32 pixelWidthAspect, uint32 pixelHeightAspect, uint32 rotation);


video_display_size clamp_video_display_size(video_display_size size,
	float boundsWidth, float boundsHeight);


#endif // _VIDEO_DISPLAY_H
