/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "UserPictureView.h"

#include <new>


static const float kFrameWidth = 70.0f;
static const float kFrameHeight = 90.0f;


static float
_Scale()
{
	return be_plain_font->Size() / 12.0f;
}


UserPictureView::UserPictureView()
	:
	BView("picture", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_NAVIGABLE),
	fPicture(NULL)
{
	const BSize size(kFrameWidth * _Scale(), kFrameHeight * _Scale());
	SetExplicitMinSize(size);
	SetExplicitMaxSize(size);
}


UserPictureView::~UserPictureView()
{
	delete fPicture;
}


void
UserPictureView::SetPicture(BBitmap* picture)
{
	delete fPicture;
	fPicture = picture;
	Invalidate();
}


void
UserPictureView::Clear()
{
	SetPicture(NULL);
}


void
UserPictureView::Draw(BRect updateRect)
{
	BRect frame(Bounds());
	frame.InsetBy(1.0f, 1.0f);

	SetHighUIColor(B_SHINE_COLOR);
	StrokeRect(Bounds());

	if (fPicture == NULL || !fPicture->IsValid()) {
		SetHighUIColor(B_CONTROL_TEXT_COLOR);
		// Person silhouette placeholder, same spirit as People's default.
		const float scale = _Scale();
		const float center = frame.left + frame.Width() / 2.0f;
		BRect head(center - 8.0f * scale, frame.top + 14.0f * scale,
			center + 8.0f * scale, frame.top + 30.0f * scale);
		FillEllipse(head);
		BRect body(center - 14.0f * scale, frame.top + 34.0f * scale,
			center + 14.0f * scale, frame.bottom - 4.0f * scale);
		FillRect(body);
		return;
	}

	BRect src = fPicture->Bounds();
	float frameAspect = frame.Width() / frame.Height();
	float srcAspect = src.Width() / src.Height();
	BRect dest = frame;
	if (srcAspect > frameAspect) {
		// Letterbox horizontally.
		float h = frame.Width() / srcAspect;
		dest.top += (frame.Height() - h) / 2.0f;
		dest.bottom = dest.top + h;
	} else {
		float w = frame.Height() * srcAspect;
		dest.left += (frame.Width() - w) / 2.0f;
		dest.right = dest.left + w;
	}

	SetDrawingMode(B_OP_COPY);
	DrawBitmap(fPicture, src, dest, B_FILTER_BITMAP_BILINEAR);
}
