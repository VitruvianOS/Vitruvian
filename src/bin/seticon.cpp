/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// seticon file size < raw.bgra
// Sets the legacy mini (16) or large (32) icon of a file from raw BGRA pixels.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <File.h>
#include <NodeInfo.h>


int
main(int argc, char** argv)
{
	if (argc != 3 || (strcmp(argv[2], "16") != 0 && strcmp(argv[2], "32") != 0)) {
		fprintf(stderr, "usage: seticon file 16|32 < raw.bgra\n");
		return 1;
	}
	int size = atoi(argv[2]);
	BFile file(argv[1], B_READ_ONLY);
	BNodeInfo info(&file);
	if (info.InitCheck() != B_OK) {
		fprintf(stderr, "seticon: cannot open %s\n", argv[1]);
		return 1;
	}

	BBitmap bitmap(BRect(0, 0, size - 1, size - 1), B_BITMAP_NO_SERVER_LINK,
		B_RGBA32);
	size_t length = size * size * 4;
	if (bitmap.InitCheck() != B_OK || (size_t)bitmap.BitsLength() != length)
		return 1;
	uint8* bits = (uint8*)bitmap.Bits();
	size_t got = 0;
	while (got < length) {
		size_t n = fread(bits + got, 1, length - got, stdin);
		if (n == 0)
			break;
		got += n;
	}
	if (got != length) {
		fprintf(stderr, "seticon: short input\n");
		return 1;
	}
	status_t status = info.SetIcon(&bitmap, size == 16 ? B_MINI_ICON : B_LARGE_ICON);
	if (status != B_OK) {
		fprintf(stderr, "seticon: %s\n", strerror(status));
		return 1;
	}
	return 0;
}
