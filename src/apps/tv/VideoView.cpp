/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "VideoView.h"

#include <String.h>


VideoView::VideoView(BRect frame, const char* name, uint32 resizeMask,
	uint32 flags)
	:
	BView(frame, name, resizeMask, flags),
	fCapturing(false),
	fFrame(NULL),
	fWidth(0),
	fHeight(0)
{
	SetViewColor(0, 0, 0);
}


VideoView::~VideoView()
{
	delete fFrame;
}


void
VideoView::AttachedToWindow()
{
	SetViewColor(0, 0, 0);
}


void
VideoView::SetCapturing(bool capturing)
{
	fCapturing = capturing;
	Invalidate();
}


void
VideoView::DrawFrame(const void* data, size_t size, uint32 width, uint32 height)
{
	if (data == NULL || width == 0 || height == 0)
		return;

	const uint32 bytes = width * height * 4;
	if (size < bytes)
		return;

	if (fFrame == NULL || fWidth != width || fHeight != height) {
		delete fFrame;
		fFrame = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGBA32);
		fWidth = width;
		fHeight = height;
		if (fFrame == NULL || fFrame->InitCheck() != B_OK) {
			delete fFrame;
			fFrame = NULL;
			return;
		}
	}

	fFrame->SetBits(data, bytes, 0, B_RGBA32);
	Invalidate();
}


void
VideoView::Draw(BRect updateRect)
{
	FillRect(updateRect, B_SOLID_LOW);

	if (fFrame != NULL && fWidth > 0 && fHeight > 0) {
		DrawBitmap(fFrame, fFrame->Bounds(), Bounds(),
			B_FILTER_BITMAP_BILINEAR);
		return;
	}

	SetHighColor(fCapturing ? 40 : 20, fCapturing ? 40 : 20,
		fCapturing ? 50 : 20);
	FillRect(updateRect);

	if (!fCapturing) {
		SetHighColor(180, 180, 180);
		BString text("No video frames");
		DrawString(text.String(), BPoint(10, 20));
		SetHighColor(120, 120, 120);
		text.SetTo("media2 has no video capture path yet");
		DrawString(text.String(), BPoint(10, 36));
	}
}
