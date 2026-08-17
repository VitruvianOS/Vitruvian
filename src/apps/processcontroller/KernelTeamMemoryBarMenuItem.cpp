/*
 * Copyright 2025-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KernelTeamMemoryBarMenuItem.h"

#include "Colors.h"
#include "MemoryBarMenu.h"
#include "ProcessController.h"

#include <syscalls.h>

#include <float.h>
#include <stdio.h>

#include <Catalog.h>
#include <ControlLook.h>
#include <StringForSize.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ProcessController"


KernelTeamMemoryBarMenuItem::KernelTeamMemoryBarMenuItem(system_info& systemInfo,
	BBitmap* icon)
	: IconMenuItem(icon, B_TRANSLATE("Kernel Team"), NULL, true, false)
{
	fLastFootprint = -1;
	fGrenze1 = -1;
	fPhysicalMemory = (int64)systemInfo.max_pages * B_PAGE_SIZE / 1024LL;

	uint64 bytes = 0;
	if (_kern_get_kernel_memory_footprint(&bytes) != B_OK)
		bytes = 0;
	fFootprint = bytes / 1024;
}


void
KernelTeamMemoryBarMenuItem::DrawContent()
{
	DrawIcon();
	DrawBar(true);

	BPoint loc = ContentLocation();
	loc.x += ceilf(be_control_look->DefaultLabelSpacing() * 3.3f);
	Menu()->MovePenTo(loc);
	BMenuItem::DrawContent();
}


void
KernelTeamMemoryBarMenuItem::UpdateSituation()
{
	uint64 bytes = 0;
	if (_kern_get_kernel_memory_footprint(&bytes) != B_OK)
		bytes = 0;
	fFootprint = bytes / 1024;
	DrawBar(false);
}


void
KernelTeamMemoryBarMenuItem::DrawBar(bool force)
{
	bool selected = IsSelected();
	BRect frame = Frame();
	BMenu* menu = Menu();
	rgb_color highColor = menu->HighColor();

	BFont font;
	menu->GetFont(&font);
	const float margin = font.Size();
	BRect cadre = bar_rect(frame, &font);

	if (fLastFootprint < 0)
		force = true;
	if (force) {
		if (selected)
			menu->SetHighColor(gFrameColorSelected);
		else
			menu->SetHighColor(gFrameColor);
		menu->StrokeRect(cadre);
	}
	cadre.InsetBy(1, 1);
	BRect r = cadre;

	double grenze1 = cadre.left + (cadre.right - cadre.left)
						* fFootprint / fPhysicalMemory;
	if (grenze1 > cadre.right)
		grenze1 = cadre.right;

	r.right = grenze1;
	if (!force)
		r.left = fGrenze1;
	if (r.left < r.right) {
		if (selected)
			menu->SetHighColor(gKernelColorSelected);
		else
			menu->SetHighColor(gKernelColor);
		menu->FillRect(r);
	}

	r.left = grenze1;
	r.right = cadre.right;
	if (!force)
		r.right = fGrenze1;
	if (r.left < r.right) {
		if (selected)
			menu->SetHighColor(gWhiteSelected);
		else
			menu->SetHighColor(kWhite);
		menu->FillRect(r);
	}

	menu->SetHighColor(highColor);
	fGrenze1 = grenze1;

	if (force || fFootprint != fLastFootprint) {
		if (selected) {
			menu->SetLowColor(gMenuBackColorSelected);
			menu->SetHighColor(gMenuBackColorSelected);
		} else {
			menu->SetLowColor(gMenuBackColor);
			menu->SetHighColor(gMenuBackColor);
		}
		BRect trect(cadre.left - margin - gMemoryTextWidth, frame.top,
			cadre.left - margin, frame.bottom);
		menu->FillRect(trect);
		menu->SetHighColor(highColor);

		char infos[128];
		BPoint loc(cadre.left, cadre.bottom + 1);

		string_for_size(fFootprint * 1024.0, infos, sizeof(infos));
		loc.x -= margin + menu->StringWidth(infos);
		menu->DrawString(infos, loc);

		fLastFootprint = fFootprint;
	}
}


void
KernelTeamMemoryBarMenuItem::GetContentSize(float* _width, float* _height)
{
	IconMenuItem::GetContentSize(_width, _height);

	BFont font;
	Menu()->GetFont(&font);
	*_width += ceilf(be_control_look->DefaultLabelSpacing() * 2.0f)
		+ kBarWidth + font.Size() + gMemoryTextWidth;
}
