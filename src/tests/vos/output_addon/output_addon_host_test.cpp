/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Host checks of the output add-on transport core: rect math, the
// generation handoff, frame encode/decode and the shared area layout.
// Runs on a plain Linux host; nothing here needs /dev/nexus.

#include <OutputAddonProtocol.h>

#include <stdio.h>
#include <stdlib.h>
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


static void
testRectMath()
{
	output_addon_rect a;
	output_addon_rect_set(&a, 10, 10, 20, 20);
	check(!output_addon_rect_empty(&a), "rect not empty");

	output_addon_rect empty;
	output_addon_rect_set(&empty, 0, 0, 0, 0);
	check(output_addon_rect_empty(&empty), "zero rect empty");

	output_addon_rect b;
	output_addon_rect_set(&b, 15, 15, 30, 30);
	output_addon_rect_union(&a, &b);
	check(a.left == 10 && a.top == 10 && a.right == 30 && a.bottom == 30,
		"union grows");

	output_addon_rect_set(&a, 0, 0, 10, 10);
	output_addon_rect_set(&b, 5, 5, 20, 20);
	output_addon_rect out;
	check(output_addon_rect_intersect(&a, &b, &out), "overlap intersects");
	check(out.left == 5 && out.top == 5 && out.right == 10
		&& out.bottom == 10, "intersect box");

	output_addon_rect_set(&b, 30, 30, 40, 40);
	check(!output_addon_rect_intersect(&a, &b, &out), "disjoint rejected");
}


static void
testHandoff()
{
	volatile uint32 front = 0;
	output_addon_publish(&front, 7, 1);

	uint32 generation = 0;
	uint32 index = 0;
	output_addon_unpack(front, &generation, &index);
	check(generation == 7 && index == 1, "publish/unpack roundtrip");
	check(output_addon_pack(7, 1) == front, "pack stable");
}


static void
testAreaLayout()
{
	int32 areaSize = output_addon_fb_area_size(2560, 480, 2);
	check(areaSize == (int32)sizeof(output_addon_fb_header) + 2 * 2560 * 480,
		"area size covers both slots");

	uint8* area = (uint8*)malloc((size_t)areaSize);
	memset(area, 0xab, (size_t)areaSize);

	uint8* slot1 = (uint8*)output_addon_fb_buffer(area, 2560, 480, 1);
	check(slot1 == area + sizeof(output_addon_fb_header) + 2560 * 480,
		"slot 1 offset");

	// Copy a damaged rect between strides of different width.
	uint8* src = (uint8*)malloc(640 * 480 * 4);
	memset(src, 0x11, 640 * 480 * 4);
	output_addon_rect rect;
	output_addon_rect_set(&rect, 10, 20, 30, 40);
	output_addon_copy_rect(slot1, 2560, src, 640 * 4, &rect, 1);

	uint8* pixel = slot1 + 20 * 2560 + 10 * 4;
	check(pixel[0] == 0x11 && pixel[3] == 0x11, "copied pixel lands");
	uint8* outside = slot1 + 40 * 2560 + 30 * 4;
	check(outside[0] == 0xab, "outside rect untouched");

	free(src);
	free(area);
}


static void
testFrameCodec()
{
	output_addon_rect rects[2];
	output_addon_rect_set(&rects[0], 0, 0, 64, 64);
	output_addon_rect_set(&rects[1], 100, 100, 160, 180);

	output_addon_frame frame;
	frame.screen = 3;
	frame.generation = 42;
	frame.rect_count = 2;

	int32 size = output_addon_frame_encode(&frame, rects, NULL, 0);
	check(size == (int32)(sizeof(frame) + 2 * sizeof(output_addon_rect)),
		"encode sizes");

	uint8* buffer = (uint8*)malloc((size_t)size);
	check(output_addon_frame_encode(&frame, rects, buffer, size) == size,
		"encode fits");

	output_addon_frame decoded;
	const output_addon_rect* outRects = NULL;
	check(output_addon_frame_decode(buffer, size, &decoded, &outRects) == size,
		"decode reads");
	check(decoded.screen == 3 && decoded.generation == 42
		&& decoded.rect_count == 2, "decode header");
	check(outRects != NULL && memcmp(outRects, rects, sizeof(rects)) == 0,
		"decode rects");

	// Truncated buffers must be rejected, never read past the end.
	check(output_addon_frame_decode(buffer, size - 1, &decoded, &outRects)
		== -1, "decode truncation rejected");

	output_addon_frame hostile;
	hostile.rect_count = kOutputAddonMaxRects + 1;
	check(output_addon_frame_encode(&hostile, rects, NULL, 0) == -1,
		"encode rejects hostile count");

	free(buffer);
}


int
main()
{
	testRectMath();
	testHandoff();
	testAreaLayout();
	testFrameCodec();

	if (sFailures == 0) {
		printf("output_addon_host_test: all checks passed\n");
		return 0;
	}

	printf("output_addon_host_test: %d failures\n", sFailures);
	return 1;
}
