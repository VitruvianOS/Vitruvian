/*
 * Copyright 2019, Dario Casalinuovo
 * All rights reserved. Distributed under the terms of the GPL license.
 */
#ifndef ACCELERANT_BUFFER_H
#define ACCELERANT_BUFFER_H

#include "FBDevBuffer.h"

#include <linux/fb.h>

#include "RenderingBuffer.h"


class FBDevBuffer : public RenderingBuffer {
public:
								FBDevBuffer(int fd,
									struct fb_var_screeninfo vInfo,
									struct fb_fix_screeninfo);
	virtual						~FBDevBuffer();

	virtual	status_t			InitCheck() const;

	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

private:
			uint8_t*			fBuffer;
			struct fb_var_screeninfo fVInfo;
			struct fb_fix_screeninfo fInfo;
};

#endif // ACCELERANT_BUFFER_H
