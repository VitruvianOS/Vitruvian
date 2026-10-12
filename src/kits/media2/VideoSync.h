/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _VITRUVIAN_MEDIA2_VIDEO_SYNC_H
#define _VITRUVIAN_MEDIA2_VIDEO_SYNC_H


#include <support/SupportDefs.h>


namespace BPrivate { namespace media {


enum video_sync_decision {
	VIDEO_SYNC_DELIVER = 0,	// frame is due now
	VIDEO_SYNC_HOLD,		// early: keep the previous frame up
	VIDEO_SYNC_DROP		// late: skip this frame entirely
};


// Judge one frame against the playback clock. Only a real clock can
// declare a frame late: the video-only path paces itself from frame
// timestamps, so allowDrop is false there and lateness never drops.
inline video_sync_decision
DecideVideoSync(bigtime_t frameTime, bigtime_t clockNow,
	bigtime_t tolerance, bool allowDrop)
{
	if (frameTime > clockNow + tolerance)
		return VIDEO_SYNC_HOLD;
	if (allowDrop && frameTime < clockNow - tolerance)
		return VIDEO_SYNC_DROP;
	return VIDEO_SYNC_DELIVER;
}


}
}


#endif // _VITRUVIAN_MEDIA2_VIDEO_SYNC_H
