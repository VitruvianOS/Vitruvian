/*
Open Tracker License

Terms and Conditions

Copyright (c) 1991-2000, Be Incorporated. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice applies to all licensees
and shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of Be Incorporated shall not be
used in advertising or otherwise to promote the sale, use or other dealings in
this Software without prior written authorization from Be Incorporated.

Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
trademarks of Be Incorporated in the United States and other countries. Other
brand product names are registered trademarks or trademarks of their respective
holders.
All rights reserved.
*/


#include "BarMenuTitle.h"

#include <ControlLook.h>
#include <Debug.h>
#include <Font.h>

#include <Bitmap.h>
#include <IconUtils.h>

#include "BarApp.h"
#include "BarView.h"
#include "BarWindow.h"
#include "DeskbarMenu.h"
#include "ResourceSet.h"
#include "icons.h"


TBarMenuTitle::TBarMenuTitle(float width, float height, BMenu* menu,
	TBarView* barView)
	:
	BMenuItem(menu, new BMessage(B_REFS_RECEIVED)),
	fWidth(width),
	fHeight(height),
	fMenu(menu),
	fBarView(barView),
	fInitStatus(B_NO_INIT)
{
	if (fMenu == NULL || fBarView == NULL)
		fInitStatus = B_BAD_VALUE;
	else
		fInitStatus = B_OK;
}


TBarMenuTitle::~TBarMenuTitle()
{
}


void
TBarMenuTitle::SetContentSize(float width, float height)
{
	fWidth = width;
	fHeight = height;
}


void
TBarMenuTitle::GetContentSize(float* width, float* height)
{
	*width = fWidth;
	*height = fHeight;
}


void
TBarMenuTitle::Draw()
{
	BMenu* menu = Menu();
	if (fInitStatus != B_OK || menu == NULL)
		return;

	BRect frame(Frame());
	rgb_color base = ui_color(B_MENU_BACKGROUND_COLOR);

	menu->PushState();

	BRect windowBounds = menu->Window()->Bounds();
	if (frame.right > windowBounds.right)
		frame.right = windowBounds.right;

	// fill in background
	if (IsSelected()) {
		be_control_look->DrawMenuItemBackground(menu, frame, frame, base,
			BControlLook::B_ACTIVATED);
	} else
		be_control_look->DrawButtonBackground(menu, frame, frame, base);

	menu->MovePenTo(ContentLocation());
	DrawContent();

	menu->PopState();
}


void
TBarMenuTitle::DrawContent()
{
	BMenu* menu = Menu();
	if (menu == NULL)
		return;

	const BRect frame(Frame());

	if (fBarView != NULL && fBarView->MiniState()) {
		// Mini mode: deskbar menu shows atom icon, team menu shows team icon
		bool isDeskbarMenu = dynamic_cast<TDeskbarMenu*>(fMenu) != NULL;
		int32 iconID = isDeskbarMenu ? R_AtomLogoBitmap : R_TeamIcon;

		size_t iconSize = 0;
		const uint8* iconData = (const uint8*)AppResSet()->FindResource(
			B_VECTOR_ICON_TYPE, iconID, &iconSize);

		if (iconData != NULL && iconSize > 0) {
			float side = floorf(min_c(frame.Width(), frame.Height())) - 4;
			if (side < 8) side = 8;
			BRect iconRect(0, 0, side - 1, side - 1);
			BBitmap* icon = new(std::nothrow) BBitmap(iconRect, B_RGBA32);
			if (icon != NULL && icon->InitCheck() == B_OK
				&& BIconUtils::GetVectorIcon(iconData, iconSize, icon) == B_OK) {
				float x = frame.left
					+ rintf((frame.Width() - side) / 2);
				float y = frame.top
					+ rintf((frame.Height() - side) / 2);
				menu->SetDrawingMode(B_OP_ALPHA);
				menu->DrawBitmap(icon, BPoint(x, y));
				menu->SetDrawingMode(B_OP_COPY);
			}
			delete icon;
		}
		return;
	}

	// Expanded/full mode: draw "VitruvianOS" in brand colors
	BFont font(be_bold_font);
	float fontSize = min_c(be_bold_font->Size(),
		floorf(frame.Height() * 0.62f));
	font.SetSize(fontSize);
	menu->SetFont(&font);

	font_height fh;
	font.GetHeight(&fh);

	float vWidth = font.StringWidth("V");
	float midWidth = font.StringWidth("itruvian");
	float osWidth = font.StringWidth("OS");
	float totalWidth = vWidth + midWidth + osWidth;

	float x = frame.left + rintf((frame.Width() - totalWidth) / 2);
	float y = frame.top + rintf((frame.Height() + fh.ascent - fh.descent) / 2);

	menu->SetDrawingMode(B_OP_OVER);

	// "V" in red
	menu->SetHighColor(210, 0, 0);
	menu->DrawString("V", BPoint(x, y));
	x += vWidth;

	// "itruvian" in black
	menu->SetHighColor(0, 0, 0);
	menu->DrawString("itruvian", BPoint(x, y));
	x += midWidth;

	// "OS" in blue
	menu->SetHighColor(0, 80, 200);
	menu->DrawString("OS", BPoint(x, y));
}


status_t
TBarMenuTitle::Invoke(BMessage* message)
{
	if (fInitStatus != B_OK || fBarView == NULL)
		return fInitStatus;

	BLooper* looper = fBarView->Looper();
	if (looper->Lock()) {
		// tell barview to add the refs to the deskbar menu
		fBarView->HandleDeskbarMenu(NULL);
		looper->Unlock();
	}

	return BMenuItem::Invoke(message);
}
