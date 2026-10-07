/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "VideoView.h"

#include <ControlLook.h>
#include <Font.h>
#include <Message.h>
#include <String.h>


VideoView::VideoView(BRect frame, const char* name, uint32 resizeMask)
	:
	BView(frame, name, resizeMask,
		B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fVideoWidth(0),
	fVideoHeight(0)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


VideoView::~VideoView()
{
}


void
VideoView::SetVideoSize(uint32 width, uint32 height)
{
	fVideoWidth = width;
	fVideoHeight = height;
	Invalidate();
}


void
VideoView::GetPreferredSize(float* width, float* height)
{
	if (width != NULL)
		*width = 320;
	if (height != NULL)
		*height = 240;
	if (!HasVideo())
		return;

	// Fit the native video size into the current frame, keeping aspect.
	BRect frame = Bounds();
	float scale = frame.Width() / (float)fVideoWidth;
	float scaleY = frame.Height() / (float)fVideoHeight;
	if (scaleY < scale)
		scale = scaleY;
	if (width != NULL)
		*width = fVideoWidth * scale;
	if (height != NULL)
		*height = fVideoHeight * scale;
}


void
VideoView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	Invalidate();
}


void
VideoView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	FillRect(bounds, B_SOLID_LOW);

	if (!HasVideo()) {
		SetHighColor(0x60, 0x60, 0x60);
		const char* text = "No video";
		float width = StringWidth(text);
		DrawString(text,
			BPoint((bounds.Width() - width) / 2.0f, bounds.Height() / 2.0f));
		return;
	}

	// Letterbox the native frame once W12 delivers decoded video into a
	// BBitmap attached to this view. Until then show the geometry we know.
	BRect videoFrame;
	GetPreferredSize(&videoFrame.right, &videoFrame.bottom);
	videoFrame.OffsetTo(
		(bounds.Width() - videoFrame.Width()) / 2.0f,
		(bounds.Height() - videoFrame.Height()) / 2.0f);

	SetHighColor(0, 0, 0);
	FillRect(videoFrame);

	SetHighColor(0x40, 0x40, 0x40);
	StrokeRect(videoFrame);

	SetHighColor(0xa0, 0xa0, 0xa0);
	BString label;
	label << fVideoWidth << "x" << fVideoHeight;
	float width = StringWidth(label.String());
	DrawString(label.String(),
		BPoint(videoFrame.left + (videoFrame.Width() - width) / 2.0f,
			videoFrame.bottom + 16));
}
