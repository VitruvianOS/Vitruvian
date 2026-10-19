/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host check of the panel orientation rules: the user's rotation is relative
// to the mounting, and every transform composes the same way.

#include <PanelOrientationTransform.h>

#include <math.h>
#include <stdio.h>


static int sFailures = 0;


static void
check(bool ok, const char* what, int a, int b)
{
	if (!ok) {
		printf("FAIL %s (%d, %d)\n", what, a, b);
		sFailures++;
	}
}


int
main()
{
	const int32 kAll[] = {
		B_PANEL_ORIENTATION_NORMAL, B_PANEL_ORIENTATION_UPSIDE_DOWN,
		B_PANEL_ORIENTATION_LEFT_UP, B_PANEL_ORIENTATION_RIGHT_UP
	};

	// Two quarter turns are a half turn; opposite quarter turns cancel.
	check(panel_compose_orientation(B_PANEL_ORIENTATION_LEFT_UP,
			B_PANEL_ORIENTATION_LEFT_UP) == B_PANEL_ORIENTATION_UPSIDE_DOWN,
		"left+left", 0, 0);
	check(panel_compose_orientation(B_PANEL_ORIENTATION_LEFT_UP,
			B_PANEL_ORIENTATION_RIGHT_UP) == B_PANEL_ORIENTATION_NORMAL,
		"left+right", 0, 0);

	for (int32 m : kAll) {
		for (int32 u : kAll) {
			int32 absolute = panel_compose_orientation(m, u);
			check(panel_relative_orientation(absolute, m) == u,
				"relative inverts compose", m, u);

			// The scanout transform must equal mounting then user rotation.
			for (int i = 0; i <= 4; i++) {
				for (int j = 0; j <= 4; j++) {
					float p = i / 4.0f, q = j / 4.0f;
					float mu, mv, au, av, cu, cv;
					rotate_panel_point(m, p, q, mu, mv);
					rotate_panel_point(u, mu, mv, au, av);
					rotate_panel_point(absolute, p, q, cu, cv);
					check(fabsf(au - cu) < 1e-6f && fabsf(av - cv) < 1e-6f,
						"compose matches point mapping", m, u);
				}
			}
		}
	}

	// rotate_point() (logical pixel to physical pixel) inverts
	// rotate_panel_point() (physical to logical) on pixel centres.
	const int32 kW = 6, kH = 4;
	for (int32 o : kAll) {
		bool swap = o == B_PANEL_ORIENTATION_LEFT_UP
			|| o == B_PANEL_ORIENTATION_RIGHT_UP;
		int32 physW = swap ? kH : kW, physH = swap ? kW : kH;
		for (int32 y = 0; y < kH; y++) {
			for (int32 x = 0; x < kW; x++) {
				int32 px, py;
				rotate_point(o, x, y, kW, kH, px, py);
				float lu, lv;
				rotate_panel_point(o, (px + 0.5f) / physW,
					(py + 0.5f) / physH, lu, lv);
				check((int32)floorf(lu * kW) == x
						&& (int32)floorf(lv * kH) == y,
					"rotate_point inverts rotate_panel_point", o, x * 10 + y);
			}
		}
	}

	// A sideways panel (800x1280 mounted right side up): the old absolute
	// "right side up" the user had to pick becomes "normal".
	check(panel_relative_orientation(B_PANEL_ORIENTATION_RIGHT_UP,
			B_PANEL_ORIENTATION_RIGHT_UP) == B_PANEL_ORIENTATION_NORMAL,
		"sideways panel converts to normal", 0, 0);

	printf(sFailures == 0 ? "test_panel_orientation: all checks passed\n"
		: "test_panel_orientation: %d failures\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
