/*
 * Copyright 2026, VitruvianOS Project.
 * Distributed under the terms of the LGPL License.
 */


#include "StatusBarView.h"

#include <LayoutBuilder.h>
#include <StatusBar.h>
#include <StringView.h>


StatusBarView::StatusBarView(BRect frame)
	:
	BView(frame, "statusbar", B_FOLLOW_LEFT_RIGHT | B_FOLLOW_BOTTOM,
		B_WILL_DRAW),
	fProgressBar(nullptr),
	fStatusText(nullptr)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	fProgressBar = new BStatusBar("progress");
	fProgressBar->SetBarColor(make_color(80, 140, 220, 255));
	fProgressBar->SetExplicitMinSize(BSize(150, B_SIZE_UNSET));
	fProgressBar->SetExplicitMaxSize(BSize(200, B_SIZE_UNSET));
	fProgressBar->Hide();

	fStatusText = new BStringView("status", "");
	fStatusText->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(fStatusText, 1.0f)
		.Add(fProgressBar);
}


StatusBarView::~StatusBarView() = default;


void
StatusBarView::AttachedToWindow()
{
	BView::AttachedToWindow();
}


void
StatusBarView::SetProgress(double progress)
{
	if (fProgressBar->IsHidden())
		fProgressBar->Show();

	float newVal = (float)(progress * 100.0);
	fProgressBar->Update(newVal - fProgressBar->CurrentValue());

	if (progress >= 1.0)
		ClearProgress();
}


void
StatusBarView::SetStatusText(const char* text)
{
	if (fStatusText) fStatusText->SetText(text);
}


void
StatusBarView::ClearProgress()
{
	if (fProgressBar) {
		fProgressBar->Reset();
		fProgressBar->Hide();
	}
	if (fStatusText)
		fStatusText->SetText("");
}
