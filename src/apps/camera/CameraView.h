/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */

#ifndef _CAMERA_VIEW_H
#define _CAMERA_VIEW_H


#include <Bitmap.h>
#include <Locker.h>
#include <View.h>


enum {
	// Posted to the window when the user taps the preview.
	kMsgCameraTap			= 'TAPV',
	kMsgCameraFrameShown	= 'FRAM',
	kMsgCameraFrameReshaped	= 'FRSH'
};


class CameraView : public BView {
public:
								CameraView(BRect frame, const char* name);
	virtual						~CameraView();

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint point);

	virtual	bool				HasHeightForWidth();
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);

			// Called on the capture thread; copies the frame and redraws.
			void				SetFrame(const uint8* bits, size_t size,
									uint32 width, uint32 height);

			void				SetZoom(float zoom);
			float				Zoom() const { return fZoom; }
			void				SetMirrored(bool mirrored);
			void				SetGridVisible(bool visible);

private:
			// Where the frame is drawn, before mirroring; fLock held.
			BRect				_Destination() const;

			BLocker				fLock;
			BBitmap*			fFrame;
			float				fAspect;
			float				fZoom;
			bool				fMirrored;
			bool				fGrid;
			BPoint				fTapPoint;
			bool				fHasTap;
};


#endif // _CAMERA_VIEW_H
