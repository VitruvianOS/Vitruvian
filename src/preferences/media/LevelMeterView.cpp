/*
 * Copyright 2026, Vitruvian Project.
 * Distributed under the terms of the MIT License.
 */

#include "LevelMeterView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <ControlLook.h>
#include <View.h>


LevelMeterView::LevelMeterView(const char* name, float minDB, float maxDB,
		bool vertical)
	:
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_PULSE_NEEDED),
	fMinDB(minDB),
	fMaxDB(maxDB),
	fVertical(vertical),
	fPeak(0.0f),
	fDecayRate(0.85f)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}


LevelMeterView::~LevelMeterView()
{
}


void
LevelMeterView::SetPeak(float linear)
{
	if (linear < 0.0f) linear = 0.0f;
	if (linear > 1.0f) linear = 1.0f;
	if (std::fabs(linear - fPeak) > 0.005f) {
		fPeak = linear;
		Invalidate();
	}
}


void
LevelMeterView::SetDecayRate(float perSecond)
{
	fDecayRate = perSecond;
}


void
LevelMeterView::Pulse()
{
	if (fPeak <= 0.0f)
		return;

	float newPeak = fPeak * fDecayRate;
	if (newPeak < 0.001f)
		newPeak = 0.0f;
	if (newPeak != fPeak) {
		fPeak = newPeak;
		Invalidate();
	}
}


void
LevelMeterView::FrameResized(float, float)
{
	_RecomputeBar();
	Invalidate();
}


void
LevelMeterView::_RecomputeBar()
{

	BRect b = Bounds().InsetByCopy(2.0f, 2.0f);
	fBar = b;
}


static float
DBToNorm(float db, float minDB, float maxDB)
{
	if (db <= minDB) return 0.0f;
	if (db >= maxDB) return 1.0f;
	return (db - minDB) / (maxDB - minDB);
}


void
LevelMeterView::Draw(BRect)
{
	if (fBar.IsValid() == false)
		_RecomputeBar();


	SetHighColor(tint_color(LowColor(), B_DARKEN_2_TINT));
	FillRect(fBar);


	float db = fPeak > 0.0001f ? 20.0f * std::log10(fPeak) : fMinDB;
	float norm = DBToNorm(db, fMinDB, fMaxDB);
	if (norm <= 0.0f)
		return;


	const float greenMax  = DBToNorm(-6.0f, fMinDB, fMaxDB);
	const float yellowMax = DBToNorm(-3.0f, fMinDB, fMaxDB);

	BRect fill = fBar;
	if (fVertical) {
		float h = fBar.Height() * norm;
		fill.top = fBar.bottom - h;
	} else {
		float w = fBar.Width() * norm;
		fill.right = fBar.left + w;
	}


	auto seg = [&](float loN, float hiN, rgb_color c) {
		if (hiN <= loN) return;
		BRect r = fBar;
		if (fVertical) {
			float top = fBar.bottom - fBar.Height() * hiN;
			float bot = fBar.bottom - fBar.Height() * loN;
			r.Set(fBar.left, top, fBar.right, bot);
		} else {
			r.Set(fBar.left + fBar.Width() * loN, fBar.top,
				fBar.left + fBar.Width() * hiN, fBar.bottom);
		}
		SetHighColor(c);
		FillRect(r);
	};

	rgb_color green = { 116, 224, 0, 255 };
	rgb_color yellow = { 255, 200, 0, 255 };
	rgb_color red    = { 255, 80,  0, 255 };

	seg(0.0f,   std::min(norm, greenMax),  green);
	if (norm > greenMax)
		seg(greenMax, std::min(norm, yellowMax), yellow);
	if (norm > yellowMax)
		seg(yellowMax, norm, red);
}
