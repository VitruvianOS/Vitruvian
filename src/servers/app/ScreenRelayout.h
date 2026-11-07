/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SCREEN_RELAYOUT_H
#define _SCREEN_RELAYOUT_H


#include <Point.h>
#include <Rect.h>
#include <SupportDefs.h>


// Relayout policy for external screen changes. Pure so the unit test
// can reach it without a running app_server.

// True when the window should be moved on an external screen change.
bool	RelayoutShouldMove(bool isNormal, int32 feel, uint32 flags,
				const char* teamSignature);

enum relayout_anchor {
	RELAYOUT_CENTRED,
	RELAYOUT_START,
	RELAYOUT_END,
	RELAYOUT_RELATIVE
};

struct relayout_axis {
	relayout_anchor	anchor;
	float			value;
};

struct relayout_placement {
	bool			valid;
	relayout_axis	x;
	relayout_axis	y;
	BPoint			placedAt;
};

relayout_axis		RelayoutClassify(float start, float size,
						float screenStart, float screenSize);
float				RelayoutPlace(const relayout_axis& axis, float size,
						float screenStart, float screenSize);
relayout_placement	RelayoutCapture(const BRect& footprint,
						const BRect& screen);
BPoint				RelayoutOrigin(const relayout_placement& placement,
						const BRect& footprint, const BRect& screen);

bool	RelayoutFillsScreen(const BRect& frame, const BRect& screen);


#endif // _SCREEN_RELAYOUT_H
