/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// iconraw file size > raw.bgra
// Decodes an image with the installed translators and writes it as size x size
// raw BGRA pixels (aspect kept, centered, transparent padding): the input of
// seticon and setapptype.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Bitmap.h>
#include <TranslationUtils.h>


int
main(int argc, char** argv)
{
	int size = argc == 3 ? atoi(argv[2]) : 0;
	if (size < 1 || size > 512) {
		fprintf(stderr, "usage: iconraw file size > raw.bgra\n");
		return 1;
	}

	BBitmap* decoded = BTranslationUtils::GetBitmapFile(argv[1]);
	if (decoded == NULL || decoded->InitCheck() != B_OK) {
		fprintf(stderr, "iconraw: cannot decode %s\n", argv[1]);
		delete decoded;
		return 1;
	}

	BBitmap* source = decoded;
	if (decoded->ColorSpace() != B_RGBA32) {
		source = new BBitmap(decoded->Bounds(), B_BITMAP_NO_SERVER_LINK,
			B_RGBA32);
		if (source->InitCheck() != B_OK
			|| source->ImportBits(decoded) != B_OK) {
			fprintf(stderr, "iconraw: unsupported pixel format\n");
			return 1;
		}
		delete decoded;
	}

	int sourceWidth = source->Bounds().IntegerWidth() + 1;
	int sourceHeight = source->Bounds().IntegerHeight() + 1;
	int32 stride = source->BytesPerRow();
	const uint8* bits = (const uint8*)source->Bits();

	// The scaled image fits the square, the rest stays transparent.
	int width = size;
	int height = size;
	if (sourceWidth > sourceHeight)
		height = (sourceHeight * size + sourceWidth / 2) / sourceWidth;
	else
		width = (sourceWidth * size + sourceHeight / 2) / sourceHeight;
	if (width < 1)
		width = 1;
	if (height < 1)
		height = 1;
	int left = (size - width) / 2;
	int top = (size - height) / 2;

	uint8* out = (uint8*)calloc(size * size, 4);
	if (out == NULL)
		return 1;

	// Every destination pixel averages the source pixels it covers, color
	// weighted by alpha.
	for (int y = 0; y < height; y++) {
		int y0 = y * sourceHeight / height;
		int y1 = (y + 1) * sourceHeight / height;
		if (y1 <= y0)
			y1 = y0 + 1;
		for (int x = 0; x < width; x++) {
			int x0 = x * sourceWidth / width;
			int x1 = (x + 1) * sourceWidth / width;
			if (x1 <= x0)
				x1 = x0 + 1;

			uint64 b = 0, g = 0, r = 0, a = 0;
			int count = 0;
			for (int sy = y0; sy < y1 && sy < sourceHeight; sy++) {
				const uint8* row = bits + sy * stride;
				for (int sx = x0; sx < x1 && sx < sourceWidth; sx++) {
					const uint8* p = row + sx * 4;
					b += (uint64)p[0] * p[3];
					g += (uint64)p[1] * p[3];
					r += (uint64)p[2] * p[3];
					a += p[3];
					count++;
				}
			}
			if (count == 0 || a == 0)
				continue;
			uint8* d = out + ((top + y) * size + left + x) * 4;
			d[0] = b / a;
			d[1] = g / a;
			d[2] = r / a;
			d[3] = a / count;
		}
	}

	size_t length = (size_t)size * size * 4;
	size_t written = fwrite(out, 1, length, stdout);
	free(out);
	delete source;
	return written == length ? 0 : 1;
}
