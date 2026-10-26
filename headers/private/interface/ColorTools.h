/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _COLOR_TOOLS_H
#define _COLOR_TOOLS_H


#include <GraphicsDefs.h>


namespace BPrivate {


static inline int32
perceptual_brightness(rgb_color color)
{
	return color.Brightness();
}


}	// namespace BPrivate


#endif	// _COLOR_TOOLS_H
