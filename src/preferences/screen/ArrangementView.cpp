/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ArrangementView.h"

#include <math.h>
#include <stdio.h>

#include <ControlLook.h>
#include <Font.h>
#include <Message.h>
#include <Window.h>


static const float kPadding = 8.0f;


static float
distance_between(BPoint a, BPoint b)
{
	return fabsf(a.x - b.x) + fabsf(a.y - b.y);
}


static bool
same_place(const BRect& a, const BRect& b)
{
	return a.LeftTop() == b.LeftTop();
}


ArrangementView::ArrangementView(const char* name)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
	fOutputs(4),
	fDragIndex(-1),
	fDragMoved(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


ArrangementView::~ArrangementView()
{
}


void
ArrangementView::GetPreferredSize(float* _width, float* _height)
{
	const float scaling = max_c(1.0f, be_plain_font->Size() / 12.0f);
	if (_width != NULL)
		*_width = 360.0f * scaling;
	if (_height != NULL)
		*_height = 180.0f * scaling;
}


void
ArrangementView::AttachedToWindow()
{
	SetViewColor(B_TRANSPARENT_COLOR);
}


void
ArrangementView::SetOutputs(const BMessage& outputs)
{
	fOutputs.MakeEmpty();

	const char* name;
	for (int32 i = 0; outputs.FindString("name", i, &name) == B_OK; i++) {
		output_entry* output = new output_entry;
		output->name = name;
		output->enabled = outputs.GetBool("enabled", i, true);
		output->first = outputs.GetBool("first", i, false);
		output->main = outputs.GetBool("main", i, false);
		output->screen = outputs.GetInt32("screen", i, -1);
		output->frame = outputs.GetRect("frame", i, BRect());
		fOutputs.AddItem(output);
	}

	if (Find(fSelected.String()) == NULL)
		fSelected = "";
	fDragIndex = -1;
	Invalidate();
}


void
ArrangementView::Select(const char* name)
{
	if (fSelected == name)
		return;

	fSelected = name;
	Invalidate();
}


const output_entry*
ArrangementView::Find(const char* name) const
{
	for (int32 i = 0; i < fOutputs.CountItems(); i++) {
		if (fOutputs.ItemAt(i)->name == name)
			return fOutputs.ItemAt(i);
	}

	return NULL;
}


bool
ArrangementView::IsMirrored() const
{
	int32 count = 0;
	for (int32 i = 0; i < fOutputs.CountItems(); i++) {
		const output_entry* output = fOutputs.ItemAt(i);
		if (output->screen < 0 || !output->enabled)
			continue;

		for (int32 j = 0; j < i; j++) {
			const output_entry* other = fOutputs.ItemAt(j);
			if (other->screen >= 0 && other->enabled
				&& !same_place(output->frame, other->frame))
				return false;
		}
		count++;
	}

	return count > 1;
}


/*!	The desktop area the view shows: the screens with room around them to
	drag one to any side.
*/
BRect
ArrangementView::_DesktopBounds() const
{
	BRect bounds;
	float width = 0;
	float height = 0;
	for (int32 i = 0; i < fOutputs.CountItems(); i++) {
		const output_entry* output = fOutputs.ItemAt(i);
		if (!output->enabled || output->screen < 0)
			continue;

		bounds = bounds.IsValid() ? bounds | output->frame : output->frame;
		width = max_c(width, output->frame.Width() + 1);
		height = max_c(height, output->frame.Height() + 1);
	}

	if (!bounds.IsValid())
		return BRect();

	bounds.InsetBy(-width / 2, -height / 2);
	return bounds;
}


float
ArrangementView::_Scale() const
{
	const BRect desktop = _DesktopBounds();
	if (!desktop.IsValid())
		return 1.0f;

	BRect area = Bounds().InsetByCopy(kPadding, kPadding);
	for (int32 i = 0; i < fOutputs.CountItems(); i++) {
		if (!fOutputs.ItemAt(i)->enabled) {
			area.bottom -= be_plain_font->Size() * 2.5f + kPadding;
			break;
		}
	}

	const float x = (area.Width() + 1) / (desktop.Width() + 1);
	const float y = (area.Height() + 1) / (desktop.Height() + 1);
	return max_c(0.0001f, min_c(x, y));
}


BRect
ArrangementView::_BoxFor(const output_entry* output, const BRect& desktop,
	float scale) const
{
	BRect area = Bounds().InsetByCopy(kPadding, kPadding);
	const float width = (desktop.Width() + 1) * scale;
	const float height = (desktop.Height() + 1) * scale;
	const float left = area.left + (area.Width() + 1 - width) / 2;
	float top = area.top;
	for (int32 i = 0; i < fOutputs.CountItems(); i++) {
		if (!fOutputs.ItemAt(i)->enabled) {
			area.bottom -= be_plain_font->Size() * 2.5f + kPadding;
			break;
		}
	}
	top += (area.Height() + 1 - height) / 2;

	BRect frame = output == NULL ? BRect() : output->frame;
	if (output != NULL && fDragIndex >= 0
		&& fOutputs.ItemAt(fDragIndex) == output)
		frame = fDragFrame;

	return BRect(left + (frame.left - desktop.left) * scale,
		top + (frame.top - desktop.top) * scale,
		left + (frame.right + 1 - desktop.left) * scale - 1,
		top + (frame.bottom + 1 - desktop.top) * scale - 1);
}


BRect
ArrangementView::_DisabledBox(int32 index) const
{
	// Disabled outputs sit in a row at the bottom.
	int32 slot = 0;
	for (int32 i = 0; i < index; i++) {
		if (!fOutputs.ItemAt(i)->enabled)
			slot++;
	}

	const float height = be_plain_font->Size() * 2.5f;
	const float width = be_plain_font->Size() * 9;
	BRect bounds = Bounds().InsetByCopy(kPadding, kPadding);
	return BRect(bounds.left + slot * (width + kPadding),
		bounds.bottom - height, bounds.left + slot * (width + kPadding)
		+ width, bounds.bottom);
}


void
ArrangementView::Draw(BRect updateRect)
{
	const rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(panel);
	FillRect(updateRect);

	const rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	const rgb_color border = tint_color(panel, B_DARKEN_3_TINT);
	const BRect desktop = _DesktopBounds();
	const float scale = _Scale();

	font_height fontHeight;
	GetFontHeight(&fontHeight);
	const float lineHeight = ceilf(fontHeight.ascent + fontHeight.descent);

	// Nothing to arrange on its own.
	for (int32 pass = 0; pass < 2; pass++) {
		for (int32 i = 0; i < fOutputs.CountItems(); i++) {
			const output_entry* output = fOutputs.ItemAt(i);
			const bool selected = fSelected == output->name;
			if ((selected ? 1 : 0) != pass)
				continue;

			BRect box;
			if (output->enabled && output->screen >= 0) {
				if (!desktop.IsValid())
					continue;
				box = _BoxFor(output, desktop, scale);
			} else if (!output->enabled)
				box = _DisabledBox(i);
			else
				continue;

			const bool disabled = !output->enabled;
			rgb_color fill = disabled ? tint_color(panel, B_DARKEN_1_TINT)
				: tint_color(panel, B_LIGHTEN_2_TINT);
			if (selected)
				fill = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);

			SetHighColor(fill);
			FillRect(box);
			SetHighColor(selected ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
				: border);
			StrokeRect(box);

			// The main screen carries a bar, as its menu bar would.
			if (output->main && !disabled) {
				SetHighColor(tint_color(panel, B_DARKEN_4_TINT));
				FillRect(BRect(box.left + 1, box.top + 1, box.right - 1,
					min_c(box.top + 3, box.bottom - 1)));
			}

			SetHighColor(disabled ? tint_color(text, B_LIGHTEN_2_TINT)
				: (selected ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
					: text));
			SetLowColor(fill);

			BString label = output->name;
			TruncateString(&label, B_TRUNCATE_END, box.Width() - 6);
			if (lineHeight <= box.Height()) {
				const float width = StringWidth(label.String());
				DrawString(label.String(), BPoint(
					box.left + (box.Width() - width) / 2,
					box.top + (box.Height() - lineHeight) / 2
						+ fontHeight.ascent));
			}
		}
	}
}


output_entry*
ArrangementView::_HitTest(BPoint where, bool cycle) const
{
	const BRect desktop = _DesktopBounds();
	const float scale = _Scale();

	output_entry* hits[16];
	int32 count = 0;
	for (int32 i = 0; i < fOutputs.CountItems() && count < 16; i++) {
		output_entry* output = fOutputs.ItemAt(i);
		BRect box;
		if (!output->enabled)
			box = _DisabledBox(i);
		else if (output->screen >= 0 && desktop.IsValid())
			box = _BoxFor(output, desktop, scale);
		else
			continue;

		if (box.Contains(where))
			hits[count++] = output;
	}

	if (count == 0)
		return NULL;

	if (cycle) {
		for (int32 i = 0; i < count; i++) {
			if (fSelected == hits[i]->name)
				return hits[(i + 1) % count];
		}
	}

	// The selected one is drawn last, so it is on top.
	for (int32 i = 0; i < count; i++) {
		if (fSelected == hits[i]->name)
			return hits[i];
	}

	return hits[count - 1];
}


void
ArrangementView::MouseDown(BPoint where)
{
	MakeFocus();

	output_entry* output = _HitTest(where, IsMirrored());
	if (output == NULL)
		return;

	if (fSelected != output->name) {
		fSelected = output->name;
		Invalidate();
		_Notify(kMsgOutputSelected);
	}

	// Mirrored screens sit on one another; they are arranged once the
	// mirroring is off.
	if (!output->enabled || output->screen < 0 || IsMirrored()
		|| fOutputs.CountItems() < 2)
		return;

	int32 enabled = 0;
	for (int32 i = 0; i < fOutputs.CountItems(); i++) {
		if (fOutputs.ItemAt(i)->enabled && fOutputs.ItemAt(i)->screen >= 0)
			enabled++;
	}
	if (enabled < 2)
		return;

	fDragIndex = fOutputs.IndexOf(output);
	fDragStart = where;
	fDragOrigin = output->frame;
	fDragFrame = output->frame;
	fDragMoved = false;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
}


void
ArrangementView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fDragIndex < 0)
		return;

	const float scale = _Scale();
	BRect wanted = fDragOrigin;
	wanted.OffsetBy(floorf((where.x - fDragStart.x) / scale),
		floorf((where.y - fDragStart.y) / scale));

	const BRect placed = _Placement(fDragIndex, wanted);
	if (placed.IsValid() && placed != fDragFrame) {
		fDragFrame = placed;
		fDragMoved = placed != fDragOrigin;
		Invalidate();
	}
}


void
ArrangementView::MouseUp(BPoint where)
{
	if (fDragIndex < 0)
		return;

	const bool moved = fDragMoved && fDragFrame != fDragOrigin;
	output_entry* output = fOutputs.ItemAt(fDragIndex);
	if (moved && output != NULL) {
		output->frame = fDragFrame;
		fDragIndex = -1;
		_Notify(kMsgLayoutDropped);
	} else
		fDragIndex = -1;

	Invalidate();
}


/*!	The place nearest to \a wanted where the dragged screen touches another
	one and overlaps none: snapped to their edges, never apart, never on top.
*/
BRect
ArrangementView::_Placement(int32 dragged, BRect wanted) const
{
	const float width = wanted.Width() + 1;
	const float height = wanted.Height() + 1;
	// Within this many desktop pixels an edge pulls the screen into line.
	const float snap = 12.0f / _Scale();

	BRect best;
	float bestDistance = 0;

	for (int32 i = 0; i < fOutputs.CountItems(); i++) {
		const output_entry* other = fOutputs.ItemAt(i);
		if (i == dragged || !other->enabled || other->screen < 0)
			continue;

		const BRect o = other->frame;
		BRect candidates[4];
		// right of, left of, below, above
		float y = max_c(o.top - height + 1, min_c(wanted.top, o.bottom));
		float x = max_c(o.left - width + 1, min_c(wanted.left, o.right));
		if (fabsf(y - o.top) <= snap)
			y = o.top;
		else if (fabsf(y + height - 1 - o.bottom) <= snap)
			y = o.bottom - height + 1;
		if (fabsf(x - o.left) <= snap)
			x = o.left;
		else if (fabsf(x + width - 1 - o.right) <= snap)
			x = o.right - width + 1;

		candidates[0].Set(o.right + 1, y, o.right + width, y + height - 1);
		candidates[1].Set(o.left - width, y, o.left - 1, y + height - 1);
		candidates[2].Set(x, o.bottom + 1, x + width - 1, o.bottom + height);
		candidates[3].Set(x, o.top - height, x + width - 1, o.top - 1);

		for (int32 c = 0; c < 4; c++) {
			bool clear = true;
			for (int32 j = 0; j < fOutputs.CountItems(); j++) {
				const output_entry* check = fOutputs.ItemAt(j);
				if (j == dragged || !check->enabled || check->screen < 0)
					continue;
				if (candidates[c].Intersects(check->frame)) {
					clear = false;
					break;
				}
			}
			if (!clear)
				continue;

			const float distance = distance_between(candidates[c].LeftTop(),
				wanted.LeftTop());
			if (!best.IsValid() || distance < bestDistance) {
				best = candidates[c];
				bestDistance = distance;
			}
		}
	}

	return best;
}


void
ArrangementView::_Notify(uint32 what)
{
	if (Window() == NULL)
		return;

	BMessage message(what);
	if (what == kMsgOutputSelected)
		message.AddString("name", fSelected);
	else {
		for (int32 i = 0; i < fOutputs.CountItems(); i++) {
			const output_entry* output = fOutputs.ItemAt(i);
			if (!output->enabled || output->screen < 0)
				continue;

			message.AddInt32("screen", output->screen);
			message.AddPoint("origin", output->frame.LeftTop());
		}
	}

	Window()->PostMessage(&message);
}
