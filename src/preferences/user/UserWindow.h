/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef USER_WINDOW_H
#define USER_WINDOW_H


#include <String.h>
#include <Window.h>

#include "AccountUtil.h"


class BButton;
class BCheckBox;
class BStringView;
class BTextControl;
class UserPictureView;


class UserWindow : public BWindow {
public:
								UserWindow();
	virtual						~UserWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_ApplyRealName();
			void				_ChangePassword();
			void				_ToggleAutologin();
			void				_LoadAutologin();
			void				_ChoosePicture(BMessage* message);
			void				_ApplyPicture(const entry_ref* ref);
			void				_LoadPicture();
			void				_ShowError(const char* text);

			BString				fUserName;
			BStringView*		fHeader;
			BTextControl*		fRealName;
			BButton*			fChangePasswordButton;
			BCheckBox*			fAutologinBox;
			BButton*			fApplyButton;
			UserPictureView*	fPictureView;
			BButton*			fChoosePictureButton;
			BButton*			fClearPictureButton;
};


#endif // USER_WINDOW_H
