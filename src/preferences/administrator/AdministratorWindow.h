/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 */
#ifndef ADMINISTRATOR_WINDOW_H
#define ADMINISTRATOR_WINDOW_H


#include <String.h>
#include <Window.h>

#include "AccountUtil.h"


class BButton;
class BColumnListView;
class BStringView;


class AdministratorWindow : public BWindow {
public:
								AdministratorWindow();
	virtual						~AdministratorWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Reload();
			void				_SetUnlocked(bool unlocked);
			void				_ShowError(const char* text);
			void				_Confirm(const char* text, uint32 what);
			void				_UpdateStatus();

			void				_Unlock();
			void				_AddUser();
			void				_DeleteUser();
			void				_GrantAdmin();
			void				_RevokeAdmin();
			void				_ResetPassword();
			void				_DeleteConfirmed(bool removeHome);

			BPrivate::Accounts::Account* _SelectedAccount();

			bool				fUnlocked;
			BPrivate::Accounts::AccountList fAccounts;
			BStringView*		fStatus;
			BColumnListView*	fList;
			BButton*			fUnlockButton;
			BButton*			fAddButton;
			BButton*			fDeleteButton;
			BButton*			fGrantButton;
			BButton*			fRevokeButton;
			BButton*			fResetPasswordButton;
};


#endif // ADMINISTRATOR_WINDOW_H
