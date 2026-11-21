/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef OVERLAY_PLANE_H
#define OVERLAY_PLANE_H

#include <GraphicsDefs.h>
#include <Rect.h>
#include <SupportDefs.h>

class BRegion;


// Told how the buffers staged on a plane fared; serials come from Show().
class OverlayPlaneListener {
public:
	virtual						~OverlayPlaneListener() {}

			// On screen as of when.
	virtual	void				PlaneShown(uint64 serial, bigtime_t when) = 0;
			// Replaced before it reached the screen.
	virtual	void				PlaneDropped(uint64 serial) = 0;
			// The plane is gone; nothing more is reported.
	virtual	void				PlaneLost() = 0;
};


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

			// A plane scans out the buffer it was claimed with as index 0;
			// more of the same size and format join with AddBuffer().
	virtual	status_t			AddBuffer(int dmabufFd, uint32 width,
									uint32 height, uint32 fourcc,
									uint32 stride, uint32 sharedHandle,
									int32* _index, uint64 modifier = 0)
									{ return B_UNSUPPORTED; }
			// Shows a buffer; a nonzero serial is reported to the listener.
	virtual	status_t			Show(int32 index, uint64 serial,
									const BRect& source,
									const BRect& destination)
									{ return B_UNSUPPORTED; }
	virtual	void				SetListener(OverlayPlaneListener* listener) {}
			// The primary plane shows a frame composed on the GPU.
	virtual	bool				Composed() const { return false; }
			// Composed: the part of the destination that shows the buffer.
	virtual	status_t			SetVisible(const BRegion& region)
									{ return B_UNSUPPORTED; }
			// Composed: a new set of buffers follows, of another size; the
			// picture stays up until one of them is shown.
	virtual	status_t			Retarget() { return B_UNSUPPORTED; }
};

#endif // OVERLAY_PLANE_H
