/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Minimal GraphicsDefs stand-in so the fbdev format host test can build
 * without the full V\OS header stack. Values match headers/os/interface.
 */
#ifndef _GRAPHICS_DEFS_H
#define _GRAPHICS_DEFS_H


#include <stdint.h>


typedef uint8_t	uint8;
typedef uint16_t	uint16;
typedef uint32_t	uint32;
typedef int32_t	int32;


enum {
	B_RGB24		= 0x0003,
	B_RGB32		= 0x0008,
	B_RGBA32	= 0x2008,
};

typedef uint32 color_space;


#endif // _GRAPHICS_DEFS_H
