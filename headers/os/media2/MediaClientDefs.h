/*
 * Copyright 2025-2026, The Vitruvian Project. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_MEDIA_CLIENT_DEFS_H
#define _MEDIA2_MEDIA_CLIENT_DEFS_H


#include <SupportDefs.h>


typedef int32 media_client_id;
typedef int32 media_connection_id;


enum media_client_kinds {
	B_MEDIA_PLAYER          = 0x0001,
	B_MEDIA_RECORDER        = 0x0002,
	B_MEDIA_FILTER          = 0x0004,
	B_MEDIA_TIME_SOURCE     = 0x0008,
	B_MEDIA_CONTROLLABLE    = 0x0010,
	B_MEDIA_HARDWARE        = 0x0020,
	B_MEDIA_PHYSICAL_INPUT  = 0x0040,
	B_MEDIA_PHYSICAL_OUTPUT = 0x0080,
	B_MEDIA_VIRTUAL         = 0x0100
	// B_MEDIA_MIDI is added in Batch H; the legacy media_type enum in
	// <MediaDefs.h> already uses that name as a media-type value.
};


enum media_connection_kinds {
	B_MEDIA_INPUT  = 0x01,
	B_MEDIA_OUTPUT = 0x02
};


#endif // _MEDIA2_MEDIA_CLIENT_DEFS_H
