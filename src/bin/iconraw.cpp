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

#include <ByteOrder.h>
#include <File.h>
#include <DataIO.h>
#include <TranslationDefs.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>


// The pixel at (x, y) of a translated bitmap as b, g, r, a. False when the
// pixel format is not one the legacy icons can come from.
static bool
read_pixel(const TranslatorBitmap& header, const uint8* data, int x, int y,
	uint8* out)
{
	const uint8* row = data + (size_t)y * header.rowBytes;
	switch (header.colors) {
		case B_RGBA32:
			memcpy(out, row + x * 4, 4);
			return true;
		case B_RGB32:
			memcpy(out, row + x * 4, 3);
			out[3] = 255;
			return true;
		case B_RGB24:
			memcpy(out, row + x * 3, 3);
			out[3] = 255;
			return true;
		case B_GRAY8:
			out[0] = out[1] = out[2] = row[x];
			out[3] = 255;
			return true;
		default:
			return false;
	}
}


int
main(int argc, char** argv)
{
	int size = argc == 3 ? atoi(argv[2]) : 0;
	if (size < 1 || size > 512) {
		fprintf(stderr, "usage: iconraw file size > raw.bgra\n");
		return 1;
	}

	// The translators write the bitmap as a header and raw pixels into any
	// stream: no application or app_server is needed.
	BFile file(argv[1], B_READ_ONLY);
	BTranslatorRoster* roster = BTranslatorRoster::Default();
	BMallocIO translated;
	if (file.InitCheck() != B_OK || roster == NULL
		|| roster->Translate(&file, NULL, NULL, &translated,
			B_TRANSLATOR_BITMAP) != B_OK
		|| translated.BufferLength() < sizeof(TranslatorBitmap)) {
		fprintf(stderr, "iconraw: cannot decode %s\n", argv[1]);
		return 1;
	}

	TranslatorBitmap header;
	memcpy(&header, translated.Buffer(), sizeof(header));
	header.magic = B_BENDIAN_TO_HOST_INT32(header.magic);
	header.bounds.left = B_BENDIAN_TO_HOST_FLOAT(header.bounds.left);
	header.bounds.top = B_BENDIAN_TO_HOST_FLOAT(header.bounds.top);
	header.bounds.right = B_BENDIAN_TO_HOST_FLOAT(header.bounds.right);
	header.bounds.bottom = B_BENDIAN_TO_HOST_FLOAT(header.bounds.bottom);
	header.rowBytes = B_BENDIAN_TO_HOST_INT32(header.rowBytes);
	header.colors = (color_space)B_BENDIAN_TO_HOST_INT32(header.colors);
	header.dataSize = B_BENDIAN_TO_HOST_INT32(header.dataSize);

	int sourceWidth = header.bounds.IntegerWidth() + 1;
	int sourceHeight = header.bounds.IntegerHeight() + 1;
	const uint8* bits = (const uint8*)translated.Buffer() + sizeof(header);
	uint8 probe[4];
	if (header.magic != B_TRANSLATOR_BITMAP || sourceWidth < 1
		|| sourceHeight < 1
		|| translated.BufferLength() < sizeof(header) + header.dataSize
		|| (size_t)sourceHeight * header.rowBytes > header.dataSize
		|| !read_pixel(header, bits, 0, 0, probe)) {
		fprintf(stderr, "iconraw: unsupported image %s\n", argv[1]);
		return 1;
	}

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
				for (int sx = x0; sx < x1 && sx < sourceWidth; sx++) {
					uint8 p[4];
					read_pixel(header, bits, sx, sy, p);
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
	return written == length ? 0 : 1;
}
