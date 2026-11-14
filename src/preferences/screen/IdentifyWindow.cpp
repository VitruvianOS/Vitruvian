/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "IdentifyWindow.h"

#include <stdio.h>

#include <Font.h>
#include <View.h>


static const uint32 kMsgIdentifyDone = 'idne';


class IdentifyView : public BView {
public:
	IdentifyView(BRect frame, int32 number, const char* name)
		:
		BView(frame, "identify", B_FOLLOW_ALL, B_WILL_DRAW),
		fNumber(number),
		fName(name)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	virtual void Draw(BRect updateRect)
	{
		const BRect bounds = Bounds();
		SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillRect(bounds);

		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		StrokeRect(bounds);

		BFont big(be_bold_font);
		big.SetSize(bounds.Height() * 0.55f);
		SetFont(&big);
		BString number;
		number << fNumber;
		font_height height;
		big.GetHeight(&height);
		DrawString(number.String(), BPoint(
			(bounds.Width() - big.StringWidth(number.String())) / 2,
			bounds.Height() * 0.62f));

		BFont small(be_plain_font);
		small.SetSize(bounds.Height() * 0.12f);
		SetFont(&small);
		DrawString(fName.String(), BPoint(
			(bounds.Width() - small.StringWidth(fName.String())) / 2,
			bounds.Height() * 0.88f));
	}

private:
	int32		fNumber;
	BString		fName;
};


IdentifyWindow::IdentifyWindow(BRect screenFrame, int32 number,
	const char* name)
	:
	BWindow(BRect(screenFrame.left + 40, screenFrame.top + 40,
			screenFrame.left + 40 + 239, screenFrame.top + 40 + 159),
		"identify", B_BORDERED_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_CLOSABLE | B_AVOID_FOCUS
			| B_NOT_MOVABLE,
		B_ALL_WORKSPACES),
	fRunner(NULL)
{
	AddChild(new IdentifyView(Bounds(), number, name));

	BMessage done(kMsgIdentifyDone);
	fRunner = new BMessageRunner(BMessenger(this), &done, 2500000, 1);
}


IdentifyWindow::~IdentifyWindow()
{
	delete fRunner;
}


void
IdentifyWindow::MessageReceived(BMessage* message)
{
	if (message->what == kMsgIdentifyDone) {
		Quit();
		return;
	}

	BWindow::MessageReceived(message);
}
