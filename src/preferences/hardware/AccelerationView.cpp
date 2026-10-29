/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AccelerationView.h"

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <TabView.h>

#include "OverclockView.h"
#include "RenderingView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Hardware"


AccelerationView::AccelerationView()
	:
	BView("acceleration", B_WILL_DRAW)
{
	BTabView* tabView = new BTabView("acceleration tabs", B_WIDTH_FROM_LABEL);
	BTab* renderingTab = new BTab();
	tabView->AddTab(RenderingView::Create(), renderingTab);
	renderingTab->SetLabel(B_TRANSLATE("Graphics"));
	BTab* overclockTab = new BTab();
	tabView->AddTab(new OverclockView(), overclockTab);
	overclockTab->SetLabel(B_TRANSLATE("Overclocking"));

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(tabView)
		.SetInsets(0);
}


AccelerationView::~AccelerationView()
{
}
