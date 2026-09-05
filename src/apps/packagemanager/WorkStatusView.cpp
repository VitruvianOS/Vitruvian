/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "WorkStatusView.h"

#include <BarberPole.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <StatusBar.h>
#include <String.h>
#include <View.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WorkStatusView"


enum { kBarberPoleCard = 0, kProgressBarCard = 1 };

static const BSize kProgressSize(180, 14);


WorkStatusView::WorkStatusView()
	:
	Inherited("work status", B_HORIZONTAL, B_USE_SMALL_SPACING),
	fBarberPole(new BarberPole("barber pole")),
	fProgressBar(new BStatusBar("progress")),
	fProgressLayout(new BCardLayout()),
	fProgressView(new BView("progress view", 0))
{
	fStatusText = new TruncatingStringView("status text", B_TRANSLATE("Ready"));
	fStatusText->SetMinWidthChars(32);
	fPackageText = new TruncatingStringView("package text", "");
	fPackageText->SetFont(be_bold_font);
	fPackageText->SetMinWidthChars(24);

	fPendingText = new TruncatingStringView("pending text", "");
	fPendingText->SetMinWidthChars(16);

	fBarberPole->SetExplicitSize(kProgressSize);
	fProgressBar->SetMaxValue(1.0f);
	fProgressBar->SetBarHeight(kProgressSize.Height());
	fProgressBar->SetExplicitSize(kProgressSize);

	fProgressView->SetLayout(fProgressLayout);
	fProgressLayout->AddView(fBarberPole);
	fProgressLayout->AddView(fProgressBar);

	// The glue keeps this row's max width unlimited, or the window is pinned.
	BLayoutBuilder::Group<>(this)
		.Add(fStatusText)
		.Add(fPackageText)
		.AddGlue()
		.Add(fPendingText)
		.Add(fProgressLayout);

	SetIdle(B_TRANSLATE("Ready"));
}


WorkStatusView::~WorkStatusView()
{
}


void
WorkStatusView::SetIdle(const char* text)
{
	fStatusText->SetFullText(text);
	fPackageText->SetFullText("");

	fBarberPole->Stop();
	if (fProgressLayout->VisibleIndex() != kBarberPoleCard)
		fProgressLayout->SetVisibleItem(kBarberPoleCard);
}


void
WorkStatusView::SetProgress(int32 percent, const char* text,
	const char* package)
{
	fStatusText->SetFullText(text);
	fPackageText->SetFullText(package != NULL ? package : "");

	if (percent < 0) {
		fBarberPole->Start();
		if (fProgressLayout->VisibleIndex() != kBarberPoleCard)
			fProgressLayout->SetVisibleItem(kBarberPoleCard);
		return;
	}

	fBarberPole->Stop();
	fProgressBar->SetTo((float)percent / 100.0f);
	if (fProgressLayout->VisibleIndex() != kProgressBarCard)
		fProgressLayout->SetVisibleItem(kProgressBarCard);
}


void
WorkStatusView::SetPendingCount(int32 count)
{
	if (count <= 0) {
		fPendingText->SetFullText("");
		return;
	}

	BString text(B_TRANSLATE("%count% pending"));
	BString countText;
	countText << count;
	text.ReplaceFirst("%count%", countText);
	fPendingText->SetFullText(text.String());
}
