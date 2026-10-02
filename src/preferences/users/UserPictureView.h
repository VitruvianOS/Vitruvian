/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef USER_PICTURE_VIEW_H
#define USER_PICTURE_VIEW_H


#include <Bitmap.h>
#include <View.h>


// People-style ID-photo preview: 70x90 frame, bilinear scaled, centred.
class UserPictureView : public BView {
public:
								UserPictureView();
	virtual						~UserPictureView();

			void				SetPicture(BBitmap* picture);
			BBitmap*			Picture() const { return fPicture; }
			void				Clear();

	virtual	void				Draw(BRect updateRect);

private:
			BBitmap*			fPicture;
};


#endif // USER_PICTURE_VIEW_H
