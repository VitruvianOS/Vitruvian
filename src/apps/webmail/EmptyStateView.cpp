/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "EmptyStateView.h"
#include "WebMailMessages.h"

#include <Button.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <SeparatorView.h>
#include <StringView.h>


namespace {


BStringView*
PaneTitle(const char* name, const char* text)
{
	BStringView* title = new BStringView(name, text);
	title->SetFont(be_bold_font);
	return title;
}


} // namespace


EmptyStateView::EmptyStateView()
	:
	BView("empty-state", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

	BStringView* heading = new BStringView("heading",
		"No mail account yet");
	heading->SetFont(be_bold_font);

	BStringView* body = new BStringView("body",
		"Add an account to see folders, messages and mail here.");

	BButton* addAccount = new BButton("add-account",
		"Add Account" B_UTF8_ELLIPSIS,
		new BMessage(MSG_ADD_ACCOUNT));

	BLayoutBuilder::Group<>(this, B_HORIZONTAL, 0)
		.AddGroup(B_VERTICAL, 0, 0.25f)
			.Add(PaneTitle("folders-title", "Folders"))
			.AddStrut(8)
			.Add(new BStringView("folders-empty", "No account yet"))
			.AddGlue()
			.SetInsets(12)
		.End()
		.Add(new BSeparatorView(B_VERTICAL), 0.0f)
		.AddGroup(B_VERTICAL, 0, 0.45f)
			.Add(PaneTitle("messages-title", "Messages"))
			.AddStrut(8)
			.AddGroup(B_HORIZONTAL, 0)
				.AddGlue()
				.AddGroup(B_VERTICAL, 0)
					.Add(heading)
					.AddStrut(4)
					.Add(body)
					.AddStrut(12)
					.Add(addAccount)
				.End()
				.AddGlue()
			.End()
			.AddGlue()
			.SetInsets(12)
		.End()
		.Add(new BSeparatorView(B_VERTICAL), 0.0f)
		.AddGroup(B_VERTICAL, 0, 0.30f)
			.Add(PaneTitle("reading-title", "Reading pane"))
			.AddStrut(8)
			.Add(new BStringView("reading-empty", "Nothing selected"))
			.AddGlue()
			.SetInsets(12)
		.End();
}
