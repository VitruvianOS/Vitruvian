/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _MEDIA2_VIDEO_VIEW_H
#define _MEDIA2_VIDEO_VIEW_H


#include <Bitmap.h>
#include <View.h>


class BMessage;


/*!	Reference consumer for BMediaPlayer::SetVideoView. B_PLAYER_VIDEO_FRAME
	carries a slot of the player's shared bitmap ring, which the view
	letterboxes without copying the pixels.
*/
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
			void				_ClearFrame();
			void				_ReleaseHeld();
			void				_DropRing();

			uint32				fVideoWidth;
			uint32				fVideoHeight;
			BBitmap*			fBitmap;
			void*				fRing;
			int32				fSlot;
			int64				fSequence;
			int64				fGeneration;
			bool				fHeld;
};


#endif // _MEDIA2_VIDEO_VIEW_H
