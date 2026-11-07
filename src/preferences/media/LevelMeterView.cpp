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
	fBar = Bounds().InsetByCopy(1.0f, 1.0f);
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
	if (!fBar.IsValid())
		_RecomputeBar();

	const rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(panel);
	FillRect(Bounds());
	SetHighColor(tint_color(panel, B_DARKEN_3_TINT));
	StrokeRect(Bounds());
	SetHighColor(make_color(24, 24, 24));
	FillRect(fBar);

	float db = fPeak > 0.0001f ? 20.0f * std::log10(fPeak) : fMinDB;
	const float norm = DBToNorm(db, fMinDB, fMaxDB);
	if (norm <= 0.0f)
		return;

	const float greenMax = DBToNorm(-12.0f, fMinDB, fMaxDB);
	const float yellowMax = DBToNorm(-3.0f, fMinDB, fMaxDB);
	const rgb_color green = make_color(116, 224, 0);
	const rgb_color yellow = make_color(255, 200, 0);
	const rgb_color red = make_color(255, 80, 0);

	// Lit segments with a one pixel gap, coloured by their level.
	const float length = fVertical ? fBar.Height() : fBar.Width();
	const float step = 4.0f;
	const int32 count = std::max((int32)1, (int32)(length / step));
	const int32 lit = (int32)ceilf(norm * count);
	for (int32 i = 0; i < lit; i++) {
		const float level = (i + 0.5f) / count;
		SetHighColor(level > yellowMax ? red
			: level > greenMax ? yellow : green);
		BRect r = fBar;
		if (fVertical) {
			r.bottom = fBar.bottom - i * step;
			r.top = r.bottom - (step - 2.0f);
		} else {
			r.left = fBar.left + i * step;
			r.right = r.left + (step - 2.0f);
		}
		FillRect(r);
	}
}
