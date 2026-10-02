/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _SCREEN_RELAYOUT_H
#define _SCREEN_RELAYOUT_H


#include <Rect.h>
#include <SupportDefs.h>


// Relayout policy for external screen changes. Pure so the unit test
// can reach it without a running app_server.

// True when the window should be moved on an external screen change.
bool	RelayoutShouldMove(bool isNormal, int32 feel, uint32 flags,
				const char* teamSignature);

float	RelayoutAxis(float left, float frameSize,
				float oldScreenLeft, float oldScreenWidth,
				float newScreenLeft, float newScreenWidth);

BPoint	RelayoutOrigin(const BRect& frame, const BRect& oldFrame,
				const BRect& newFrame);

bool	RelayoutFillsScreen(const BRect& frame, const BRect& screen);


#endif // _SCREEN_RELAYOUT_H
