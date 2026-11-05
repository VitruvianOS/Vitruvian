/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// Transport core for the output add-on protocol. Shared by app_server,
// the client library and the host checks; keep it free of libbe objects.

#include "OutputAddonProtocol.h"

#include <string.h>


void
output_addon_rect_set(output_addon_rect* rect, int32 left, int32 top,
	int32 right, int32 bottom)
{
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
}


bool
output_addon_rect_empty(const output_addon_rect* rect)
{
	return rect->right <= rect->left || rect->bottom <= rect->top;
}


void
output_addon_rect_union(output_addon_rect* ioRect, const output_addon_rect* add)
{
	if (output_addon_rect_empty(add))
		return;
	if (output_addon_rect_empty(ioRect)) {
		*ioRect = *add;
		return;
	}

	if (add->left < ioRect->left)
		ioRect->left = add->left;
	if (add->top < ioRect->top)
		ioRect->top = add->top;
	if (add->right > ioRect->right)
		ioRect->right = add->right;
	if (add->bottom > ioRect->bottom)
		ioRect->bottom = add->bottom;
}


bool
output_addon_rect_intersect(const output_addon_rect* a,
	const output_addon_rect* b, output_addon_rect* out)
{
	out->left = a->left > b->left ? a->left : b->left;
	out->top = a->top > b->top ? a->top : b->top;
	out->right = a->right < b->right ? a->right : b->right;
	out->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
	return !output_addon_rect_empty(out);
}


uint32
output_addon_pack(uint32 generation, uint32 index)
{
	return (generation << 16) | (index & 0xffff);
}


void
output_addon_unpack(uint32 front, uint32* generation, uint32* index)
{
	*generation = front >> 16;
	*index = front & 0xffff;
}


void
output_addon_publish(volatile uint32* front, uint32 generation, uint32 index)
{
	*front = output_addon_pack(generation, index);
}


int32
output_addon_fb_header_size()
{
	return sizeof(output_addon_fb_header);
}


int32
output_addon_fb_area_size(int32 bytesPerRow, int32 height, int32 bufferCount)
{
	if (bytesPerRow <= 0 || height <= 0 || bufferCount <= 0)
		return -1;

	return (int32)sizeof(output_addon_fb_header)
		+ bufferCount * bytesPerRow * height;
}


void*
output_addon_fb_buffer(void* base, int32 bytesPerRow, int32 height, int32 index)
{
	uint8* start = (uint8*)base + sizeof(output_addon_fb_header);
	return start + (int64)index * bytesPerRow * height;
}


int32
output_addon_frame_encode(const output_addon_frame* frame,
	const output_addon_rect* rects, void* outBuffer, int32 outSize)
{
	int32 wanted = (int32)sizeof(output_addon_frame)
		+ frame->rect_count * (int32)sizeof(output_addon_rect);
	if (frame->rect_count < 0 || frame->rect_count > kOutputAddonMaxRects)
		return -1;
	if (outBuffer == NULL)
		return wanted;
	if (outSize < wanted)
		return -1;

	memcpy(outBuffer, frame, sizeof(output_addon_frame));
	if (frame->rect_count > 0) {
		memcpy((uint8*)outBuffer + sizeof(output_addon_frame), rects,
			frame->rect_count * sizeof(output_addon_rect));
	}
	return wanted;
}


int32
output_addon_frame_decode(const void* buffer, int32 size,
	output_addon_frame* frame, const output_addon_rect** rects)
{
	if (buffer == NULL || size < (int32)sizeof(output_addon_frame))
		return -1;

	memcpy(frame, buffer, sizeof(output_addon_frame));
	if (frame->rect_count < 0 || frame->rect_count > kOutputAddonMaxRects)
		return -1;

	int32 wanted = (int32)sizeof(output_addon_frame)
		+ frame->rect_count * (int32)sizeof(output_addon_rect);
	if (size < wanted)
		return -1;

	if (frame->rect_count > 0) {
		*rects = (const output_addon_rect*)((const uint8*)buffer
			+ sizeof(output_addon_frame));
	} else
		*rects = NULL;

	return wanted;
}


void
output_addon_copy_rect(uint8* dstBase, int32 dstStride, const uint8* srcBase,
	int32 srcStride, const output_addon_rect* rects, int32 rectCount)
{
	for (int32 i = 0; i < rectCount; i++) {
		const output_addon_rect* rect = &rects[i];
		int32 width = rect->right - rect->left;
		int32 height = rect->bottom - rect->top;
		if (width <= 0 || height <= 0)
			continue;

		for (int32 y = 0; y < height; y++) {
			const uint8* src = srcBase + (int64)(rect->top + y) * srcStride
				+ (int64)rect->left * 4;
			uint8* dst = dstBase + (int64)(rect->top + y) * dstStride
				+ (int64)rect->left * 4;
			memcpy(dst, src, (size_t)width * 4);
		}
	}
}
