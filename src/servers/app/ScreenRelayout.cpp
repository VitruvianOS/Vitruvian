/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ScreenRelayout.h"

#include <algorithm>
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


relayout_axis
RelayoutClassify(float start, float size, float screenStart,
	float screenSize)
{
	const float centre = start + size / 2.0f;
	const float endGap = screenStart + screenSize - (start + size);

	relayout_axis axis;
	if (fabsf(centre - (screenStart + screenSize / 2.0f))
			<= screenSize * kScreenCenterTolerance) {
		axis.anchor = RELAYOUT_CENTRED;
		axis.value = 0;
	} else if (fabsf(start - screenStart) <= kScreenAnchorMargin) {
		axis.anchor = RELAYOUT_START;
		axis.value = start - screenStart;
	} else if (fabsf(endGap) <= kScreenAnchorMargin) {
		axis.anchor = RELAYOUT_END;
		axis.value = endGap;
	} else {
		axis.anchor = RELAYOUT_RELATIVE;
		axis.value = screenSize > 0
			? (centre - screenStart) / screenSize : 0.5f;
	}
	return axis;
}


float
RelayoutPlace(const relayout_axis& axis, float size, float screenStart,
	float screenSize)
{
	switch (axis.anchor) {
		case RELAYOUT_CENTRED:
			return screenStart + (screenSize - size) / 2.0f;
		case RELAYOUT_START:
			return screenStart + axis.value;
		case RELAYOUT_END:
			return screenStart + screenSize - axis.value - size;
		default:
			return screenStart + axis.value * screenSize - size / 2.0f;
	}
}


relayout_placement
RelayoutCapture(const BRect& footprint, const BRect& screen)
{
	relayout_placement placement;
	placement.valid = true;
	placement.x = RelayoutClassify(footprint.left, footprint.Width() + 1,
		screen.left, screen.Width() + 1);
	placement.y = RelayoutClassify(footprint.top, footprint.Height() + 1,
		screen.top, screen.Height() + 1);
	placement.placedAt = footprint.LeftTop();
	return placement;
}


BPoint
RelayoutOrigin(const relayout_placement& placement, const BRect& footprint,
	const BRect& screen)
{
	BPoint origin(
		floorf(RelayoutPlace(placement.x, footprint.Width() + 1,
			screen.left, screen.Width() + 1)),
		floorf(RelayoutPlace(placement.y, footprint.Height() + 1,
			screen.top, screen.Height() + 1)));

	origin.x = std::min(origin.x, screen.right + 1 - kScreenMinTitleVisible);
	origin.y = std::min(origin.y, screen.bottom + 1 - kScreenMinTitleVisible);
	origin.x = std::max(origin.x, screen.left);
	origin.y = std::max(origin.y, screen.top);
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
