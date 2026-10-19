/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_FILTER_CATALOG_PRIVATE_H
#define _MEDIA2_FILTER_CATALOG_PRIVATE_H


#include <SupportDefs.h>


namespace BPrivate {
namespace media {


// Add-on hook: a user filter described by a backend element chain.
// kind is "video" or "audio".
status_t	RegisterCustomFilter(const char* id, const char* name,
				const char* kind, const char* description);


}	// namespace media
}	// namespace BPrivate


#endif	// _MEDIA2_FILTER_CATALOG_PRIVATE_H
