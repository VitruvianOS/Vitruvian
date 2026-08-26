/*
 * Copyright 2001-2009, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Stefano Ceccherini (stefano.ceccherini@gmail.com)
 */

#include <MenuPrivate.h>

#include <ControlLook.h>
#include <Menu.h>


static const char* kShiftLabel = "SHIFT";
static const char* kControlLabel = "CTRL";
static const char* kOptionLabel = "OPT";
static const char* kAltLabel = "ALT";


static void
key_cap_font(BView* view, BFont& font)
{
	view->GetFont(&font);
	font.SetSize(ceilf(font.Size() * 0.55f));
}


static float
key_cap_inset(BView* view)
{
	BFont font;
	view->GetFont(&font);

	return ceilf(font.Size() / 6.0f);
}


namespace BPrivate {


MenuPrivate::MenuPrivate(BMenu* menu)
	:
	fMenu(menu)
{
}


menu_layout
MenuPrivate::Layout() const
{
	return fMenu->Layout();
}


void
MenuPrivate::SetLayout(menu_layout layout)
{
	fMenu->fLayout = layout;
}


void
MenuPrivate::ItemMarked(BMenuItem* item)
{
	fMenu->_ItemMarked(item);
}


void
MenuPrivate::CacheFontInfo()
{
	fMenu->_CacheFontInfo();
}


float
MenuPrivate::FontHeight() const
{
	return fMenu->fFontHeight;
}


float
MenuPrivate::Ascent() const
{
	return fMenu->fAscent;
}


BRect
MenuPrivate::Padding() const
{
	return fMenu->fPad;
}


void
MenuPrivate::GetItemMargins(float* left, float* top, float* right,
	float* bottom) const
{
	fMenu->GetItemMargins(left, top, right, bottom);
}


void
MenuPrivate::SetItemMargins(float left, float top, float right, float bottom)
{
	fMenu->SetItemMargins(left, top, right, bottom);
}


int
MenuPrivate::State(BMenuItem** item) const
{
	return fMenu->_State(item);
}


void
MenuPrivate::Install(BWindow* window)
{
	fMenu->_Install(window);
}


void
MenuPrivate::Uninstall()
{
	fMenu->_Uninstall();
}


void
MenuPrivate::SetSuper(BMenu* menu)
{
	fMenu->fSuper = menu;
}


void
MenuPrivate::SetSuperItem(BMenuItem* item)
{
	fMenu->fSuperitem = item;
}


void
MenuPrivate::InvokeItem(BMenuItem* item, bool now)
{
	fMenu->_InvokeItem(item, now);
}


void
MenuPrivate::QuitTracking(bool thisMenuOnly)
{
	fMenu->_QuitTracking(thisMenuOnly);
}


/* static */
const char*
MenuPrivate::MenuItemShift()
{
	return kShiftLabel;
}


/* static */
const char*
MenuPrivate::MenuItemControl()
{
	switch (BMenu::sControlKey) {
		case 0x5d:
		case 0x5f:
			return kAltLabel;

		case 0x66:
		case 0x67:
			return kOptionLabel;
	}

	return kControlLabel;
}


/* static */
const char*
MenuPrivate::MenuItemOption()
{
	switch (BMenu::sOptionKey) {
		case 0x5c:
		case 0x60:
			return kControlLabel;

		case 0x66:
		case 0x67:
			return kOptionLabel;
	}

	return kAltLabel;
}


/* static */
const char*
MenuPrivate::MenuItemCommand()
{
	switch (BMenu::sCommandKey) {
		case 0x5c:
		case 0x60:
			return kControlLabel;

		case 0x66:
		case 0x67:
			return kOptionLabel;
	}

	return kAltLabel;
}


/* static */
float
MenuPrivate::KeyCapWidth(BView* view, const char* label)
{
	BFont font;
	key_cap_font(view, font);

	// the label, its padding, and a pixel of border on either side
	return ceilf(font.StringWidth(label)) + key_cap_inset(view) * 2 + 2;
}


/* static */
float
MenuPrivate::KeyCapHeight(BView* view)
{
	BFont font;
	view->GetFont(&font);

	return ceilf(font.Size() * 0.9f);
}


/* static */
void
MenuPrivate::DrawKeyCap(BView* view, BPoint leftTop, float width,
	const char* label)
{
	BRect rect(leftTop.x, leftTop.y, leftTop.x + width - 1,
		leftTop.y + KeyCapHeight(view) - 1);

	const rgb_color base = view->LowColor();
	const rgb_color textColor = view->HighColor();

	view->PushState();

	view->SetHighColor(tint_color(base, B_DARKEN_1_TINT));
	view->FillRect(rect);

	// bevel: light along the top and left, dark along the bottom and right
	view->SetHighColor(tint_color(base, B_LIGHTEN_MAX_TINT));
	view->StrokeLine(rect.LeftBottom(), rect.LeftTop());
	view->StrokeLine(rect.LeftTop(), rect.RightTop());

	view->SetHighColor(tint_color(base, B_DARKEN_3_TINT));
	view->StrokeLine(rect.RightTop(), rect.RightBottom());
	view->StrokeLine(rect.RightBottom(), rect.LeftBottom());

	BFont originalFont;
	view->GetFont(&originalFont);

	BFont font;
	key_cap_font(view, font);
	view->SetFont(&font);

	font_height fontHeight;
	font.GetHeight(&fontHeight);
	const float baseline = rect.top + fontHeight.ascent
		+ ceilf((rect.Height() + 1 - (fontHeight.ascent + fontHeight.descent))
			/ 2);

	view->SetHighColor(textColor);
	view->SetDrawingMode(B_OP_OVER);
	view->DrawString(label,
		BPoint(rect.left + key_cap_inset(view) + 1, baseline));

	view->SetFont(&originalFont);
	view->PopState();
}


}	// namespace BPrivate
