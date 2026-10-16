/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "NetPlusMessages.h"
#include "PanelMenu.h"
#include "ToolbarIcons.h"
#include "ToolbarView.h"

#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <IconUtils.h>
#include <LayoutBuilder.h>
#include <Looper.h>
#include <Message.h>
#include <String.h>
#include <TextControl.h>
#include <TextView.h>
#include <ToolBar.h>


static BBitmap*
_LoadIcon(const uint8* hvif, size_t size)
{
	BSize iconSize = be_control_look->ComposeIconSize(16);
	BBitmap* bitmap = new BBitmap(BRect(BPoint(0, 0), iconSize),
		B_BITMAP_NO_SERVER_LINK, B_RGBA32);
	if (bitmap == NULL)
		return NULL;
	if (bitmap->InitCheck() != B_OK
		|| BIconUtils::GetVectorIcon(hvif, size, bitmap) != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


ToolbarView::ToolbarView(BRect frame)
	:
	BView(frame, "toolbar", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_FRAME_EVENTS),
	fToolBar(NULL),
	fURLBar(NULL),
	fReloadIcon(NULL),
	fStopIcon(NULL),
	fLoading(false)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fReloadIcon = _LoadIcon(kNetPlusReloadIcon, kNetPlusReloadIconSize);
	fStopIcon = _LoadIcon(kNetPlusStopIcon, kNetPlusStopIconSize);

	// The actions are added in AttachedToWindow: BToolBar snapshots
	// a BMessenger(target) per action, which is only valid once the
	// view has a looper.
	fToolBar = new BToolBar(B_HORIZONTAL);

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.SetInsets(0)
		.Add(fToolBar, 1.0f);
}


ToolbarView::~ToolbarView()
{
	delete fReloadIcon;
	delete fStopIcon;
}


void
ToolbarView::AttachedToWindow()
{
	BView::AttachedToWindow();

	// Re-attach would duplicate the actions; they only need the
	// looper once.
	if (fToolBar->FindButton(MSG_NAV_BACK) != NULL)
		return;

	fToolBar->AddAction(MSG_NAV_BACK, Window(), kNetPlusBackIcon,
		kNetPlusBackIconSize, "Back");
	fToolBar->AddAction(MSG_NAV_FORWARD, Window(), kNetPlusForwardIcon,
		kNetPlusForwardIconSize, "Forward");
	fToolBar->AddAction(new BMessage(MSG_NAV_RELOAD), Window(), fReloadIcon,
		"Reload");
	fToolBar->AddAction(MSG_NAV_HOME, Window(), kNetPlusHomeIcon,
		kNetPlusHomeIconSize, "Home");

	// BToolBar hosts buttons, not arbitrary controls: the address
	// field is embedded with AddView, so it shares the strip and the
	// overflow sizing, but never collapses into the overflow menu
	// itself (only actions do).
	fURLBar = new BTextControl("", "", new BMessage(MSG_OPEN_URL));
	fURLBar->SetExplicitMinSize(BSize(200, B_SIZE_UNSET));
	fURLBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fURLBar->SetTarget(Window());
	fToolBar->AddView(fURLBar);

	fToolBar->AddAction(MSG_APP_MENU, this, kNetPlusMenuIcon,
		kNetPlusMenuIconSize, "Main menu");

	fToolBar->SetActionEnabled(MSG_NAV_BACK, false);
	fToolBar->SetActionEnabled(MSG_NAV_FORWARD, false);
}


void
ToolbarView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_APP_MENU:
		{
			// Anchor under the hamburger button, like BToolBar's
			// own overflow popup does.
			BButton* button = fToolBar->FindButton(MSG_APP_MENU);
			if (button == NULL)
				break;
			BPoint point = button->Bounds().LeftBottom() + BPoint(0, 1);
			button->ConvertToScreen(&point);

			BMessage show(MSG_APP_MENU);
			show.AddPoint("anchor", point);
			Window()->PostMessage(&show);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
ToolbarView::SetURL(const char* url)
{
	if (fURLBar != NULL)
		fURLBar->SetText(url);
}


void
ToolbarView::SetLoading(bool loading)
{
	fLoading = loading;

	BButton* button = fToolBar->FindButton(MSG_NAV_RELOAD);
	if (button == NULL)
		return;

	if (loading) {
		button->SetMessage(new BMessage(MSG_NAV_STOP));
		button->SetIcon(fStopIcon);
		button->SetToolTip("Stop");
	} else {
		button->SetMessage(new BMessage(MSG_NAV_RELOAD));
		button->SetIcon(fReloadIcon);
		button->SetToolTip("Reload");
	}
}


void
ToolbarView::SetCanGoBack(bool can)
{
	fToolBar->SetActionEnabled(MSG_NAV_BACK, can);
}


void
ToolbarView::SetCanGoForward(bool can)
{
	fToolBar->SetActionEnabled(MSG_NAV_FORWARD, can);
}


void
ToolbarView::FocusURL()
{
	if (fURLBar == NULL)
		return;

	fURLBar->MakeFocus(true);
	fURLBar->TextView()->SelectAll();
}
