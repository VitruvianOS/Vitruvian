/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "VideoView.h"

#include <ControlLook.h>
#include <Font.h>
#include <Message.h>
#include <String.h>
#include <string.h>

#include <media2/MediaPlayer.h>


// Frames lagging the presented audio sample by more than this are dropped.
static const bigtime_t kFrameDropThreshold = 60000;


VideoView::VideoView(BRect frame, const char* name, uint32 resizeMask)
	:
	BView(frame, name, resizeMask,
		B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fVideoWidth(0),
	fVideoHeight(0),
	fBitmap(NULL)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


VideoView::~VideoView()
{
	delete fBitmap;
}


void
VideoView::SetVideoSize(uint32 width, uint32 height)
{
	fVideoWidth = width;
	fVideoHeight = height;
	if (width == 0 || height == 0)
		_ClearFrame();
	Invalidate();
}


void
VideoView::GetPreferredSize(float* width, float* height)
{
	if (!HasVideo()) {
		if (width != NULL)
			*width = 320;
		if (height != NULL)
			*height = 240;
		return;
	}
	if (width != NULL)
		*width = fVideoWidth;
	if (height != NULL)
		*height = fVideoHeight;
}


void
VideoView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	Invalidate();
}


void
VideoView::MessageReceived(BMessage* message)
{
	if (message->what != BMediaPlayer::B_PLAYER_VIDEO_FRAME) {
		BView::MessageReceived(message);
		return;
	}

	const void* data = NULL;
	ssize_t size = 0;
	int32 width = 0;
	int32 height = 0;
	int32 stride = 0;
	bigtime_t startTime = 0;
	bigtime_t audioTime = -1;
	if (message->FindData("frame", B_RAW_TYPE, &data, &size) != B_OK)
		return;
	message->FindInt32("width", &width);
	message->FindInt32("height", &height);
	message->FindInt32("stride", &stride);
	message->FindInt64("start_time", &startTime);
	message->FindInt64("audio_time", &audioTime);
	if (width <= 0 || height <= 0 || stride < width * 4
			|| size < (ssize_t)stride * height) {
		return;
	}

	// Without an audio track there is nothing to compare against, so
	// present every frame in order.
	if (audioTime >= 0 && startTime >= 0
			&& (audioTime - startTime) > kFrameDropThreshold) {
		return;
	}

	_PresentFrame(data, (uint32)width, (uint32)height, (uint32)stride);
}


void
VideoView::_PresentFrame(const void* data, uint32 width, uint32 height,
	uint32 stride)
{
	if (fBitmap == NULL
			|| fBitmap->Bounds().IntegerWidth() + 1 != (int32)width
			|| fBitmap->Bounds().IntegerHeight() + 1 != (int32)height) {
		delete fBitmap;
		fBitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
		if (fBitmap == NULL || fBitmap->InitCheck() != B_OK) {
			delete fBitmap;
			fBitmap = NULL;
			return;
		}
	}

	uint8* bits = (uint8*)fBitmap->Bits();
	const int32 bytesPerRow = fBitmap->BytesPerRow();
	const uint8* src = (const uint8*)data;
	const uint32 rowBytes = width * 4;
	for (uint32 y = 0; y < height; y++)
		memcpy(bits + (size_t)y * bytesPerRow, src + (size_t)y * stride,
			rowBytes);

	fVideoWidth = width;
	fVideoHeight = height;
	Invalidate();
}


void
VideoView::_ClearFrame()
{
	delete fBitmap;
	fBitmap = NULL;
}


void
VideoView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	FillRect(bounds, B_SOLID_LOW);

	if (fBitmap == NULL || !HasVideo()) {
		SetHighColor(0x60, 0x60, 0x60);
		const char* text = "No video";
		float width = StringWidth(text);
		DrawString(text,
			BPoint((bounds.Width() - width) / 2.0f, bounds.Height() / 2.0f));
		return;
	}

	// Letterbox the decoded frame into the current bounds.
	float scale = bounds.Width() / (float)fVideoWidth;
	float scaleY = bounds.Height() / (float)fVideoHeight;
	if (scaleY < scale)
		scale = scaleY;
	float targetW = fVideoWidth * scale;
	float targetH = fVideoHeight * scale;
	BRect videoFrame(0, 0, targetW, targetH);
	videoFrame.OffsetTo(
		(bounds.Width() - targetW) / 2.0f,
		(bounds.Height() - targetH) / 2.0f);

	DrawBitmap(fBitmap, fBitmap->Bounds(), videoFrame);
}
