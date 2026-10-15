/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "FullScreenState.h"


// Full screen: hide the strip and the cursor after the mouse has been
// idle this long.
static const bigtime_t kControlsHideDelay = 2000000;
static const bigtime_t kCursorHideDelay = 1000000;


FullScreenState::FullScreenState()
	:
	fFullscreen(false),
	fControlsVisible(false),
	fCursorHidden(false),
	fLastRevealTime(0)
{
}


FullScreenState::Result
FullScreenState::_Snapshot(bool enter, bool exit)
{
	Result result = { enter, exit, fCursorHidden };
	return result;
}


// Full screen is refused until there is video to fill it with.
FullScreenState::Result
FullScreenState::Toggle(bool hasVideo, bigtime_t now)
{
	if (fFullscreen) {
		fFullscreen = false;
		fControlsVisible = false;
		fCursorHidden = false;
		return _Snapshot(false, true);
	}
	if (!hasVideo)
		return _Snapshot(false, false);
	fFullscreen = true;
	fControlsVisible = true;
	fCursorHidden = false;
	fLastRevealTime = now;
	return _Snapshot(true, false);
}


FullScreenState::Result
FullScreenState::Escape(bigtime_t now)
{
	(void)now;
	if (!fFullscreen)
		return _Snapshot(false, false);
	fFullscreen = false;
	fControlsVisible = false;
	fCursorHidden = false;
	return _Snapshot(false, true);
}


void
FullScreenState::MouseMoved(bigtime_t now)
{
	if (!fFullscreen)
		return;
	fControlsVisible = true;
	fCursorHidden = false;
	fLastRevealTime = now;
}


FullScreenState::Result
FullScreenState::Tick(bigtime_t now)
{
	if (!fFullscreen)
		return _Snapshot(false, false);
	if (now - fLastRevealTime > kCursorHideDelay)
		fCursorHidden = true;
	if (now - fLastRevealTime > kControlsHideDelay)
		fControlsVisible = false;
	return _Snapshot(false, false);
}
