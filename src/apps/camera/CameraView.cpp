/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#include "CameraView.h"

#include <Autolock.h>
#include <Message.h>
#include <Window.h>
#include <Point.h>
#include <Rect.h>

#include <algorithm>
#include <string.h>


CameraView::CameraView(BRect frame, const char* name)
	:
	BView(frame, name, B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS),
	fLock("camera view"),
	fFrame(NULL),
	fAspect(4.0f / 3.0f),
	fZoom(1.0f),
	fMirrored(false),
	fGrid(false),
	fTapPoint(0, 0),
	fHasTap(false)
{
	SetViewColor(0, 0, 0);
}


CameraView::~CameraView()
{
	delete fFrame;
}


void
CameraView::SetFrame(const uint8* bits, size_t size, uint32 width,
	uint32 height)
{
	if (bits == NULL || width == 0 || height == 0)
		return;

	bool notify = false;
	bool reshaped = false;
	{
		BAutolock _(fLock);
		if (fFrame == NULL
			|| (uint32)fFrame->Bounds().Width() + 1 != width
			|| (uint32)fFrame->Bounds().Height() + 1 != height) {
			delete fFrame;
			fFrame = new BBitmap(BRect(0, 0, width - 1, height - 1),
				B_RGB32);
			if (fFrame == NULL || fFrame->InitCheck() != B_OK) {
				delete fFrame;
				fFrame = NULL;
				return;
			}
			fAspect = (float)width / height;
			reshaped = true;
		}
		const size_t frameSize = (size_t)fFrame->BytesPerRow() * height;
		if (size >= frameSize)
			memcpy(fFrame->Bits(), bits, frameSize);
		notify = true;
	}

	if (reshaped && Window() != NULL)
		Window()->PostMessage(kMsgCameraFrameReshaped);
	if (notify && Window() != NULL)
		Window()->PostMessage(kMsgCameraFrameShown, this);
}


bool
CameraView::HasHeightForWidth()
{
	return true;
}


void
CameraView::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	// Keep the frame's shape so the picture fills the view.
	BAutolock _(fLock);
	const float height = (width + 1) / fAspect - 1;
	if (min != NULL)
		*min = height;
	if (max != NULL)
		*max = height;
	if (preferred != NULL)
		*preferred = height;
}


void
CameraView::SetZoom(float zoom)
{
	if (zoom < 1.0f)
		zoom = 1.0f;
	if (zoom > 8.0f)
		zoom = 8.0f;
	fZoom = zoom;
	if (Window() != NULL)
		Window()->PostMessage(kMsgCameraFrameShown, this);
}


void
CameraView::SetMirrored(bool mirrored)
{
	fMirrored = mirrored;
	if (Window() != NULL)
		Window()->PostMessage(kMsgCameraFrameShown, this);
}


void
CameraView::SetGridVisible(bool visible)
{
	fGrid = visible;
	if (Window() != NULL)
		Invalidate();
}


void
CameraView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCameraFrameShown:
			Invalidate();
			break;

		case B_MOUSE_WHEEL_CHANGED:
		{
			float deltaY = 0;
			if (message->FindFloat("be:wheel_delta_y", &deltaY) == B_OK) {
				SetZoom(fZoom * (deltaY > 0 ? 1.1f : (1.0f / 1.1f)));
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
CameraView::Draw(BRect updateRect)
{
	BAutolock _(fLock);
	const BRect bounds = Bounds();

	if (fFrame == NULL) {
		SetLowColor(0, 0, 0);
		FillRect(bounds, B_SOLID_LOW);
		return;
	}

	const BRect frameBounds = fFrame->Bounds();
	const float frameWidth = frameBounds.Width() + 1;
	const float frameHeight = frameBounds.Height() + 1;

	// Fit the whole frame, then zoom in around the center.
	const float scale = std::min(bounds.Width() / frameWidth,
		bounds.Height() / frameHeight);
	const float scaledWidth = frameWidth * scale * fZoom;
	const float scaledHeight = frameHeight * scale * fZoom;
	const BRect destination((bounds.Width() - scaledWidth) / 2,
		(bounds.Height() - scaledHeight) / 2,
		(bounds.Width() - scaledWidth) / 2 + scaledWidth,
		(bounds.Height() - scaledHeight) / 2 + scaledHeight);

	PushState();
	if (fMirrored) {
		TranslateBy(bounds.Width(), 0);
		ScaleBy(-1, 1);
	}
	DrawBitmap(fFrame, frameBounds, destination);
	PopState();

	if (fGrid) {
		SetHighColor(255, 255, 255, 120);
		StrokeLine(BPoint(bounds.Width() / 3, 0),
			BPoint(bounds.Width() / 3, bounds.Height()));
		StrokeLine(BPoint(2 * bounds.Width() / 3, 0),
			BPoint(2 * bounds.Width() / 3, bounds.Height()));
		StrokeLine(BPoint(0, bounds.Height() / 3),
			BPoint(bounds.Width(), bounds.Height() / 3));
		StrokeLine(BPoint(0, 2 * bounds.Height() / 3),
			BPoint(bounds.Width(), 2 * bounds.Height() / 3));
	}

	if (fHasTap) {
		SetHighColor(255, 255, 0);
		const float x = destination.left
			+ (fTapPoint.x / frameWidth) * scaledWidth;
		const float y = destination.top
			+ (fTapPoint.y / frameHeight) * scaledHeight;
		StrokeRect(BRect(x - 2, y - 2, x + 2, y + 2));
		StrokeRect(BRect(x - 20, y - 20, x + 20, y + 20));
	}
}


void
CameraView::MouseDown(BPoint point)
{
	MakeFocus();

	BPoint framePoint(0, 0);
	bool inside = false;
	{
		BAutolock _(fLock);
		if (fFrame == NULL)
			return;

		const float zoomFactor = fZoom;
		const BRect bounds = Bounds();
		const float frameWidth = fFrame->Bounds().Width() + 1;
		const float frameHeight = fFrame->Bounds().Height() + 1;
		const float scale = bounds.Width() / frameWidth
				> bounds.Height() / frameHeight
			? bounds.Width() / frameWidth : bounds.Height() / frameHeight;
		const float scaledWidth = frameWidth * scale / zoomFactor;
		const float scaledHeight = frameHeight * scale / zoomFactor;
		const float left = (bounds.Width() - scaledWidth) / 2;
		const float top = (bounds.Height() - scaledHeight) / 2;

		framePoint = BPoint((point.x - left) / scaledWidth * frameWidth,
			(point.y - top) / scaledHeight * frameHeight);
		inside = framePoint.x >= 0 && framePoint.y >= 0
			&& framePoint.x < frameWidth && framePoint.y < frameHeight;
		if (inside) {
			fTapPoint = framePoint;
			fHasTap = true;
		}
	}

	if (!inside)
		return;

	Invalidate();

	if (Window() != NULL) {
		BMessage message(kMsgCameraTap);
		message.AddFloat("x", framePoint.x);
		message.AddFloat("y", framePoint.y);
		Window()->PostMessage(&message);
	}
}
