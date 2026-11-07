/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// One thread, no looper: a square moves across a BSurface while poll()
// waits on the surface's event port.

#include <Application.h>
#include <Bitmap.h>
#include <OS.h>
#include <Surface.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void
fill(BBitmap* bitmap, BRect rect, uint32 color)
{
	rect = rect & bitmap->Bounds();
	uint8* bits = (uint8*)bitmap->Bits();
	int32 bpr = bitmap->BytesPerRow();
	for (int32 y = (int32)rect.top; y <= (int32)rect.bottom; y++) {
		uint32* row = (uint32*)(bits + y * bpr);
		for (int32 x = (int32)rect.left; x <= (int32)rect.right; x++)
			row[x] = color;
	}
}


int
main(int argc, char** argv)
{
	BApplication app("application/x-vnd.vos-test-surface");

	bigtime_t duration = argc > 1 ? atoll(argv[1]) * 1000000LL : 3000000;

	BSurface surface(BRect(200, 200, 519, 439), "BSurface test",
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, 0);
	if (surface.InitCheck() != B_OK) {
		fprintf(stderr, "surface: %s\n", strerror(surface.InitCheck()));
		return 1;
	}
	surface.SetEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);

	int fd = port_to_fd(surface.EventPort());
	if (fd < 0) {
		fprintf(stderr, "port_to_fd failed\n");
		return 1;
	}

	BBitmap* buffer = surface.Buffer();
	fill(buffer, buffer->Bounds(), 0xff3060a0);
	surface.Show();
	surface.Present(buffer->Bounds());

	int32 events = 0;
	int32 updates = 0;
	int32 moves = 0;
	BRect square(0, 100, 39, 139);
	bigtime_t start = system_time();
	while (system_time() - start < duration) {
		struct pollfd pfd = { fd, POLLIN, 0 };
		if (poll(&pfd, 1, 16) > 0) {
			BMessage event;
			while (surface.NextEvent(&event, 0) == B_OK) {
				events++;
				if (event.what == _UPDATE_)
					updates++;
				else if (event.what == B_MOUSE_MOVED)
					moves++;
			}
		}

		buffer = surface.Buffer();
		BRect old = square;
		square.OffsetBy(4, 0);
		if (square.left > buffer->Bounds().right)
			square.OffsetTo(0, square.top);
		fill(buffer, old, 0xff3060a0);
		fill(buffer, square, 0xffe0a020);
		surface.Present(old | square);
	}

	printf("test_surface OK events=%" B_PRId32 " updates=%" B_PRId32
		" mouse_moved=%" B_PRId32 "\n", events, updates, moves);
	return 0;
}
