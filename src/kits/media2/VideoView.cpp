/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <media2/VideoView.h>

#include <ControlLook.h>
#include <Font.h>
#include <Message.h>
#include <String.h>
#include <string.h>

#include <media2/MediaPlayer.h>

#include "VideoFrameRing.h"


// Frames lagging the presented audio sample by more than this are dropped.
static const bigtime_t kFrameDropThreshold = 60000;


VideoView::VideoView(BRect frame, const char* name, uint32 resizeMask)
	:
	BView(frame, name, resizeMask,
		B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fVideoWidth(0),
	fVideoHeight(0),
	fBitmap(NULL),
	fRing(NULL),
	fSlot(-1),
	fSequence(-1),
	fGeneration(-1),
	fHeld(false)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


VideoView::~VideoView()
{
	_ClearFrame();
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

	void* bitmapPtr = NULL;
	void* ringPtr = NULL;
	int32 slot = -1;
	int64 sequence = -1;
	int64 generation = -1;
	if (message->FindPointer("bitmap", &bitmapPtr) != B_OK
			|| message->FindPointer("ring", &ringPtr) != B_OK
			|| message->FindInt32("slot", &slot) != B_OK
			|| message->FindInt64("sequence", &sequence) != B_OK
			|| message->FindInt64("generation", &generation) != B_OK)
		return;

	VideoFrameRing* ring = static_cast<VideoFrameRing*>(ringPtr);
	BBitmap* bitmap = static_cast<BBitmap*>(bitmapPtr);
	if (ring == NULL || bitmap == NULL)
		return;

	// The pointers are team local, the player dispatches in place.
	if (generation != ring->Generation() || !ring->ClaimSlot(slot, sequence))
		return;

	// A newer frame replaces an undrawn one.
	_DropRing();

	// Without an audio track there is nothing to compare against, so
	// present every frame in order.
	bigtime_t startTime = -1;
	bigtime_t audioTime = -1;
	message->FindInt64("start_time", &startTime);
	message->FindInt64("audio_time", &audioTime);
	if (audioTime >= 0 && startTime >= 0
			&& (audioTime - startTime) > kFrameDropThreshold) {
		ring->ReleaseSlot(slot);
		return;
	}

	ring->AddRef();
	fBitmap = bitmap;
	fRing = ring;
	fSlot = slot;
	fSequence = sequence;
	fGeneration = generation;
	fHeld = true;
	fVideoWidth = (uint32)(bitmap->Bounds().Width() + 1.0f);
	fVideoHeight = (uint32)(bitmap->Bounds().Height() + 1.0f);
	Invalidate();
}


void
VideoView::_ReleaseHeld()
{
	VideoFrameRing* ring = static_cast<VideoFrameRing*>(fRing);
	if (ring != NULL && fHeld)
		ring->ReleaseSlot(fSlot);
	fHeld = false;
}


// Drops the ring reference taken on claim, after giving the slot back.
void
VideoView::_DropRing()
{
	_ReleaseHeld();
	if (fRing != NULL)
		static_cast<VideoFrameRing*>(fRing)->Release();
	fRing = NULL;
}


void
VideoView::_ClearFrame()
{
	_DropRing();

	fBitmap = NULL;
	fSlot = -1;
	fSequence = -1;
	fGeneration = -1;
	fVideoWidth = 0;
	fVideoHeight = 0;
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

	VideoFrameRing* ring = static_cast<VideoFrameRing*>(fRing);
	if (ring == NULL || ring->Generation() != fGeneration
			|| !ring->SlotUnchanged(fSlot, fSequence)) {
		_ClearFrame();
		FillRect(bounds, B_SOLID_LOW);
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

	// The pixels are with the server, the decoder may reuse the slot.
	_ReleaseHeld();
}
