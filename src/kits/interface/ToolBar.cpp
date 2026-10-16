/*
 * Copyright 2011 Stephan Aßmus <superstippi@gmx.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#include <ToolBar.h>

#include <math.h>

#include <new>

#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Font.h>
#include <IconUtils.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <SeparatorView.h>
#include <SpaceLayoutItem.h>
#include <String.h>


static const uint32 kOverflowMessage = 'TBOF';


class ToolBarButton : public BButton {
public:
								ToolBarButton(BToolBar* toolBar,
									const char* label, BMessage* message,
									bool lockable);

	virtual	void				MouseDown(BPoint point);
	virtual	status_t			Invoke(BMessage* message = NULL);
	virtual	void				Draw(BRect updateRect);
	virtual	BSize				MinSize();

			void				SetLabelUnder(bool under);
			void				SetGroup(uint32 group) { fGroup = group; }

private:
			BToolBar*			fToolBar;
			bool				fLockable;
			bool				fLabelUnder;
			uint32				fGroup;
};


ToolBarButton::ToolBarButton(BToolBar* toolBar, const char* label,
				BMessage* message, bool lockable)
	:
	BButton(NULL, label, message),
	fToolBar(toolBar),
	fLockable(lockable),
	fLabelUnder(false),
	fGroup(0)
{
	SetFlat(true);
}


void
ToolBarButton::MouseDown(BPoint point)
{
	if (fLockable) {
		if ((modifiers() & B_SHIFT_KEY) != 0 || Value() == B_CONTROL_ON)
			SetBehavior(B_TOGGLE_BEHAVIOR);
		else
			SetBehavior(B_BUTTON_BEHAVIOR);

		if (Message() != NULL)
			Message()->SetInt32("behavior", Behavior());
	}

	BButton::MouseDown(point);
}


status_t
ToolBarButton::Invoke(BMessage* message)
{
	if (fToolBar != NULL && fGroup != 0 && Value() == B_CONTROL_ON)
		fToolBar->_SetRadioPressed(fToolBar->_ItemForButton(this));

	return BButton::Invoke(message);
}


void
ToolBarButton::Draw(BRect updateRect)
{
	if (!fLabelUnder || Label() == NULL) {
		BButton::Draw(updateRect);
		return;
	}

	BRect rect(Bounds());
	rgb_color background = ViewColor();
	rgb_color base = LowColor();

	uint32 flags = be_control_look->Flags(this);
	if (IsFlat() && !IsTracking())
		flags |= BControlLook::B_FLAT;

	be_control_look->DrawButtonFrame(this, rect, updateRect, base, background,
		flags);
	be_control_look->DrawButtonBackground(this, rect, updateRect, base, flags);

	const BBitmap* icon = IconBitmap(
		(IsEnabled() ? 0 : B_DISABLED_ICON_BITMAP));

	float bottom = Bounds().bottom;
	if (icon != NULL) {
		BRect iconRect = icon->Bounds();
		float x = floorf((Bounds().Width() + 1 - iconRect.Width()) / 2);
		DrawBitmap(icon, BPoint(x, 2));
		bottom = 2 + iconRect.bottom;
	}

	BFont font;
	GetFont(&font);
	BString truncated(Label());
	BRect labelRect(0, bottom + 2, Bounds().Width(), Bounds().bottom);
	font.TruncateString(&truncated, B_TRUNCATE_END, labelRect.Width());
	float width = ceilf(font.StringWidth(truncated.String()));
	float x = floorf((Bounds().Width() + 1 - width) / 2);
	font_height height;
	font.GetHeight(&height);
	DrawString(truncated.String(),
		BPoint(x, Bounds().bottom - ceilf(height.descent) - 1));
}


BSize
ToolBarButton::MinSize()
{
	if (!fLabelUnder || Label() == NULL)
		return BButton::MinSize();

	const BBitmap* icon = IconBitmap(B_ACTIVE_ICON_BITMAP);

	BFont font;
	GetFont(&font);

	float spacing = be_control_look->DefaultItemSpacing();
	float inset = ceilf(spacing / 2);

	float iconWidth = 0;
	float iconHeight = 0;
	if (icon != NULL) {
		iconWidth = icon->Bounds().Width() + 1;
		iconHeight = icon->Bounds().Height() + 1;
	}

	float width = ceilf(font.StringWidth(Label()));
	if (iconWidth > width)
		width = iconWidth;

	width += 2 * inset;
	float height = inset + iconHeight + ceilf(font.Size()) + 2 * inset;

	return BSize(width, height);
}


void
ToolBarButton::SetLabelUnder(bool under)
{
	if (fLabelUnder == under)
		return;

	fLabelUnder = under;
	InvalidateLayout(true);
	Invalidate();
}


//#pragma mark -


class OverflowButton : public BButton {
public:
								OverflowButton(BToolBar* toolBar);

	virtual	void				MouseDown(BPoint where);

			void				SetMenu(BPopUpMenu* menu);

private:
			BToolBar*			fToolBar;
			BPopUpMenu*			fMenu;
};


OverflowButton::OverflowButton(BToolBar* toolBar)
	:
	BButton(NULL, "···", new BMessage(kOverflowMessage)),
	fToolBar(toolBar),
	fMenu(NULL)
{
	SetFlat(true);
}


void
OverflowButton::MouseDown(BPoint where)
{
	if (fMenu == NULL) {
		BButton::MouseDown(where);
		return;
	}

	BPoint point = Bounds().LeftBottom() + BPoint(0, 1);
	ConvertToScreen(&point);
	fMenu->Go(point, false, false, true);
}


void
OverflowButton::SetMenu(BPopUpMenu* menu)
{
	delete fMenu;
	fMenu = menu;
}


//#pragma mark -


struct BToolBar::ActionItem {
								ActionItem();
								~ActionItem();

			uint32				command;
			BMessage*			message;
			const BBitmap*		icon;
			BString				toolTip;
			BString				text;
			BMessenger			target;
			bool				lockable;
			uint32				group;
			bool				isAction;
			bool				visible;
			BButton*			button;
			BLayoutItem*		layoutItem;
};


BToolBar::ActionItem::ActionItem()
	:
	command(0),
	message(NULL),
	icon(NULL),
	lockable(false),
	group(0),
	isAction(false),
	visible(true),
	button(NULL),
	layoutItem(NULL)
{
}


BToolBar::ActionItem::~ActionItem()
{
}


//#pragma mark -


BToolBar::BToolBar(BRect frame, orientation direction)
	:
	BGroupView(direction),
	fOrientation(direction),
	fLabelMode(B_TOOLBAR_LABEL_NONE),
	fOverflowEnabled(true),
	fInOverflowUpdate(false),
	fOverflowButton(NULL)
{
	_Init();

	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());
	SetResizingMode(B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP);
}


BToolBar::BToolBar(orientation direction)
	:
	BGroupView(direction),
	fOrientation(direction),
	fLabelMode(B_TOOLBAR_LABEL_NONE),
	fOverflowEnabled(true),
	fInOverflowUpdate(false),
	fOverflowButton(NULL)
{
	_Init();
}


BToolBar::~BToolBar()
{
	int32 count = fItems.CountItems();
	for (int32 i = 0; i < count; i++)
		delete (ActionItem*)fItems.ItemAt(i);

	count = fOwnedIcons.CountItems();
	for (int32 i = 0; i < count; i++)
		delete (BBitmap*)fOwnedIcons.ItemAt(i);
}


void
BToolBar::Hide()
{
	BView::Hide();
	// TODO: This could be fixed in BView instead. Looking from the
	// BButtons, they are not hidden though, only their parent is...
	_HideToolTips();
}


void
BToolBar::AddAction(uint32 command, BHandler* target, const BBitmap* icon,
	const char* toolTipText, const char* text, bool lockable)
{
	AddAction(new BMessage(command), target, icon, toolTipText, text,
		lockable);
}


void
BToolBar::AddAction(BMessage* message, BHandler* target, const BBitmap* icon,
	const char* toolTipText, const char* text, bool lockable)
{
	ToolBarButton* button = new(std::nothrow) ToolBarButton(this, text, message,
		lockable);
	if (button == NULL) {
		delete message;
		return;
	}

	button->SetIcon(icon);
	if (toolTipText != NULL)
		button->SetToolTip(toolTipText);

	ActionItem* item = new(std::nothrow) ActionItem;
	if (item == NULL) {
		delete button;
		return;
	}

	item->command = message->what;
	item->message = button->Message();
	item->icon = icon;
	if (toolTipText != NULL)
		item->toolTip = toolTipText;
	if (text != NULL)
		item->text = text;
	item->target = BMessenger(target);
	item->lockable = lockable;
	item->isAction = true;
	item->button = button;
	item->layoutItem = GroupLayout()->AddView(button);
	fItems.AddItem(item);

	button->SetTarget(target);
}


void
BToolBar::AddAction(uint32 command, BHandler* target, const uint8* hvifIcon,
	size_t hvifLength, const char* toolTipText, const char* text, bool lockable)
{
	BBitmap* bitmap = NULL;
	if (hvifIcon != NULL) {
		BSize size = be_control_look->ComposeIconSize(16);
		bitmap = new(std::nothrow) BBitmap(
			BRect(BPoint(0, 0), size), B_BITMAP_NO_SERVER_LINK, B_RGBA32);
		if (bitmap != NULL) {
			if (bitmap->InitCheck() != B_OK
				|| BIconUtils::GetVectorIcon(hvifIcon, hvifLength, bitmap)
					!= B_OK) {
				delete bitmap;
				bitmap = NULL;
			} else
				fOwnedIcons.AddItem(bitmap);
		}
	}

	AddAction(command, target, bitmap, toolTipText, text, lockable);
}


void
BToolBar::AddSeparator()
{
	orientation ont = (fOrientation == B_HORIZONTAL)
		? B_VERTICAL : B_HORIZONTAL;
	BSeparatorView* separator = new(std::nothrow) BSeparatorView(ont,
		B_PLAIN_BORDER);
	if (separator == NULL)
		return;

	ActionItem* item = new(std::nothrow) ActionItem;
	if (item == NULL) {
		delete separator;
		return;
	}

	item->layoutItem = GroupLayout()->AddView(separator);
	fItems.AddItem(item);
}


void
BToolBar::AddGlue()
{
	GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());
}


void
BToolBar::AddView(BView* view)
{
	ActionItem* item = new(std::nothrow) ActionItem;
	if (item == NULL) {
		delete view;
		return;
	}

	item->layoutItem = GroupLayout()->AddView(view);
	fItems.AddItem(item);
}


void
BToolBar::SetActionEnabled(uint32 command, bool enabled)
{
	ActionItem* item = _FindItem(command);
	if (item == NULL || item->button == NULL)
		return;

	item->button->SetEnabled(enabled);
}


void
BToolBar::SetActionPressed(uint32 command, bool pressed)
{
	ActionItem* item = _FindItem(command);
	if (item == NULL || item->button == NULL)
		return;

	item->button->SetValue(pressed ? B_CONTROL_ON : B_CONTROL_OFF);
	if (pressed)
		_SetRadioPressed(item);
}


void
BToolBar::SetActionVisible(uint32 command, bool visible)
{
	ActionItem* item = _FindItem(command);
	if (item == NULL || item->layoutItem == NULL)
		return;

	item->visible = visible;
	item->layoutItem->SetVisible(visible);
	_UpdateOverflow();
}


void
BToolBar::SetActionGroup(uint32 command, uint32 group)
{
	ActionItem* item = _FindItem(command);
	if (item == NULL || item->button == NULL)
		return;

	item->group = group;
	ToolBarButton* button = dynamic_cast<ToolBarButton*>(item->button);
	if (button != NULL) {
		button->SetBehavior(BButton::B_TOGGLE_BEHAVIOR);		button->SetGroup(group);
	}
}


void
BToolBar::SetLabelMode(uint32 mode)
{
	if (fLabelMode == mode)
		return;

	fLabelMode = mode;

	int32 count = fItems.CountItems();
	for (int32 i = 0; i < count; i++) {
		ActionItem* item = (ActionItem*)fItems.ItemAt(i);
		ToolBarButton* button = dynamic_cast<ToolBarButton*>(item->button);
		if (button != NULL)
			button->SetLabelUnder(mode == B_TOOLBAR_LABEL_UNDER);
	}

	ResizeToPreferred();
}


uint32
BToolBar::LabelMode() const
{
	return fLabelMode;
}


void
BToolBar::SetOverflowEnabled(bool enabled)
{
	if (fOverflowEnabled == enabled)
		return;

	fOverflowEnabled = enabled;
	_UpdateOverflow();
}


bool
BToolBar::IsOverflowEnabled() const
{
	return fOverflowEnabled;
}


BButton*
BToolBar::FindButton(uint32 command) const
{
	ActionItem* item = _FindItem(command);
	return item != NULL ? item->button : NULL;
}


// #pragma mark - Private methods


void
BToolBar::Pulse()
{
	// TODO: Perhaps this could/should be addressed in BView instead.
	if (IsHidden())
		_HideToolTips();
}


void
BToolBar::FrameResized(float width, float height)
{
	_UpdateOverflow();

	// TODO: There seems to be a bug in app_server which does not
	// correctly trigger invalidation of views which are shown, when
	// the resulting dirty area is somehow already part of an update region.
	Invalidate();
}


void
BToolBar::_Init()
{
	float inset = ceilf(be_control_look->DefaultItemSpacing() / 2);
	GroupLayout()->SetInsets(inset, 0, inset, 0);
	GroupLayout()->SetSpacing(1);

	SetFlags(Flags() | B_FRAME_EVENTS | B_PULSE_NEEDED);

	SetLowUIColor(B_MENU_BACKGROUND_COLOR);
	SetViewUIColor(B_MENU_BACKGROUND_COLOR);

	fOverflowButton = new(std::nothrow) OverflowButton(this);
}


void
BToolBar::_HideToolTips() const
{
	for (int32 i = 0; BView* view = ChildAt(i); i++)
		view->HideToolTip();
}


void
BToolBar::_SetRadioPressed(ActionItem* item)
{
	if (item == NULL || item->group == 0)
		return;

	int32 count = fItems.CountItems();
	for (int32 i = 0; i < count; i++) {
		ActionItem* other = (ActionItem*)fItems.ItemAt(i);
		if (other == item || other->group != item->group
			|| other->button == NULL) {
			continue;
		}

		other->button->SetValue(B_CONTROL_OFF);
	}
}


void
BToolBar::_UpdateOverflow()
{
	if (!fOverflowEnabled || fInOverflowUpdate || fOverflowButton == NULL)
		return;

	fInOverflowUpdate = true;

	bool horizontal = fOrientation == B_HORIZONTAL;

	int32 count = fItems.CountItems();
	float total = 0;
	int32 visibleActions = 0;
	for (int32 i = 0; i < count; i++) {
		ActionItem* item = (ActionItem*)fItems.ItemAt(i);
		item->layoutItem->SetVisible(item->visible);
		if (!item->visible)
			continue;
		BSize min = item->layoutItem->MinSize();
		total += horizontal ? min.Width() : min.Height();
		if (item->isAction)
			visibleActions++;
	}

	BSize overflowMin = fOverflowButton->MinSize();
	float overflowSize = horizontal
		? overflowMin.Width() : overflowMin.Height();
	float available = horizontal ? Bounds().Width() : Bounds().Height();

	bool hiddenAny = false;
	for (int32 i = count - 1; i >= 0 && total > available; i--) {
		ActionItem* item = (ActionItem*)fItems.ItemAt(i);
		if (!item->isAction || visibleActions <= 1)
			continue;

		BSize min = item->layoutItem->MinSize();
		item->layoutItem->SetVisible(false);
		total -= horizontal ? min.Width() : min.Height();
		visibleActions--;
		hiddenAny = true;

		if (total + overflowSize <= available)
			break;
	}

	if (hiddenAny) {
		BPopUpMenu* menu = new(std::nothrow) BPopUpMenu("overflow", false,
			false);
		if (menu != NULL) {
			for (int32 i = 0; i < count; i++) {
				ActionItem* item = (ActionItem*)fItems.ItemAt(i);
				if (item->isAction && !item->layoutItem->IsVisible()) {
					const char* label = item->text.String();
					if (label == NULL || label[0] == '\0')
						label = item->toolTip.String();
					if (label == NULL || label[0] == '\0')
						label = "···";
					BMenuItem* menuItem = new(std::nothrow) BMenuItem(label,
						new BMessage(*item->message));
					if (menuItem != NULL) {
						menuItem->SetTarget(item->target);
						if (item->button != NULL
							&& !item->button->IsEnabled()) {
							menuItem->SetEnabled(false);
						}
						menu->AddItem(menuItem);
					}
				}
			}
		}

		OverflowButton* overflow
			= dynamic_cast<OverflowButton*>(fOverflowButton);
		if (overflow != NULL)
			overflow->SetMenu(menu);
		else
			delete menu;

		if (fOverflowButton->Parent() == NULL)
			GroupLayout()->AddView(fOverflowButton);

		fOverflowButton->Show();
		fOverflowButton->InvalidateLayout();
	} else
		fOverflowButton->Hide();

	fInOverflowUpdate = false;
}


BToolBar::ActionItem*
BToolBar::_FindItem(uint32 command) const
{
	int32 count = fItems.CountItems();
	for (int32 i = 0; i < count; i++) {
		ActionItem* item = (ActionItem*)fItems.ItemAt(i);
		if (item->isAction && item->command == command)
			return item;
	}
	return NULL;
}


BToolBar::ActionItem*
BToolBar::_ItemForButton(BButton* button) const
{
	int32 count = fItems.CountItems();
	for (int32 i = 0; i < count; i++) {
		ActionItem* item = (ActionItem*)fItems.ItemAt(i);
		if (item->button == button)
			return item;
	}
	return NULL;
}
