/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "AccessibilityWindow.h"

#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <SeparatorView.h>

#include "AccessibilityView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "AccessibilityWindow"


static const uint32 kMsgSetDefaults = 'dflt';
static const uint32 kMsgRevert = 'rvrt';


AccessibilityWindow::AccessibilityWindow()
	:
	BWindow(BRect(100, 100, 520, 640), B_TRANSLATE("Accessibility"),
		B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_NOT_RESIZABLE
		| B_AUTO_UPDATE_SIZE_LIMITS)
{
	fView = new AccessibilityView();

	fDefaultsButton = new BButton("defaults", B_TRANSLATE("Defaults"),
		new BMessage(kMsgSetDefaults), B_WILL_DRAW);
	fRevertButton = new BButton("revert", B_TRANSLATE("Revert"),
		new BMessage(kMsgRevert), B_WILL_DRAW);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(0, B_USE_DEFAULT_SPACING, 0, B_USE_DEFAULT_SPACING)
		.Add(fView)
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fDefaultsButton)
			.Add(fRevertButton)
			.SetInsets(B_USE_WINDOW_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, 0)
			.AddGlue();

	_UpdateButtons();

	CenterOnScreen();
}


AccessibilityWindow::~AccessibilityWindow()
{
}


bool
AccessibilityWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
AccessibilityWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSetDefaults:
			fView->SetDefaults();
			_UpdateButtons();
			break;

		case kMsgRevert:
			fView->Revert();
			_UpdateButtons();
			break;

		case kAccessibilitySettingsChanged:
			_UpdateButtons();
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
AccessibilityWindow::_UpdateButtons()
{
	fDefaultsButton->SetEnabled(fView->IsDefaultable());
	fRevertButton->SetEnabled(fView->IsRevertable());
}
