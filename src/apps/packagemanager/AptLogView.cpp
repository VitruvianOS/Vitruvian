/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AptLogView.h"

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <ScrollView.h>
#include <TextView.h>
#include <Window.h>

#include "PackageManagerDefs.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AptLogView"


AptLogView::AptLogView()
	:
	Inherited("apt log", B_VERTICAL, B_USE_SMALL_SPACING)
{
	fTextView = new BTextView("log text");
	fTextView->MakeEditable(false);
	fTextView->SetWordWrap(false);
	fTextView->SetInsets(6, 6, 6, 6);
	fTextView->SetFontAndColor(be_fixed_font);

	fReloadButton = new BButton("reload", B_TRANSLATE("Reload"),
		new BMessage(kMsgReloadLog));

	BLayoutBuilder::Group<>(this)
		.Add(new BScrollView("log scroll", fTextView, 0, true, true,
			B_FANCY_BORDER))
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fReloadButton)
		.End();
}


AptLogView::~AptLogView()
{
}


void
AptLogView::AttachedToWindow()
{
	Inherited::AttachedToWindow();

	fReloadButton->SetTarget(Window());
}


void
AptLogView::SetLogText(const char* text)
{
	fTextView->SetText(text);
}
