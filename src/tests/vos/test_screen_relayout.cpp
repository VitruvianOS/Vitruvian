/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * test_screen_relayout: screen-change relayout policy.
 * Runs inside the VM only.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ScreenRelayout.h"

// Feel/flag values from Window.h / WindowPrivate.h.
static const int32 kNormalFeel = 0;		// B_NORMAL_WINDOW_FEEL
static const int32 kDesktopFeel = 1024;	// kDesktopWindowFeel
static const uint32 kNotMovable = 0x1;	// B_NOT_MOVABLE
static const char* kDeskbarSig = "application/x-vnd.Be-TSKB";

static int sPass = 0;
static int sFail = 0;


static void
check(bool ok, const char* name)
{
	printf("RESULT|%s|%s|\n", ok ? "PASS" : "FAIL", name);
	if (ok)
		sPass++;
	else
		sFail++;
}


int
main()
{
	// Skip predicate: ordinary windows move; special ones do not.
	check(RelayoutShouldMove(true, kNormalFeel, 0, NULL) == true,
		"normal-window-moves");
	check(RelayoutShouldMove(true, kNormalFeel, 0, kDeskbarSig) == false,
		"deskbar-skipped");
	check(RelayoutShouldMove(true, kDesktopFeel, 0, NULL) == false,
		"desktop-skipped");
	check(RelayoutShouldMove(true, kNormalFeel, kNotMovable, NULL) == false,
		"non-movable-skipped");
	check(RelayoutShouldMove(false, kNormalFeel, 0, NULL) == false,
		"non-normal-skipped");
	// A user window that happens to be titled "Deskbar" is not special;
	// only the team signature is.
	check(RelayoutShouldMove(true, kNormalFeel, 0,
			"application/x-vnd.Be-UserApp") == true,
		"user-window-not-deskbar-moves");

	// Geometry: a centred window stays centred when the screen grows.
	const BRect oldScreen(0, 0, 1023, 767);
	const BRect newScreen(0, 0, 1279, 1023);
	const BRect centred(412, 304, 611, 423);
	const BPoint moved = RelayoutOrigin(centred, oldScreen, newScreen);
	const float expectedX
		= (newScreen.Width() - centred.Width()) / 2.0f;
	const float expectedY
		= (newScreen.Height() - centred.Height()) / 2.0f;
	check(fabsf(moved.x - expectedX) < 0.01f
			&& fabsf(moved.y - expectedY) < 0.01f,
		"centred-window-moves");

	// Edge-anchored keeps its margin.
	const BRect anchored(16, 16, 215, 135);
	const BPoint kept = RelayoutOrigin(anchored, oldScreen, newScreen);
	check(fabsf(kept.x - 16.0f) < 0.01f && fabsf(kept.y - 16.0f) < 0.01f,
		"edge-anchored-keeps-margin");

	// Full-screen fills the old screen and is not relaid out.
	check(RelayoutFillsScreen(oldScreen, oldScreen) == true,
		"full-screen-fills");
	check(RelayoutFillsScreen(centred, oldScreen) == false,
		"ordinary-window-does-not-fill");

	printf("SUMMARY|pass=%d|fail=%d\n", sPass, sFail);
	return sFail == 0 ? 0 : 1;
}
