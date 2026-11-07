/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _POINTER_GRAB_H
#define _POINTER_GRAB_H


// Public native pointer control: grabs (with optional confinement and
// relative mode) for the X server's XGrabPointer/XGrabKeyboard and for
// native games. Grabs are window-scoped and owned by the calling team.


#include <InterfaceDefs.h>
#include <Point.h>
#include <Rect.h>
#include <SupportDefs.h>


class BWindow;


// Pointer grab flags. The first two double as the device-side
// pointer-constraint bits; input_server reads the same values.
enum {
	B_POINTER_GRAB_CONFINED		= 1 << 0,
		// the cursor may not leave confineTo
	B_POINTER_GRAB_RELATIVE		= 1 << 1,
		// motion is delta-only; for FPS mouse-look and XI2 relative
		// pointers. The server keeps the cursor on its lock point.
};


// The grab routes every pointer (keyboard) event to `window` even when
// it is not focused or under the cursor. Only the calling team can
// release; the grab also drops when the window or the team goes away.
// VT switch chords and the secure-attention dialog always win over a
// keyboard grab.
status_t	acquire_pointer_grab(BWindow* window, uint32 flags,
				const BRect& confineTo);
status_t	release_pointer_grab();
status_t	acquire_keyboard_grab(BWindow* window, uint32 flags = 0);
status_t	release_keyboard_grab();

// Warp the cursor, honouring a confinement rect (this is what
// XWarpPointer and game menus use). Relative-mode grabs ignore the
// lock point: warping is positional by definition.
void		warp_pointer(const BPoint& where);


#endif // _POINTER_GRAB_H
