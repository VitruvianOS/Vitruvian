/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Host-side check of fbdev pixel-format and stride handling.
 */

#include "FBDevFormat.h"

#include <stdio.h>


static int sFailures;


static void
check(bool ok, const char* label)
{
	printf("%-52s %s\n", label, ok ? "OK" : "FAIL");
	if (!ok)
		sFailures++;
}


static void
test_format_mapping()
{
	color_space space = (color_space)0;

	check(fbdev_color_space(32, &space) && space == B_RGB32,
		"32 bpp maps to B_RGB32");
	check(fbdev_color_space(24, &space) && space == B_RGB24,
		"24 bpp maps to B_RGB24");
	check(!fbdev_color_space(16, &space),
		"16 bpp is rejected");
	check(!fbdev_color_space(8, &space),
		"8 bpp is rejected");
}


static void
test_stride()
{
	// Normal stride equals width * bpp.
	check(fbdev_bytes_per_row(32, 1024, 4096) == 4096,
		"32 bpp 1024 wide line_length 4096");
	check(fbdev_bytes_per_row(24, 1024, 3072) == 3072,
		"24 bpp 1024 wide line_length 3072");

	// Odd line_length must be used as-is, not recomputed from width*bpp.
	check(fbdev_bytes_per_row(32, 1023, 4096) == 4096,
		"32 bpp odd width uses line_length 4096");
	check(fbdev_bytes_per_row(24, 1023, 3072) == 3072,
		"24 bpp odd width uses line_length 3072");
	check(fbdev_bytes_per_row(32, 100, 401) == 401,
		"32 bpp padded line_length 401 (odd)");
	check(fbdev_bytes_per_row(24, 100, 301) == 301,
		"24 bpp padded line_length 301 (odd)");
	check(fbdev_bytes_per_row(32, 800, 3204) == 3204,
		"32 bpp line_length 3204 (800*4+4)");
	check(fbdev_bytes_per_row(24, 800, 2404) == 2404,
		"24 bpp line_length 2404 (800*3+4)");

	// Zero line_length falls back to width*bpp.
	check(fbdev_bytes_per_row(32, 640, 0) == 2560,
		"32 bpp line_length 0 falls back to width*4");
	check(fbdev_bytes_per_row(24, 640, 0) == 1920,
		"24 bpp line_length 0 falls back to width*3");
}


int
main()
{
	printf("fbdev format/stride host test\n");
	test_format_mapping();
	test_stride();

	if (sFailures != 0) {
		printf("%d failure(s)\n", sFailures);
		return 1;
	}
	printf("all checks passed\n");
	return 0;
}
