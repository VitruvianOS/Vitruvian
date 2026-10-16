/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "SuggestPopup.h"
#include "NetPlusMessages.h"

#include <Font.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Messenger.h>
#include <Rect.h>
#include <Screen.h>
#include <Size.h>
#include <View.h>
#include <Window.h>

#include <algorithm>
#include <math.h>


static const float kRowInset = 6.0f;
static const float kBorder = 1.0f;
static const float kMinWidth = 200.0f;


class SuggestPopupView : public BView {
public:
								SuggestPopupView(BRect frame,
									SuggestPopup* popup);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* message);
	virtual	BSize				PreferredSize();

			void				SetContent(const std::vector<std::string>& urls,
									int32_t selection);

private:
			float				_RowHeight() const;
			int32				RowAt(BPoint where) const;

			SuggestPopup*		fPopup;
			std::vector<std::string>	fUrls;
			int32				fSelection;
			int32				fHover;
};


SuggestPopupView::SuggestPopupView(BRect frame, SuggestPopup* popup)
	:
	BView(frame, "suggest popup view", B_FOLLOW_ALL, B_WILL_DRAW
		| B_FULL_UPDATE_ON_RESIZE),
	fPopup(popup),
	fSelection(-1),
	fHover(-1)
{
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);
}


float
SuggestPopupView::_RowHeight() const
{
	BFont font;
	GetFont(&font);
	font_height metrics;
	font.GetHeight(&metrics);
	return ceilf(metrics.ascent + metrics.descent) + kRowInset;
}


BSize
SuggestPopupView::PreferredSize()
{
	BFont font;
	GetFont(&font);

	float maxLabel = kMinWidth;
	for (size_t i = 0; i < fUrls.size(); i++)
		maxLabel = std::max(maxLabel, font.StringWidth(fUrls[i].c_str()));

	float width = maxLabel + 2 * kRowInset + kBorder * 2;
	float height = kBorder * 2 + fUrls.size() * _RowHeight();
	return BSize(ceilf(width), ceilf(height));
}


int32
SuggestPopupView::RowAt(BPoint where) const
{
	float y = kBorder;
	for (size_t i = 0; i < fUrls.size(); i++) {
		if (where.y < y + _RowHeight())
			return (int32)i;
		y += _RowHeight();
	}
	return -1;
}


void
SuggestPopupView::SetContent(const std::vector<std::string>& urls,
	int32_t selection)
{
	fUrls = urls;
	fSelection = selection;
	fHover = -1;
	Invalidate();
}


void
SuggestPopupView::MouseDown(BPoint where)
{
	int32 row = RowAt(where);
	if (row < 0 || row >= (int32)fUrls.size())
		return;

	BMessage message(MSG_SUGGEST_ACCEPTED);
	message.AddString("url", fUrls[row].c_str());
	if (fPopup->fOwner->IsValid())
		fPopup->fOwner->SendMessage(&message);
}


void
SuggestPopupView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* message)
{
	if (transit == B_OUTSIDE_VIEW) {
		if (fHover != -1) {
			fHover = -1;
			Invalidate();
		}
		return;
	}

	int32 row = RowAt(where);
	if (row != fHover) {
		fHover = row;
		Invalidate();
	}
}


void
SuggestPopupView::Draw(BRect updateRect)
{
	BRect bounds = Bounds();

	BFont font;
	GetFont(&font);
	font_height metrics;
	font.GetHeight(&metrics);
	float textOffset = ceilf(metrics.ascent + kRowInset / 2);

	rgb_color border = ui_color(B_MENU_BACKGROUND_COLOR);
	border.red = (uint8)(border.red * 0.8f);
	border.green = (uint8)(border.green * 0.8f);
	border.blue = (uint8)(border.blue * 0.8f);
	SetHighColor(border);
	StrokeRect(bounds.InsetByCopy(0.5f, 0.5f));

	float y = kBorder;
	for (size_t i = 0; i < fUrls.size(); i++) {
		BRect rowRect(kBorder, y, bounds.right - kBorder,
			y + _RowHeight());

		if (fHover == (int32)i || fSelection == (int32)i) {
			SetHighColor(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR));
			FillRect(rowRect);
			SetHighColor(ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR));
		} else
			SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));

		BPoint where(rowRect.left + kRowInset, y + textOffset);
		DrawString(fUrls[i].c_str(), where);
		y += _RowHeight();
	}
}


//#pragma mark -


SuggestPopup::SuggestPopup(const BMessenger& owner)
	:
	BWindow(BRect(0, 0, 0, 0), "url suggestions", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_APP_WINDOW_FEEL,
		B_NOT_MOVABLE | B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AVOID_FOCUS),
	fView(NULL),
	fOwner(new BMessenger(owner))
{
	fView = new SuggestPopupView(BRect(0, 0, 0, 0), this);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(fView);
}


SuggestPopup::~SuggestPopup()
{
	delete fOwner;
}


void
SuggestPopup::Go(BPoint where)
{
	if (!Lock())
		return;

	BSize size = fView->PreferredSize();
	ResizeTo(size.width, size.height);

	// Keep the list on the screen it was opened on, flipping up when
	// the anchor leaves no room below.
	BRect frame(0, 0, size.width - 1, size.height - 1);
	frame.OffsetTo(where);
	BScreen screen(B_MAIN_SCREEN_ID);
	BRect screenFrame = screen.Frame();
	if (frame.bottom > screenFrame.bottom)
		frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
	else if (frame.top < screenFrame.top)
		frame.OffsetBy(0, screenFrame.top - frame.top);
	if (frame.left < screenFrame.left)
		frame.OffsetBy(screenFrame.left - frame.left, 0);
	MoveTo(frame.LeftTop());

	fView->Invalidate();
	Unlock();

	Show();
}


void
SuggestPopup::Update(const std::vector<std::string>& urls, int32_t selection)
{
	BMessage message(kSuggestUpdate);
	for (size_t i = 0; i < urls.size(); i++)
		message.AddString("urls", urls[i].c_str());
	message.AddInt32("selection", selection);
	PostMessage(&message);
}


bool
SuggestPopup::QuitRequested()
{
	return true;
}


void
SuggestPopup::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kSuggestUpdate:
		{
			std::vector<std::string> urls;
			const char* url = NULL;
			for (int32 i = 0;
				message->FindString("urls", i, &url) == B_OK; i++) {
				urls.push_back(url);
			}

			int32 selection = -1;
			message->FindInt32("selection", &selection);

			if (urls.empty()) {
				Hide();
				break;
			}

			fView->SetContent(urls, selection);
			BSize size = fView->PreferredSize();
			ResizeTo(size.width, size.height);
			fView->Invalidate();
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}
