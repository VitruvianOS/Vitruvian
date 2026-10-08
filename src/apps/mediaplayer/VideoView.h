/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _VIDEO_VIEW_H
#define _VIDEO_VIEW_H


#include <Bitmap.h>
#include <Message.h>
#include <View.h>


// Receives B_PLAYER_VIDEO_FRAME from BMediaPlayer::SetVideoView and
// letterboxes the decoded frame into the current bounds.
class VideoView : public BView {
public:
								VideoView(BRect frame, const char* name,
									uint32 resizeMask = B_FOLLOW_ALL);
	virtual						~VideoView();

	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				MessageReceived(BMessage* message);

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
			void				_PresentFrame(const void* data, uint32 width,
									uint32 height, uint32 stride);
			void				_ClearFrame();

			uint32				fVideoWidth;
			uint32				fVideoHeight;
			BBitmap*			fBitmap;
};


#endif // _VIDEO_VIEW_H
