/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _VIDEO_VIEW_H
#define _VIDEO_VIEW_H


#include <View.h>


// Draw path and window-sizing seam for video. Frames arrive through the
// media2 track API once multi-track BMediaFile (W12) is in the tree; until
// then this view holds the video area and paints a placeholder.
class VideoView : public BView {
public:
								VideoView(BRect frame, const char* name,
									uint32 resizeMask = B_FOLLOW_ALL);
	virtual						~VideoView();

	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);

			void				SetVideoSize(uint32 width, uint32 height);
			uint32				VideoWidth() const
									{ return fVideoWidth; }
			uint32				VideoHeight() const
									{ return fVideoHeight; }
			bool				HasVideo() const
									{ return fVideoWidth > 0 && fVideoHeight > 0; }

			// Preferred window content size for the current video size.
			void				GetPreferredSize(float* width, float* height);

private:
			uint32				fVideoWidth;
			uint32				fVideoHeight;
};


#endif // _VIDEO_VIEW_H
