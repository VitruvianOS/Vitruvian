/*
 * Copyright 2021, Dario Casalinuovo
 * All rights reserved. Distributed under the terms of the GPL license.
 */
#ifndef DRM_BUFFER_H
#define DRM_BUFFER_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "RenderingBuffer.h"


#if DEBUG
	#define CALLED() 			printf("CALLED %s\n",__PRETTY_FUNCTION__)
#else
  	#define CALLED() 			((void)0)
#endif


class DrmDevice {
public:
	uint32*			buffer;
	uint32			conn_id;
	uint32			enc_id;
	uint32			crtc_id;
	uint32			fb_id;
	uint32			width;
	uint32			height;
	uint32			pitch;
	uint32			size;
	uint32			handle;

	drmModeModeInfo mode;
	drmModeCrtc*	crtc;

	DrmDevice*		next;
};


class DrmBuffer : public RenderingBuffer {
public:
								DrmBuffer(int fd, DrmDevice* dev);
	virtual						~DrmBuffer();

	virtual	status_t			InitCheck() const;

	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

private:
			DrmDevice*			fDev;
			status_t			fErr;
			color_space			fColorSpace;

};

#endif
