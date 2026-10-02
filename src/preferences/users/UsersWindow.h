/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef USERS_WINDOW_H
#define USERS_WINDOW_H


#include <String.h>
#include <Window.h>

#include "AccountUtil.h"


class BButton;
class BCardLayout;
class BCheckBox;
class BColumnListView;
class BStringView;
class BTextControl;
class UserPictureView;


class UsersWindow : public BWindow {
public:
								UsersWindow();
	virtual						~UsersWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Reload();
			void				_ShowError(const char* text);
			void				_Confirm(const char* text, uint32 what);
			void				_UpdateStatus();

			bool				_Authenticate();
			void				_AddUser();
			void				_DeleteUser();
			void				_GrantAdmin();
			void				_RevokeAdmin();
			void				_ResetPassword();
			void				_DeleteConfirmed(bool removeHome);

			BPrivate::Accounts::Account* _SelectedAccount();

			void				_ShowEditor(bool self);
			void				_LoadSelf();
			void				_ApplyRealName();
			void				_ChangePassword();
			void				_ToggleAutologin();
			void				_LoadAutologin();
			void				_ChoosePicture();
			void				_ApplyPicture(const entry_ref* ref);
			void				_LoadPicture();

			bool				fAuthenticated;
			BPrivate::Accounts::AccountList fAccounts;
			BStringView*		fStatus;
			BColumnListView*	fList;
			BButton*			fAddButton;
			BButton*			fDeleteButton;
			BButton*			fGrantButton;
			BButton*			fRevokeButton;
			BButton*			fResetPasswordButton;

			BCardLayout*		fCards;
			BString				fSelfName;
			BStringView*		fSelfHeader;
			BTextControl*		fRealName;
			BButton*			fApplyButton;
			BButton*			fChangePasswordButton;
			BCheckBox*			fAutologinBox;
			UserPictureView*	fPictureView;
			BButton*			fChoosePictureButton;
			BButton*			fClearPictureButton;
};


#endif // USERS_WINDOW_H
