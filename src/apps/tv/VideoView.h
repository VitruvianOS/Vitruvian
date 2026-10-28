/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef TV_VIDEO_VIEW_H
#define TV_VIDEO_VIEW_H


#include <Bitmap.h>
#include <View.h>


class VideoView : public BView {
public:
								VideoView(BRect frame, const char* name,
									uint32 resizeMask, uint32 flags);
	virtual						~VideoView();

			void				SetCapturing(bool capturing);
			void				DrawFrame(const void* data, size_t size,
									uint32 width, uint32 height);

private:
	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);

			bool				fCapturing;
			BBitmap*			fFrame;
			uint32				fWidth;
			uint32				fHeight;
};


#endif
