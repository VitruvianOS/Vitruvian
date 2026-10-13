/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _ACCOUNTS_WINDOW_H
#define _ACCOUNTS_WINDOW_H


#include <ObjectList.h>
#include <Window.h>

#include <Account.h>


class BButton;
class BListView;
class BStringView;


class AccountsWindow : public BWindow {
public:
								AccountsWindow();
	virtual						~AccountsWindow();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_AddAccount();
			void				_RemoveAccount();
			void				_EditAccount();
			void				_RequestToken();
			void				_Refresh();
			BAccount*			_SelectedAccount();

			BListView*			fList;
			BStringView*		fStatus;
			BButton*			fRemoveButton;
			BButton*			fEditButton;
			BButton*			fTokenButton;
			BObjectList<BAccount>	fAccounts;
};


#endif // _ACCOUNTS_WINDOW_H
