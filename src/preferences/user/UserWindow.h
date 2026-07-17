/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>. Distributed under the terms of the
 * MIT License.
 */
#ifndef USER_WINDOW_H
#define USER_WINDOW_H


#include <String.h>
#include <Window.h>


class BButton;
class BCheckBox;
class BStringView;
class BTextControl;


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

			BString				fUserName;
			BStringView*		fHeader;
			BTextControl*		fRealName;
			BButton*			fChangePasswordButton;
			BCheckBox*			fAutologinBox;
			BButton*			fApplyButton;
};


#endif // USER_WINDOW_H
