/*
 * Copyright 2015-2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_CLIENT_DEFS_H
#define _MEDIA2_MEDIA_CLIENT_DEFS_H


#include <SupportDefs.h>

#include <media2/MediaDefs.h>


typedef int32 media_client_id;
typedef int32 media_connection_id;


typedef uint32 media_type_mask;

#define B_MEDIA_TYPE_BIT(type) ((media_type_mask)1 << (type))


#define B_MEDIA_ANY_TYPE  ((media_type_mask)~0)
#define B_MEDIA_NO_TYPES  ((media_type_mask)0)


#define B_MEDIA_LIVE_TYPES \
	(B_MEDIA_TYPE_BIT(B_MEDIA_RAW_AUDIO) \
	| B_MEDIA_TYPE_BIT(B_MEDIA_RAW_VIDEO) \
	| B_MEDIA_TYPE_BIT(B_MEDIA_MIDI) \
	| B_MEDIA_TYPE_BIT(B_MEDIA_PARAMETERS))


enum media_client_kinds {
	B_MEDIA_PLAYER          = 0x0001,
	B_MEDIA_RECORDER        = 0x0002,
	B_MEDIA_FILTER          = B_MEDIA_PLAYER | B_MEDIA_RECORDER,

	B_MEDIA_CONTROLLABLE    = 0x0010,

	B_MEDIA_PHYSICAL_INPUT  = 0x0040,
	B_MEDIA_PHYSICAL_OUTPUT = 0x0080,
	B_MEDIA_VIRTUAL         = 0x0100
};


enum media_connection_kinds {
	B_MEDIA_INPUT  = 0x01,
	B_MEDIA_OUTPUT = 0x02
};


#endif
