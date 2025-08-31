/*
 * Copyright 2021, Dario Casalinuovo
 * All rights reserved. Distributed under the terms of the GPL license.
 */
#ifndef DRM_BUFFER_H
#define DRM_BUFFER_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "modeset.h"
#include "RenderingBuffer.h"


#if DEBUG
	#define CALLED() 			printf("CALLED %s\n",__PRETTY_FUNCTION__)
#else
  	#define CALLED() 			((void)0)
#endif


class DrmBuffer : public RenderingBuffer {
public:
								DrmBuffer(int fd, modeset_dev* dev);
	virtual						~DrmBuffer();

	virtual	status_t			InitCheck() const;

	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

private:
			modeset_dev*		fDev;
			status_t			fErr;
			color_space			fColorSpace;

};

#endif
