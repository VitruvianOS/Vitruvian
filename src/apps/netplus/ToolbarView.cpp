/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "NetPlusMessages.h"
#include "ToolbarView.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <Looper.h>
#include <Message.h>
#include <TextControl.h>


ToolbarView::ToolbarView(BRect frame)
	:
	BView(frame, "toolbar", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_FRAME_EVENTS),
	fBackButton(nullptr),
	fForwardButton(nullptr),
	fReloadStopButton(nullptr),
	fHomeButton(nullptr),
	fURLBar(nullptr),
	fLoading(false)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fBackButton       = new BButton("\xe2\x86\x90", new BMessage(MSG_NAV_BACK));
	fForwardButton    = new BButton("\xe2\x86\x92", new BMessage(MSG_NAV_FORWARD));
	fReloadStopButton = new BButton("\xe2\x86\xba", new BMessage(MSG_NAV_RELOAD));
	fHomeButton       = new BButton("\xe2\x8c\x82", new BMessage(MSG_NAV_HOME));

	fURLBar = new BTextControl("", "", new BMessage(MSG_OPEN_URL));
	fURLBar->SetExplicitMinSize(BSize(200, B_SIZE_UNSET));
	fURLBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(fBackButton)
		.Add(fForwardButton)
		.Add(fReloadStopButton)
		.Add(fHomeButton)
		.AddStrut(B_USE_SMALL_SPACING)
		.Add(fURLBar, 1.0f);
}


ToolbarView::~ToolbarView() = default;


void
ToolbarView::AttachedToWindow()
{
	BView::AttachedToWindow();
	fBackButton->SetTarget(Window());
	fForwardButton->SetTarget(Window());
	fReloadStopButton->SetTarget(Window());
	fHomeButton->SetTarget(Window());
	fURLBar->SetTarget(Window());
	fBackButton->SetEnabled(false);
	fForwardButton->SetEnabled(false);
}


void
ToolbarView::MessageReceived(BMessage* message)
{
	BView::MessageReceived(message);
}


void
ToolbarView::SetURL(const char* url)
{
	if (fURLBar) fURLBar->SetText(url);
}


void
ToolbarView::SetLoading(bool loading)
{
	fLoading = loading;
	if (!fReloadStopButton) return;
	if (loading) {
		fReloadStopButton->SetLabel("\xe2\x9c\x95");
		fReloadStopButton->SetMessage(new BMessage(MSG_NAV_STOP));
	} else {
		fReloadStopButton->SetLabel("\xe2\x86\xba");
		fReloadStopButton->SetMessage(new BMessage(MSG_NAV_RELOAD));
	}
}


void
ToolbarView::SetCanGoBack(bool can)
{
	if (fBackButton) fBackButton->SetEnabled(can);
}


void
ToolbarView::SetCanGoForward(bool can)
{
	if (fForwardButton) fForwardButton->SetEnabled(can);
}
