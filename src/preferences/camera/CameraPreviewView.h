/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef CAMERA_PREVIEW_VIEW_H
#define CAMERA_PREVIEW_VIEW_H


#include <View.h>

#include <media2/CameraSource.h>


class CameraPreviewView : public BView {
public:
								CameraPreviewView(const char* name);

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MessageReceived(BMessage* message);

			void				SetCamera(BCameraSource* camera);
			void				SetMirror(bool mirror);

private:
			BCameraSource*		fCamera;
			bool				fMirror;
			BLocker				fLock;
			BBitmap*			fBitmap;
};


#endif
