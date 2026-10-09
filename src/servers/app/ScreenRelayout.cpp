/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ScreenRelayout.h"

#include <math.h>
#include <string.h>

#include <DeskbarPrivate.h>


// kDesktopWindowFeel from WindowPrivate.h; kept local so this file does
// not pull the whole interface kit into the unit test.
static const int32 kDesktopFeel = 1024;

// B_NOT_MOVABLE from Window.h.
static const uint32 kNotMovable = 0x00000001;

// Edge-proximity that counts as "anchored" to that screen edge.
static const float kScreenAnchorMargin = 16.0f;
// Window centre within this fraction of the screen axis stays centred.
static const float kScreenCenterTolerance = 0.125f;
// After a shrink, keep at least this much of the title tab on screen.
static const float kScreenMinTitleVisible = 24.0f;


bool
RelayoutShouldMove(bool isNormal, int32 feel, uint32 flags,
	const char* teamSignature)
{
	// Deskbar handles B_SCREEN_CHANGED itself. Match its signature: its feel
	// varies with always-on-top and any window can take its title.
	if (!isNormal)
		return false;
	if (feel == kDesktopFeel)
		return false;
	if (teamSignature != NULL
			&& strcasecmp(teamSignature, kDeskbarSignature) == 0)
		return false;
	if ((flags & kNotMovable) != 0)
		return false;

	return true;
}


float
RelayoutAxis(float left, float frameSize,
	float oldScreenLeft, float oldScreenWidth,
	float newScreenLeft, float newScreenWidth)
{
	// Classification uses the old screen; the result is placed on the new.
	// Width() is right - left; the right edge is recovered as left + size - 1.
	const float oldScreenRight = oldScreenLeft + oldScreenWidth - 1;
	const float oldFrameRight = left + frameSize - 1;
	const float oldFrameCenter = left + frameSize / 2.0f;
	const float oldScreenCenter = oldScreenLeft + oldScreenWidth / 2.0f;
	const float newScreenRight = newScreenLeft + newScreenWidth - 1;

	if (oldScreenWidth <= 0.0f || newScreenWidth <= 0.0f)
		return left;

	// Centred: stay centred on the new screen.
	if (fabsf(oldFrameCenter - oldScreenCenter)
			<= oldScreenWidth * kScreenCenterTolerance) {
		return newScreenLeft + (newScreenWidth - frameSize) / 2.0f;
	}

	// Anchored: keep the same distance to the edge the window was near.
	if (fabsf(left - oldScreenLeft) <= kScreenAnchorMargin)
		return newScreenLeft + (left - oldScreenLeft);

	if (fabsf(oldFrameRight - oldScreenRight) <= kScreenAnchorMargin) {
		const float distance = oldScreenRight - oldFrameRight;
		return newScreenRight - distance - frameSize + 1;
	}

	// Otherwise the centre keeps its relative position; size unchanged.
	const float relative = (oldFrameCenter - oldScreenLeft) / oldScreenWidth;
	return newScreenLeft + relative * newScreenWidth - frameSize / 2.0f;
}


BPoint
RelayoutOrigin(const BRect& frame, const BRect& oldFrame,
	const BRect& newFrame)
{
	BPoint origin(
		RelayoutAxis(frame.left, frame.Width(),
			oldFrame.left, oldFrame.Width(),
			newFrame.left, newFrame.Width()),
		RelayoutAxis(frame.top, frame.Height(),
			oldFrame.top, oldFrame.Height(),
			newFrame.top, newFrame.Height()));

	// Title tab must stay reachable when the screen shrinks.
	if (origin.y > newFrame.bottom - kScreenMinTitleVisible)
		origin.y = newFrame.bottom - kScreenMinTitleVisible;
	if (origin.y < newFrame.top)
		origin.y = newFrame.top;
	if (origin.x > newFrame.right - kScreenMinTitleVisible)
		origin.x = newFrame.right - kScreenMinTitleVisible;
	if (origin.x < newFrame.left)
		origin.x = newFrame.left;

	return origin;
}


bool
RelayoutFillsScreen(const BRect& frame, const BRect& screen)
{
	// Full-screen and zoomed windows keep today's behaviour.
	return fabsf(frame.left - screen.left) <= 2.0f
		&& fabsf(frame.top - screen.top) <= 2.0f
		&& fabsf(frame.right - screen.right) <= 2.0f
		&& fabsf(frame.bottom - screen.bottom) <= 2.0f;
}
