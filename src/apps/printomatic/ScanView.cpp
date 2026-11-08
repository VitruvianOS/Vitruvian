/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ScanView.h"

#include <Bitmap.h>
#include <Message.h>
#include <Rect.h>


ScanView::ScanView()
	:
	BView("scanView", B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
	fBitmap(NULL),
	fHasSelection(false),
	fDragging(false)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


void
ScanView::SetBitmap(BBitmap* bitmap)
{
	if (fBitmap != bitmap) {
		delete fBitmap;
		fBitmap = bitmap;
	}

	fHasSelection = false;
	fDragging = false;
	Invalidate();
}


BRect
ScanView::Selection() const
{
	return fHasSelection ? fSelection : BRect();
}


void
ScanView::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	SetLowColor(255, 255, 255);
	FillRect(bounds, B_SOLID_LOW);

	if (fBitmap == NULL || fBitmap->InitCheck() != B_OK) {
		const char* hint = "No preview yet";
		font_height height;
		GetFontHeight(&height);
		float width = StringWidth(hint);
		DrawString(hint, BPoint((bounds.Width() - width) / 2,
			(bounds.Height() + height.ascent) / 2));
		return;
	}

	BRect src = fBitmap->Bounds();
	// Fit the bitmap inside the view, preserving the aspect ratio.
	float scale = bounds.Width() / src.Width();
	if (bounds.Height() / src.Height() < scale)
		scale = bounds.Height() / src.Height();

	BRect dst(0, 0, src.Width() * scale, src.Height() * scale);
	dst.OffsetTo((bounds.Width() - dst.Width()) / 2,
		(bounds.Height() - dst.Height()) / 2);

	DrawBitmap(fBitmap, src, dst);

	if (fHasSelection) {
		BRect sel(fSelection.left * scale + dst.left,
			fSelection.top * scale + dst.top,
			fSelection.right * scale + dst.left,
			fSelection.bottom * scale + dst.top);
		sel.InsetBy(-1, -1);
		SetHighColor(0, 90, 200);
		StrokeRect(sel);
	}
}


void
ScanView::MouseDown(BPoint where)
{
	if (fBitmap == NULL)
		return;

	fDragging = true;
	fAnchor = _ViewToBitmap(where);
	fSelection = BRect(fAnchor, fAnchor);
	fHasSelection = true;
	SetMouseEventMask(B_PRIMARY_MOUSE_BUTTON);
}


void
ScanView::MouseUp(BPoint /*where*/)
{
	fDragging = false;
}


void
ScanView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* /*dragMessage*/)
{
	if (!fDragging)
		return;
	_TrackSelection(where);
}


void
ScanView::_TrackSelection(BPoint where)
{
	fSelection = BRect(fAnchor, _ViewToBitmap(where));
	if (fSelection.left > fSelection.right) {
		float swap = fSelection.left;
		fSelection.left = fSelection.right;
		fSelection.right = swap;
	}
	if (fSelection.top > fSelection.bottom) {
		float swap = fSelection.top;
		fSelection.top = fSelection.bottom;
		fSelection.bottom = swap;
	}
	fHasSelection = fSelection.Width() > 1 && fSelection.Height() > 1;
	Invalidate();
}


// Converts a point from this view's own coordinate space into the
// bitmap's pixel space, inverting the same fit-to-view scale/offset Draw()
// computes to go the other way. fSelection/fAnchor are kept in bitmap
// space (as ScanView.h documents) so Draw()'s existing bitmap->view
// transform for the selection outline, and PrintOMaticWindow::_BuildOptions()'s
// division by the bitmap's own bounds, both read the same, single
// representation -- previously MouseDown()/_TrackSelection() stored raw
// view-space points instead, which Draw() then rescaled a second time
// (wrong outline position) and _BuildOptions() divided as if already in
// bitmap space (wrong scan area, occasionally out of range).
BPoint
ScanView::_ViewToBitmap(BPoint where) const
{
	if (fBitmap == NULL || fBitmap->InitCheck() != B_OK)
		return where;

	BRect bounds = Bounds();
	BRect src = fBitmap->Bounds();
	float scale = bounds.Width() / src.Width();
	if (bounds.Height() / src.Height() < scale)
		scale = bounds.Height() / src.Height();

	BRect dst(0, 0, src.Width() * scale, src.Height() * scale);
	dst.OffsetTo((bounds.Width() - dst.Width()) / 2,
		(bounds.Height() - dst.Height()) / 2);

	BPoint point((where.x - dst.left) / scale, (where.y - dst.top) / scale);
	// Clamp into the bitmap's own bounds -- a drag outside the preview
	// (easy to do while tracking the mouse) must not produce an
	// out-of-range scan area.
	if (point.x < src.left)
		point.x = src.left;
	else if (point.x > src.right)
		point.x = src.right;
	if (point.y < src.top)
		point.y = src.top;
	else if (point.y > src.bottom)
		point.y = src.bottom;
	return point;
}
