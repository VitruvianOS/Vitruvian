/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host checks of the pointer-control core: confinement clamp math,
// relative-mode accumulation, grab arbitration and the raw-event
// valuator helpers. Nothing here needs /dev/nexus.

#include <PointerControlCore.h>
#include <XVitruvianInputProtocol.h>

#include <math.h>
#include <stdio.h>
#include <string.h>


static int sFailures = 0;


static void
check(bool ok, const char* what)
{
	if (!ok) {
		printf("FAIL %s\n", what);
		sFailures++;
	}
}


static bool
closeTo(float a, float b)
{
	return fabsf(a - b) < 0.001f;
}


static void
testClamp()
{
	pointer_constraint c;
	pointer_constraint_init(&c, kPointerConstraintConfined,
		10.0f, 20.0f, 110.0f, 220.0f, 0.0f, 0.0f);

	float x = 5.0f;
	float y = 5.0f;
	check(pointer_constraint_clamp(&c, &x, &y), "clamp reports move");
	check(closeTo(x, 10.0f) && closeTo(y, 20.0f), "clamp to top-left");

	x = 500.0f;
	y = 500.0f;
	check(pointer_constraint_clamp(&c, &x, &y), "clamp reports move far");
	check(closeTo(x, 110.0f) && closeTo(y, 220.0f), "clamp to bottom-right");

	x = 60.0f;
	y = 120.0f;
	check(!pointer_constraint_clamp(&c, &x, &y), "inside unchanged");
	check(closeTo(x, 60.0f) && closeTo(y, 120.0f), "inside value kept");

	pointer_constraint_init(&c, 0, 0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f);
	x = 50.0f;
	y = 50.0f;
	check(!pointer_constraint_clamp(&c, &x, &y), "unconfined never clamps");

	pointer_constraint_init(&c, kPointerConstraintConfined,
		10.0f, 10.0f, 5.0f, 5.0f, 0.0f, 0.0f);
	check(!pointer_constraint_clamp(&c, &x, &y), "inverted rect is a no-op");
	check(closeTo(x, 50.0f), "inverted rect leaves value");
}


static void
testRelative()
{
	pointer_constraint c;
	pointer_constraint_init(&c, kPointerConstraintRelative
		| kPointerConstraintConfined, 0.0f, 0.0f, 100.0f, 100.0f,
		50.0f, 50.0f);

	float x = 0.0f;
	float y = 0.0f;
	pointer_constraint_move_relative(&c, 5.0f, -7.0f, &x, &y);
	check(closeTo(x, 55.0f) && closeTo(y, 43.0f), "relative advances lock");

	pointer_constraint_move_relative(&c, 1000.0f, 0.0f, &x, &y);
	check(closeTo(x, 100.0f), "relative clamps to rect");
	check(closeTo(c.lockX, 100.0f), "lock point follows the clamp");

	// A confined-only constraint must not move via the relative path.
	pointer_constraint_init(&c, kPointerConstraintConfined,
		0.0f, 0.0f, 100.0f, 100.0f, 50.0f, 50.0f);
	pointer_constraint_move_relative(&c, 10.0f, 10.0f, &x, &y);
	check(closeTo(x, 100.0f) && closeTo(y, 43.0f), "relative ignored");
	check(!pointer_constraint_active(NULL), "NULL constraint inactive");

	pointer_constraint_init(&c, kPointerConstraintRelative,
		0.0f, 0.0f, 10.0f, 10.0f, 0.0f, 0.0f);
	check(pointer_constraint_active(&c), "relative alone is active");
}


static void
testGrabs()
{
	input_grab_state g;
	input_grab_init(&g);

	check(input_grab_acquire_pointer(&g, 1, 42, kPointerGrabConfined),
		"first pointer grab wins");
	check(!input_grab_acquire_pointer(&g, 2, 7, 0),
		"second team rejected");
	check(input_grab_acquire_pointer(&g, 1, 43, kPointerGrabRelative),
		"owner re-acquires with a new target");
	check(g.pointerTargetToken == 43, "re-acquire updated the token");

	check(input_grab_pointer_target(&g) == 43, "grab target routed");
	check(!input_grab_release_pointer(&g, 2), "non-owner cannot release");
	check(input_grab_release_pointer(&g, 1), "owner releases");
	check(input_grab_pointer_target(&g) == -1, "released falls back");

	check(input_grab_acquire_keyboard(&g, 5, 9, 0), "keyboard grab");
	check(input_grab_keyboard_target(&g) == 9, "keyboard routed");
	check(input_grab_keyboard_held_by(&g, 5), "keyboard held by owner");

	input_grab_set_secure_attention_team(&g, 6);
	check(input_grab_pointer_target(&g) == -1
		&& input_grab_keyboard_target(&g) == -1,
		"secure attention suppresses grabs");
	input_grab_set_secure_attention_team(&g, -1);
	check(input_grab_keyboard_target(&g) == 9,
		"grab survives secure attention release");

	input_grab_release_for_team(&g, 5);
	check(!input_grab_keyboard_held_by(&g, 5),
		"team death releases its grabs");

	check(!input_grab_acquire_pointer(&g, -1, 1, 0),
		"invalid team rejected");
	check(!input_grab_acquire_keyboard(&g, 1, -1, 0),
		"invalid token rejected");
}


static void
testRawFlow()
{
	check(xv_axis_normalize(0, 0, 100) == 0.0f, "axis min normalises to 0");
	check(xv_axis_normalize(100, 0, 100) == 1.0f, "axis max normalises to 1");
	check(closeTo(xv_axis_normalize(50, 0, 100), 0.5f),
		"axis mid normalises to half");
	check(xv_axis_normalize(7, 10, 10) == 0.0f, "degenerate range is 0");
	check(xv_axis_normalize(-5, 0, 10) == 0.0f, "below range clamps");
	check(xv_axis_normalize(20, 0, 10) == 1.0f, "above range clamps");

	check(closeTo(xv_wheel_detents(120), 1.0f), "120 hi-res units a detent");
	check(closeTo(xv_wheel_detents(-60), -0.5f), "half detent negative");
	check(closeTo(xv_wheel_detents(1), 1.0f / 120.0f),
		"single hi-res unit is a fraction");

	check(xv_x_keycode(0) == 8, "evdev 0 is X keycode 8");
	check(xv_x_keycode(1) == 9, "evdev 1 is X keycode 9");
	check(xv_x_keycode(247) == 255, "top evdev code stays in range");

	check(strcmp(XVITRUVIAN_INPUT_PORT_NAME,
			"v-os:xvitruvian_input") == 0, "tap port name stable");
	check(kXVTouchDown != kXVTouchMove && kXVTouchMove != kXVTouchUp
		&& kXVTouchUp != kXVTouchCancel, "touch codes distinct");
}


int
main()
{
	testClamp();
	testRelative();
	testGrabs();
	testRawFlow();

	if (sFailures == 0) {
		printf("input_control_host_test: all checks passed\n");
		return 0;
	}
	printf("input_control_host_test: %d failures\n", sFailures);
	return 1;
}
