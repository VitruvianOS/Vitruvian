/*
 * Copyright 2011, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */

/* Generate mode timings using the GTF Timing Standard
 *
 * Copyright (c) 2001, Andy Ritger  aritger@nvidia.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * o Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * o Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * o Neither the name of NVIDIA nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 * NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <compute_display_timing.h>

#include <math.h>
#include <stdarg.h>


#define MARGIN_PERCENT				1.8
#define CELL_GRANULARITY			8.0
#define MIN_PORCH					1
#define V_SYNC_WIDTH				3
#define H_SYNC_PERCENT				8.0
#define MIN_VSYNC_PLUS_BACK_PORCH	550.0

#define M					600.0
#define C					40.0
#define K					128.0
#define J					20.0
#define C_PRIME				(((C - J) * K / 256.0) + J)
#define M_PRIME				(K / 256.0 * M)


status_t
compute_display_timing(uint32 width, uint32 height, float refresh,
	bool interlaced, display_timing* timing)
{
	if (width < 320 || height < 200 || width > 65536 || height > 65536
			|| refresh < 25 || refresh > 1000)
		return B_BAD_VALUE;

	bool margins = false;

	width = (uint32)(rint(width / CELL_GRANULARITY) * CELL_GRANULARITY);

	float verticalLines = interlaced ? (double)height / 2.0 : (double)height;
	float verticalFieldRate = interlaced ? refresh * 2.0 : refresh;
	float topMargin = margins ? rint(MARGIN_PERCENT / 100.0 * verticalLines) : 0.0;
	float bottomMargin = margins ? rint(MARGIN_PERCENT / 100.0 * verticalLines) : 0.0;
	float interlace = interlaced ? 0.5 : 0.0;

	float horizontalPeriodEstimate = (1.0 / verticalFieldRate
			- MIN_VSYNC_PLUS_BACK_PORCH / 1000000.0)
		/ (verticalLines + (2 * topMargin) + MIN_PORCH + interlace) * 1000000.0;

	float verticalSyncPlusBackPorch = rint(MIN_VSYNC_PLUS_BACK_PORCH
		/ horizontalPeriodEstimate);

	float totalVerticalLines = verticalLines + topMargin + bottomMargin
		+ verticalSyncPlusBackPorch + interlace + MIN_PORCH;

	float verticalFieldRateEstimate = 1.0 / horizontalPeriodEstimate
		/ totalVerticalLines * 1000000.0;

	float horizontalPeriod = horizontalPeriodEstimate
		/ (verticalFieldRate / verticalFieldRateEstimate);

	float leftMargin = margins ? rint(width * MARGIN_PERCENT / 100.0
			/ CELL_GRANULARITY) * CELL_GRANULARITY : 0.0;
	float rightMargin = margins ? rint(width * MARGIN_PERCENT / 100.0
			/ CELL_GRANULARITY) * CELL_GRANULARITY : 0.0;

	float totalActivePixels = width + leftMargin + rightMargin;

	float idealDutyCycle = C_PRIME - (M_PRIME * horizontalPeriod / 1000.0);

	float horizontalBlank = rint(totalActivePixels * idealDutyCycle
			/ (100.0 - idealDutyCycle) / (2.0 * CELL_GRANULARITY))
		* (2.0 * CELL_GRANULARITY);

	float totalPixels = totalActivePixels + horizontalBlank;
	float pixelFrequency = totalPixels / horizontalPeriod;

	float horizontalSync = rint(H_SYNC_PERCENT / 100.0 * totalPixels
			/ CELL_GRANULARITY) * CELL_GRANULARITY;
	float horizontalFrontPorch = (horizontalBlank / 2.0) - horizontalSync;
	float verticalOddFrontPorchLines = MIN_PORCH + interlace;

	timing->pixel_clock = uint32(pixelFrequency * 1000);
	timing->h_display = (uint16)width;
	timing->h_sync_start = (uint16)(width + horizontalFrontPorch);
	timing->h_sync_end = (uint16)(width + horizontalFrontPorch + horizontalSync);
	timing->h_total = (uint16)totalPixels;
	timing->v_display = (uint16)verticalLines;
	timing->v_sync_start = (uint16)(verticalLines + verticalOddFrontPorchLines);
	timing->v_sync_end = (uint16)(verticalLines + verticalOddFrontPorchLines + V_SYNC_WIDTH);
	timing->v_total = (uint16)totalVerticalLines;
	timing->flags = B_POSITIVE_HSYNC | B_POSITIVE_VSYNC
		| (interlace ? B_TIMING_INTERLACED : 0);

	return B_OK;
}
