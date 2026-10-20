/*
 * Copyright 2013,	Jérôme DUVAL.
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "EULAWindow.h"

#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <LayoutUtils.h>
#include <Roster.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>

#include "tracker_private.h"

static const uint32 kMsgAgree = 'agre';
static const uint32 kMsgNext = 'next';

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InstallerApp"


EULAWindow::EULAWindow()
	:
	BWindow(BRect(), B_TRANSLATE("README"), B_MODAL_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	BString infoText;
	infoText << B_TRANSLATE(
		"Welcome to the V\\OS Installer.\n\n");
	infoText << B_TRANSLATE(
		"V\\OS is beta software. Installing it can result in the loss of "
		"data, so please back up anything important before you continue.\n\n");
	infoText << B_TRANSLATE(
		"1)   Partitioning. Choose \"Guided partitioning\" to let the "
		"Installer prepare a disk for you, or \"Set up partitions\" to "
		"use DriveSetup directly. Partitions you change or format lose "
		"their contents.\n\n");
	infoText << B_TRANSLATE(
		"2)   Boot loader. The Installer installs GRUB for you and lets "
		"you choose UEFI, legacy BIOS or both. It does not add other "
		"operating systems to the boot menu.\n\n");
	infoText << B_TRANSLATE(
		"3)   Account. You will create the first user account during the "
		"installation. The Advanced tab holds swap, boot loader and "
		"root password options.");

	BTextView* textView = new BTextView("eula", be_plain_font, NULL, B_WILL_DRAW);
	textView->SetInsets(10, 10, 10, 10);
	textView->MakeEditable(false);
	textView->MakeSelectable(false);
	textView->SetText(infoText);

	BScrollView* scrollView = new BScrollView("eulaScroll",
		textView, B_WILL_DRAW, false, true);

	BButton* cancelButton = new BButton(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED));
	cancelButton->SetTarget(be_app);

	BButton* continueButton = new BButton(B_TRANSLATE("Continue"),
		new BMessage(kMsgAgree));
	continueButton->SetTarget(be_app);
	continueButton->MakeDefault(true);

	if (!be_roster->IsRunning(kTrackerSignature))
		SetWorkspaces(B_ALL_WORKSPACES);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(scrollView)
		.AddGroup(B_HORIZONTAL, B_USE_ITEM_SPACING)
			.AddGlue()
			.Add(cancelButton)
			.Add(continueButton);

	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);
	const float lineHeight = fontHeight.ascent + fontHeight.descent;
	GetLayout()->SetExplicitSize(BSize(be_plain_font->StringWidth("M") * 60, lineHeight * 30));
	CenterOnScreen();
	Show();
}


bool
EULAWindow::QuitRequested()
{
	be_app->PostMessage(kMsgNext);
	return true;
}
