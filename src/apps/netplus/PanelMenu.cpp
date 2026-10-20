/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */


#include "PanelMenu.h"
#include "NetPlusMessages.h"

#include <Button.h>
#include <ControlLook.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Rect.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include <Autolock.h>
#include <Locker.h>

#include <algorithm>
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string>


// Same private flag BMenuWindow sets: accept key events (Escape) even
// though the panel must not steal the page's focus ring. Value from
// headers/private/interface/WindowPrivate.h.
static const uint32 kAcceptKeyboardFocusFlag = 0x40000;

static const float kRowInset = 10.0f;
static const float kBorder = 1.0f;
static const char kBackArrow[] = "\xe2\x86\x90";
static const char kChevron[] = "\xe2\x80\xba";


// Shared with the click-outside tracker; the panel owns it.
struct PanelMenuTrackData {
	BMessenger			panel;
	BLocker				lock;
	BRect				frame;
	std::atomic<bool>	quit;

	PanelMenuTrackData() : lock("panel menu track"), quit(false) {}
};


class PanelMenuView : public BView {
public:
								PanelMenuView(BRect frame, PanelMenu* menu);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* message);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	BSize				PreferredSize();

private:
			float				_HeaderHeight() const;
			float				_RowHeight() const;
			int32				_RowAt(BPoint where) const;
			void				_SetHover(int32_t index);

			PanelMenu*			fMenu;
			int32				fHover;
};


PanelMenuView::PanelMenuView(BRect frame, PanelMenu* menu)
	:
	BView(frame, "panel menu view", B_FOLLOW_ALL, B_WILL_DRAW
		| B_FULL_UPDATE_ON_RESIZE),
	fMenu(menu),
	fHover(-1)
{
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);
}


float
PanelMenuView::_HeaderHeight() const
{
	return _RowHeight();
}


float
PanelMenuView::_RowHeight() const
{
	BFont font;
	GetFont(&font);
	font_height metrics;
	font.GetHeight(&metrics);
	return ceilf(metrics.ascent + metrics.descent) + kRowInset;
}


BSize
PanelMenuView::PreferredSize()
{
	const PanelMenuModel& model = fMenu->Model();

	BFont font;
	GetFont(&font);
	BFont boldFont(font);
	boldFont.SetFace(B_BOLD_FACE);

	float maxLabel = 0;
	float maxShortcut = 0;
	int32 rows = model.RowCount(model.CurrentPage());
	for (int32 i = 0; i < rows; i++) {
		const PanelMenuRow& row = model.RowAt(model.CurrentPage(), i);
		if (row.type == kPanelMenuRowSeparator)
			continue;
		maxLabel = std::max(maxLabel, font.StringWidth(row.label.c_str()));
		maxShortcut = std::max(maxShortcut,
			font.StringWidth(row.shortcut.c_str()));
	}

	if (model.CanGoBack()) {
		float header = boldFont.StringWidth(kBackArrow)
			+ boldFont.StringWidth(model.CurrentTitle())
			+ 3 * kRowInset;
		maxLabel = std::max(maxLabel, header);
	}

	float width = maxLabel + maxShortcut + 3 * kRowInset + kBorder * 2;
	if (width < 240)
		width = 240;

	float height = kBorder * 2;
	for (int32 i = 0; i < rows; i++) {
		height += model.RowAt(model.CurrentPage(), i).type
				== kPanelMenuRowSeparator
			? _RowHeight() / 3 : _RowHeight();
	}
	if (model.CanGoBack())
		height += _HeaderHeight();

	return BSize(ceilf(width), ceilf(height));
}


int32
PanelMenuView::_RowAt(BPoint where) const
{
	const PanelMenuModel& model = fMenu->Model();
	float y = kBorder;
	if (model.CanGoBack()) {
		if (where.y < y + _HeaderHeight())
			return -2;
			// header row: back arrow zone
		y += _HeaderHeight();
	}

	int32 rows = model.RowCount(model.CurrentPage());
	for (int32 i = 0; i < rows; i++) {
		const PanelMenuRow& row = model.RowAt(model.CurrentPage(), i);
		float height = row.type == kPanelMenuRowSeparator
			? _RowHeight() / 3 : _RowHeight();
		if (where.y < y + height)
			return i;
		y += height;
	}
	return -1;
}


void
PanelMenuView::_SetHover(int32_t index)
{
	if (fHover == index)
		return;
	fHover = index;
	Invalidate();
}


void
PanelMenuView::MouseDown(BPoint where)
{
	int32 row = _RowAt(where);
	if (row == -2) {
		// Only the left part of the header is the back button, the
		// rest is the page title.
		if (where.x < kBorder + 3 * kRowInset + StringWidth(kBackArrow))
			fMenu->_Back();
		return;
	}
	if (row >= 0)
		fMenu->_SelectRow(row);
}


void
PanelMenuView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* message)
{
	if (transit == B_OUTSIDE_VIEW) {
		_SetHover(-1);
		return;
	}

	int32 row = _RowAt(where);
	_SetHover(row >= 0 || row == -2 ? row : -1);
}


void
PanelMenuView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1 && bytes[0] == B_ESCAPE) {
		Window()->PostMessage(B_QUIT_REQUESTED);
		return;
	}
	BView::KeyDown(bytes, numBytes);
}


void
PanelMenuView::Draw(BRect updateRect)
{
	const PanelMenuModel& model = fMenu->Model();
	BRect bounds = Bounds();

	BFont font;
	GetFont(&font);
	BFont boldFont(font);
	boldFont.SetFace(B_BOLD_FACE);

	font_height metrics;
	font.GetHeight(&metrics);
	float textOffset = ceilf(metrics.ascent + kRowInset / 2);

	float y = kBorder;

	// border
	rgb_color border = ui_color(B_MENU_BACKGROUND_COLOR);
	border.red = (uint8)(border.red * 0.8f);
	border.green = (uint8)(border.green * 0.8f);
	border.blue = (uint8)(border.blue * 0.8f);
	SetHighColor(border);
	StrokeRect(bounds.InsetByCopy(0.5f, 0.5f));

	if (model.CanGoBack()) {
		float headerBottom = y + _HeaderHeight();
		BRect header(kBorder, y, bounds.right - kBorder, headerBottom);
		if (fHover == -2) {
			SetHighColor(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR));
			FillRect(header);
			SetHighColor(ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR));
		} else
			SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));

		BPoint where(header.left + kRowInset, y + textOffset);
		SetFont(&boldFont);
		DrawString(kBackArrow, where);
		where.x += boldFont.StringWidth(kBackArrow) + kRowInset / 2;
		DrawString(model.CurrentTitle(), where);
		SetFont(&font);

		y = headerBottom;
		SetHighColor(border);
		StrokeLine(BPoint(bounds.left, y), BPoint(bounds.right, y));
		SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));
	}

	int32 rows = model.RowCount(model.CurrentPage());
	for (int32 i = 0; i < rows; i++) {
		const PanelMenuRow& row = model.RowAt(model.CurrentPage(), i);
		if (row.type == kPanelMenuRowSeparator) {
			float middle = y + _RowHeight() / 6;
			SetHighColor(border);
			StrokeLine(BPoint(bounds.left + kRowInset, middle),
				BPoint(bounds.right - kRowInset, middle));
			SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));
			y += _RowHeight() / 3;
			continue;
		}

		float height = _RowHeight();
		BRect rowRect(kBorder, y, bounds.right - kBorder, y + height);

		bool selectable = row.type == kPanelMenuRowAction
			|| row.type == kPanelMenuRowSubPage;
		if (selectable && fHover == i) {
			SetHighColor(ui_color(B_MENU_SELECTED_BACKGROUND_COLOR));
			FillRect(rowRect);
			SetHighColor(ui_color(B_MENU_SELECTED_ITEM_TEXT_COLOR));
		} else if (row.type == kPanelMenuRowSectionLabel) {
			SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));
			rgb_color gray = HighColor();
			gray.red = (uint8)(gray.red * 0.6f);
			gray.green = (uint8)(gray.green * 0.6f);
			gray.blue = (uint8)(gray.blue * 0.6f);
			SetHighColor(gray);
		} else
			SetHighColor(ui_color(B_MENU_ITEM_TEXT_COLOR));

		float x = rowRect.left + kRowInset;
		BPoint where(x, y + textOffset);
		DrawString(row.label.c_str(), where);

		if (row.type == kPanelMenuRowSubPage) {
			const char* chevron = kChevron;
			float chevronWidth = font.StringWidth(chevron);
			DrawString(chevron,
				BPoint(rowRect.right - kRowInset - chevronWidth,
					y + textOffset));
		} else if (!row.shortcut.empty()) {
			float shortcutWidth = font.StringWidth(row.shortcut.c_str());
			DrawString(row.shortcut.c_str(),
				BPoint(rowRect.right - kRowInset - shortcutWidth,
					y + textOffset));
		}

		y += height;
	}
}


//#pragma mark -


PanelMenu::PanelMenu(const PanelMenuModel& model, const BMessenger& target)
	:
	// Same flag mix as BMenuWindow: float above the owner, do not
	// grab focus on show, but accept the explicit focus so Escape
	// and the arrow keys reach the panel.
	BWindow(BRect(0, 0, 0, 0), "panel menu", B_NO_BORDER_WINDOW_LOOK,
		B_FLOATING_APP_WINDOW_FEEL,
		B_NOT_MOVABLE | B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AVOID_FOCUS
			| B_CLOSE_ON_ESCAPE | kAcceptKeyboardFocusFlag),
	fModel(model),
	fTarget(new BMessenger(target)),
	fView(NULL),
	fTrack(NULL),
	fTrackThread(-1)
{
	fView = new PanelMenuView(BRect(0, 0, 0, 0), this);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0)
		.Add(fView);
}


PanelMenu::~PanelMenu()
{
	// The tracker never locks the window, so waiting cannot deadlock.
	if (fTrack != NULL)
		fTrack->quit = true;
	if (fTrackThread >= B_OK) {
		status_t status;
		wait_for_thread(fTrackThread, &status);
	}
	delete fTrack;

	delete fTarget;
}


const PanelMenuModel&
PanelMenu::Model() const
{
	return fModel;
}


void
PanelMenu::Go(BPoint where)
{
	fAnchor = where;
	_UpdateLayout();

	// Clamp the panel the way BMenu::_CalcFrame does: keep it on the
	// screen it was opened on, flipping up or left when the anchor
	// leaves no room below or right.
	BScreen screen(B_MAIN_SCREEN_ID);
	BRect screenFrame = screen.Frame();
	BRect frame = Frame();
	if (frame.bottom > screenFrame.bottom)
		frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
	else if (frame.top < screenFrame.top)
		frame.OffsetBy(0, screenFrame.top - frame.top);
	if (frame.right > screenFrame.right)
		frame.OffsetBy(screenFrame.right - frame.right, 0);
	else if (frame.left < screenFrame.left)
		frame.OffsetBy(screenFrame.left - frame.left, 0);
	MoveTo(frame.LeftTop());

	Show();
	Activate();

	// Go() runs on the caller's thread; take our own lock before
	// touching the view.
	if (Lock()) {
		fView->MakeFocus(true);
		Unlock();
	}

	// Close on click-outside: wait for the opening click to finish,
	// then watch for the next press that lands off the panel.
	fTrack = new PanelMenuTrackData;
	fTrack->panel = BMessenger(this);
	fTrack->frame = Frame();
	fTrackThread = spawn_thread(_TrackEntry, "panel menu tracker",
		B_DISPLAY_PRIORITY, fTrack);
	if (fTrackThread >= B_OK)
		resume_thread(fTrackThread);
	else
		fTrackThread = -1;
}


void
PanelMenu::FrameMoved(BPoint where)
{
	BWindow::FrameMoved(where);
	if (fTrack != NULL) {
		BAutolock _(fTrack->lock);
		fTrack->frame = Frame();
	}
}


bool
PanelMenu::QuitRequested()
{
	if (fTrack != NULL)
		fTrack->quit = true;

	// Let the owner drop its panel pointer before we are destroyed.
	if (fTarget->IsValid()) {
		BMessage message(MSG_PANEL_CLOSED);
		fTarget->SendMessage(&message);
	}

	return true;
}


void
PanelMenu::_UpdateLayout()
{
	// The window layout stretches the view to the window, so only
	// the window size needs to track the page content.
	if (!Lock())
		return;

	BSize size = fView->PreferredSize();
	Unlock();

	ResizeTo(size.width, size.height);

	// Keep the top edge on the anchor and the left edge aligned; the
	// caller clamps the final placement to the screen.
	BRect frame(0, 0, size.width - 1, size.height - 1);
	frame.OffsetTo(fAnchor);
	MoveTo(frame.LeftTop());

	if (Lock()) {
		fView->Invalidate();
		Unlock();
	}
}


void
PanelMenu::_SelectRow(int32_t index)
{
	uint32_t what = 0;
	std::string payload;
	int32_t page = -1;
	switch (fModel.SelectRow(index, &what, &payload, &page)) {
		case PanelMenuModel::kSelectInvoke:
		{
			BMessage message(what);
			if (!payload.empty())
				message.AddString("url", payload.c_str());
			if (fTarget->IsValid())
				fTarget->SendMessage(&message);
			PostMessage(B_QUIT_REQUESTED);
			break;
		}

		case PanelMenuModel::kSelectPush:
			fModel.PushPage(page);
			_UpdateLayout();
			break;

		default:
			break;
	}
}


void
PanelMenu::_Back()
{
	if (!fModel.CanGoBack())
		return;
	fModel.PopPage();
	_UpdateLayout();
}


/* static */
int32
PanelMenu::_TrackEntry(void* data)
{
	PanelMenuTrackData* track = static_cast<PanelMenuTrackData*>(data);

	// The click that opened the panel may still be down; wait it out
	// so the panel does not close on its own opening click.
	BPoint point;
	uint32 buttons;
	do {
		get_mouse(&point, &buttons);
		snooze(20000);
	} while (buttons != 0 && !track->quit);

	while (!track->quit) {
		get_mouse(&point, &buttons);
		if ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0) {
			BAutolock _(track->lock);
			if (!track->frame.Contains(point)) {
				track->panel.SendMessage(B_QUIT_REQUESTED);
				break;
			}
		}
		snooze(20000);
	}
	return 0;
}
