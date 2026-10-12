/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 * Fallback values for DRM uapi caps missing from older kernel headers.
 */
#ifndef _DRM_COMPAT_H
#define _DRM_COMPAT_H

#include <libdrm/drm.h>

#ifndef DRM_CAP_DUMB_BUFFER
#define DRM_CAP_DUMB_BUFFER			0x1
#endif

#ifndef DRM_CAP_CURSOR_WIDTH
#define DRM_CAP_CURSOR_WIDTH		0x8
#endif

#ifndef DRM_CAP_CURSOR_HEIGHT
#define DRM_CAP_CURSOR_HEIGHT		0x9
#endif

#ifndef DRM_CLIENT_CAP_UNIVERSAL_PLANES
#define DRM_CLIENT_CAP_UNIVERSAL_PLANES	2
#endif

#ifndef DRM_CLIENT_CAP_ATOMIC
#define DRM_CLIENT_CAP_ATOMIC		3
#endif

#endif // _DRM_COMPAT_H
