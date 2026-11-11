/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef OVERLAY_PLANE_H
#define OVERLAY_PLANE_H

#include <GraphicsDefs.h>
#include <Rect.h>
#include <SupportDefs.h>


// One claimed hardware overlay plane. Best plane available, first come
// first served; a failed acquire must fall back to CPU compositing.
class OverlayPlane {
public:
	virtual						~OverlayPlane() {}

	virtual	void*				Bits() const = 0;
	virtual	uint32				BytesPerRow() const = 0;
	virtual	uint32				Width() const = 0;
	virtual	uint32				Height() const = 0;
	virtual	color_space			ColorSpace() const = 0;

			// Rects are in screen pixels; scales the plane to destination.
	virtual	status_t			Configure(const BRect& source,
									const BRect& destination) = 0;
			// Unbind the plane from the CRTC, keeping the claim.
	virtual	void				Disable() = 0;
};

#endif // OVERLAY_PLANE_H
