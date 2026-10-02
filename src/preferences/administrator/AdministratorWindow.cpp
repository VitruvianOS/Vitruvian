/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "AdministratorWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>


using namespace BPrivate::Accounts;


static const uint32 kMsgUnlock = 'unlk';
static const uint32 kMsgAddUser = 'addu';
static const uint32 kMsgDeleteUser = 'delu';
static const uint32 kMsgGrantAdmin = 'grnt';
static const uint32 kMsgRevokeAdmin = 'rvke';
static const uint32 kMsgResetPassword = 'rset';
static const uint32 kMsgSelectionChanged = 'selc';
static const uint32 kMsgDeleteConfirmed = 'delf';
static const uint32 kMsgAddConfirmed = 'adcf';
static const uint32 kMsgPasswordConfirmed = 'pwcf';
static const uint32 kMsgGrantConfirmed = 'gncf';
static const uint32 kMsgRevokeConfirmed = 'rvcf';


static const char* kProtectedHint = "Live session account, cannot be changed.";


enum {
	kColumnUser = 0,
	kColumnFullName,
	kColumnType,
	kColumnSession,
	kColumnCount
};


class AddUserDialog : public BWindow {
public:
								AddUserDialog(BMessenger target);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Submit();

			BMessenger			fTarget;
			BTextControl*		fName;
			BTextControl*		fFullName;
			BTextControl*		fPassword;
			BTextControl*		fConfirm;
			BCheckBox*			fAdmin;
			BStringView*		fStatus;
};


class ResetPasswordDialog : public BWindow {
public:
								ResetPasswordDialog(const char* user,
									BMessenger target);

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_Submit();

			BMessenger			fTarget;
			BString				fUser;
			BTextControl*		fPassword;
			BTextControl*		fConfirm;
			BStringView*		fStatus;
};


// Owns only the string fields; the Account* stays with AccountList.
class AccountRow : public BRow {
public:
								AccountRow(Account* account);

			Account*			GetAccount() const { return fAccount; }

private:
			Account*			fAccount;
};


static int
_compareAccountName(const Account* a, const Account* b)
{
	return a->name.Compare(b->name.String());
}


// A field drawn in the disabled text colour, for protected accounts.
class AccountField : public BStringField {
public:
	AccountField(const char* string, bool dimmed)
		:
		BStringField(string),
		fDimmed(dimmed)
	{
	}

	bool IsDimmed() const { return fDimmed; }

private:
	bool fDimmed;
};


class AccountColumn : public BStringColumn {
public:
	AccountColumn(const char* title, float width, float minWidth,
			float maxWidth)
		:
		BStringColumn(title, width, minWidth, maxWidth, B_TRUNCATE_END)
	{
	}

	virtual void DrawField(BField* field, BRect rect, BView* parent)
	{
		AccountField* dim = dynamic_cast<AccountField*>(field);
		if (dim == NULL || !dim->IsDimmed()) {
			BStringColumn::DrawField(field, rect, parent);
			return;
		}
		rgb_color saved = parent->HighColor();
		parent->SetHighColor(tint_color(saved, B_DISABLED_LABEL_TINT));
		BStringColumn::DrawField(field, rect, parent);
		parent->SetHighColor(saved);
	}
};


AccountRow::AccountRow(Account* account)
	:
	fAccount(account)
{
	bool dim = account->isProtected;
	SetField(new AccountField(account->name.String(), dim), kColumnUser);
	SetField(new AccountField(account->realName.Length() > 0
		? account->realName.String() : "-", dim), kColumnFullName);
	SetField(new AccountField(account->isAdmin
		? "Administrator" : "Standard", dim), kColumnType);
	BString session(account->hasSession ? "Running" : "-");
	if (account->isSelf)
		session << "  (you)";
	SetField(new AccountField(session.String(), dim), kColumnSession);
}


AddUserDialog::AddUserDialog(BMessenger target)
	:
	BWindow(BRect(0, 0, 400, 300), "Add User",
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target)
{
	fName = new BTextControl("name", "Username:", "", NULL);
	fFullName = new BTextControl("fullname", "Full name:", "", NULL);
	fPassword = new BTextControl("password", "Password:", "", NULL);
	fPassword->TextView()->HideTyping(true);
	fConfirm = new BTextControl("confirm", "Confirm:", "", NULL);
	fConfirm->TextView()->HideTyping(true);
	fAdmin = new BCheckBox("admin",
		"Give this account administrator rights", NULL);
	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);

	BButton* ok = new BButton("ok", "Create",
		new BMessage(kMsgAddConfirmed));
	ok->MakeDefault(true);
	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fName)
		.Add(fFullName)
		.Add(fPassword)
		.Add(fConfirm)
		.Add(fAdmin)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();

	CenterOnScreen();
	fName->MakeFocus(true);
	Show();
}


void
AddUserDialog::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAddConfirmed:
			_Submit();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


void
AddUserDialog::_Submit()
{
	BString name(fName->Text());
	BString fullName(fFullName->Text());
	BString password(fPassword->Text());
	BString confirm(fConfirm->Text());

	name = name.Trim();
	if (name.Length() == 0) {
		fStatus->SetText("Username must not be empty.");
		return;
	}
	if (password.Length() == 0) {
		fStatus->SetText("Password must not be empty.");
		return;
	}
	if (password != confirm) {
		fStatus->SetText("Passwords do not match.");
		fPassword->SetText("");
		fConfirm->SetText("");
		fPassword->MakeFocus(true);
		return;
	}
	// Same floor as pwquality.conf so the UI and policy agree.
	if (password.Length() < 6) {
		fStatus->SetText("Password is too short (minimum 6 characters).");
		fPassword->MakeFocus(true);
		return;
	}

	BMessage reply(kMsgAddConfirmed);
	reply.AddString("name", name.String());
	reply.AddString("fullname", fullName.String());
	reply.AddString("password", password.String());
	reply.AddBool("admin", fAdmin->Value() == B_CONTROL_ON);
	fTarget.SendMessage(&reply);
	PostMessage(B_QUIT_REQUESTED);
}


ResetPasswordDialog::ResetPasswordDialog(const char* user, BMessenger target)
	:
	BWindow(BRect(0, 0, 380, 220), "Reset Password",
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fUser(user)
{
	BString heading;
	heading.SetToFormat("New password for %s:", user);
	fPassword = new BTextControl("password", heading.String(), "", NULL);
	fPassword->TextView()->HideTyping(true);
	fConfirm = new BTextControl("confirm", "Confirm:", "", NULL);
	fConfirm->TextView()->HideTyping(true);
	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);

	BButton* ok = new BButton("ok", "Set Password",
		new BMessage(kMsgPasswordConfirmed));
	ok->MakeDefault(true);
	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fPassword)
		.Add(fConfirm)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();

	CenterOnScreen();
	fPassword->MakeFocus(true);
	Show();
}


void
ResetPasswordDialog::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPasswordConfirmed:
			_Submit();
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


void
ResetPasswordDialog::_Submit()
{
	BString password(fPassword->Text());
	BString confirm(fConfirm->Text());

	if (password.Length() == 0) {
		fStatus->SetText("Password must not be empty.");
		return;
	}
	if (password != confirm) {
		fStatus->SetText("Passwords do not match.");
		fPassword->SetText("");
		fConfirm->SetText("");
		fPassword->MakeFocus(true);
		return;
	}
	if (password.Length() < 6) {
		fStatus->SetText("Password is too short (minimum 6 characters).");
		fPassword->MakeFocus(true);
		return;
	}

	BMessage reply(kMsgPasswordConfirmed);
	reply.AddString("user", fUser.String());
	reply.AddString("password", password.String());
	fTarget.SendMessage(&reply);
	PostMessage(B_QUIT_REQUESTED);
}


AdministratorWindow::AdministratorWindow()
	:
	BWindow(BRect(0, 0, 620, 440), "Administrator", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fUnlocked(false)
{
	BStringView* header = new BStringView("header",
		"Accounts on this V\\OS computer");
	header->SetFont(be_bold_font);

	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);

	// BColumnListView keeps column headers drawn even with zero rows;
	// a bare BListView leaves a blank white area in the layout.
	fList = new BColumnListView("accounts",
		B_WILL_DRAW | B_FRAME_EVENTS, B_FANCY_BORDER);
	fList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kMsgSelectionChanged));
	fList->SetInvocationMessage(new BMessage(kMsgSelectionChanged));
	fList->AddColumn(new AccountColumn("Username",
		120.0, 80.0, 200.0), kColumnUser);
	fList->AddColumn(new AccountColumn("Full name",
		160.0, 80.0, 280.0), kColumnFullName);
	fList->AddColumn(new AccountColumn("Type",
		110.0, 80.0, 180.0), kColumnType);
	fList->AddColumn(new AccountColumn("Session",
		120.0, 80.0, 200.0), kColumnSession);

	fUnlockButton = new BButton("unlock", "Unlock" B_UTF8_ELLIPSIS,
		new BMessage(kMsgUnlock));
	fAddButton = new BButton("add", "Add User" B_UTF8_ELLIPSIS,
		new BMessage(kMsgAddUser));
	fDeleteButton = new BButton("delete", "Delete User" B_UTF8_ELLIPSIS,
		new BMessage(kMsgDeleteUser));
	fGrantButton = new BButton("grant", "Grant Administrator",
		new BMessage(kMsgGrantAdmin));
	fRevokeButton = new BButton("revoke", "Revoke Administrator",
		new BMessage(kMsgRevokeAdmin));
	fResetPasswordButton = new BButton("resetpw",
		"Reset Password" B_UTF8_ELLIPSIS,
		new BMessage(kMsgResetPassword));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(header)
		.Add(fList, 1.0f)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fUnlockButton)
			.Add(fAddButton)
			.Add(fDeleteButton)
			.AddGlue()
			.Add(fResetPasswordButton)
			.Add(fGrantButton)
			.Add(fRevokeButton)
		.End()
		.Add(fStatus);

	CenterOnScreen();
	// The list is filled here, while locked; Unlock only enables edits.
	_Reload();
	_SetUnlocked(false);
	Show();
}


AdministratorWindow::~AdministratorWindow()
{
}


void
AdministratorWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgUnlock:
			_Unlock();
			break;
		case kMsgAddUser:
			_AddUser();
			break;
		case kMsgDeleteUser:
			_DeleteUser();
			break;
		case kMsgGrantAdmin:
			_GrantAdmin();
			break;
		case kMsgRevokeAdmin:
			_RevokeAdmin();
			break;
		case kMsgResetPassword:
			_ResetPassword();
			break;
		case kMsgSelectionChanged:
			_UpdateStatus();
			break;
		case kMsgDeleteConfirmed:
		{
			int32 which = 0;
			message->FindInt32("which", &which);
			if (which == 1)
				_DeleteConfirmed(false);
			else if (which == 2)
				_DeleteConfirmed(true);
			break;
		}
		case kMsgGrantConfirmed:
		{
			int32 which = 0;
			message->FindInt32("which", &which);
			if (which != 1)
				break;
			Account* account = _SelectedAccount();
			if (account == NULL) {
				_ShowError("Select an account first.");
				break;
			}
			StringList* args = new StringList;
			args->AddItem(new BString("grant-admin"));
			args->AddItem(new BString(account->name.String()));
			BString error;
			status_t status = runAdminHelper(args, NULL, error);
			delete args;
			if (status != B_OK)
				_ShowError(error.Length() > 0 ? error.String()
					: "Could not grant administrator rights.");
			_Reload();
			break;
		}
		case kMsgRevokeConfirmed:
		{
			int32 which = 0;
			message->FindInt32("which", &which);
			if (which != 1)
				break;
			Account* account = _SelectedAccount();
			if (account == NULL) {
				_ShowError("Select an account first.");
				break;
			}
			StringList* args = new StringList;
			args->AddItem(new BString("revoke-admin"));
			args->AddItem(new BString(account->name.String()));
			BString error;
			status_t status = runAdminHelper(args, NULL, error);
			delete args;
			if (status != B_OK)
				_ShowError(error.Length() > 0 ? error.String()
					: "Could not revoke administrator rights.");
			_Reload();
			break;
		}
		case kMsgAddConfirmed:
		{
			BString name, fullName, password;
			bool admin = false;
			message->FindString("name", &name);
			message->FindString("fullname", &fullName);
			message->FindString("password", &password);
			message->FindBool("admin", &admin);

			StringList* args = new StringList;
			args->AddItem(new BString("create"));
			args->AddItem(new BString(name.String()));
			if (fullName.Length() > 0) {
				BString flag;
				flag.SetToFormat("--full-name=%s", fullName.String());
				args->AddItem(new BString(flag.String()));
			}
			if (admin)
				args->AddItem(new BString("--admin"));
			BString error;
			status_t status = runAdminHelper(args, password.String(), error);
			delete args;
			if (status != B_OK)
				_ShowError(error.Length() > 0 ? error.String()
					: "Could not create the account.");
			_Reload();
			break;
		}
		case kMsgPasswordConfirmed:
		{
			BString user, password;
			message->FindString("user", &user);
			message->FindString("password", &password);
			if (user.Length() == 0 || password.Length() == 0)
				break;
			StringList* args = new StringList;
			args->AddItem(new BString("set-password"));
			args->AddItem(new BString(user.String()));
			BString error;
			status_t status = runAdminHelper(args, password.String(), error);
			delete args;
			if (status != B_OK)
				_ShowError(error.Length() > 0 ? error.String()
					: "Could not set the password.");
			_Reload();
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


bool
AdministratorWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


Account*
AdministratorWindow::_SelectedAccount()
{
	AccountRow* row = static_cast<AccountRow*>(fList->CurrentSelection());
	if (row == NULL)
		return NULL;
	return row->GetAccount();
}


void
AdministratorWindow::_Reload()
{
	fList->Clear();
	fAccounts.MakeEmpty();
	if (listAccounts(&fAccounts) != B_OK) {
		_ShowError("Could not read the account list.");
		_UpdateStatus();
		return;
	}
	fAccounts.SortItems(_compareAccountName);

	for (int32 i = 0; i < fAccounts.CountItems(); i++)
		fList->AddRow(new AccountRow(fAccounts.ItemAt(i)));
	_UpdateStatus();
}


void
AdministratorWindow::_UpdateStatus()
{
	if (fList->CountRows() > 0) {
		Account* selected = _SelectedAccount();
		if (selected != NULL && selected->isProtected) {
			fStatus->SetHighUIColor(B_SHINE_COLOR);
			fStatus->SetText(kProtectedHint);
			return;
		}
		if (!fUnlocked) {
			fStatus->SetHighUIColor(B_SHINE_COLOR);
			fStatus->SetText("Read only. Unlock to manage accounts "
				"(administrator password required).");
		} else {
			fStatus->SetHighUIColor(B_FAILURE_COLOR);
			fStatus->SetText("");
		}
		return;
	}

	fStatus->SetHighUIColor(B_SHINE_COLOR);
	if (!fUnlocked)
		fStatus->SetText("No accounts to show. Accounts with a login shell "
			"and a user ID of 1000 or higher are listed here. Unlock to "
			"add one (administrator password required).");
	else
		fStatus->SetText("No accounts to show. Accounts with a login shell "
			"and a user ID of 1000 or higher are listed here. Use Add User "
			"to create one.");
}


void
AdministratorWindow::_SetUnlocked(bool unlocked)
{
	fUnlocked = unlocked;
	fUnlockButton->SetEnabled(!unlocked);
	fAddButton->SetEnabled(unlocked);
	fDeleteButton->SetEnabled(unlocked);
	fGrantButton->SetEnabled(unlocked);
	fRevokeButton->SetEnabled(unlocked);
	fResetPasswordButton->SetEnabled(unlocked);
	_UpdateStatus();
}


void
AdministratorWindow::_ShowError(const char* text)
{
	if (text == NULL || text[0] == '\0')
		text = "The operation failed.";
	fStatus->SetHighUIColor(B_FAILURE_COLOR);
	fStatus->SetText(text);
	BAlert* alert = new BAlert("Administrator", text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


void
AdministratorWindow::_Confirm(const char* text, uint32 what)
{
	BAlert* alert = new BAlert("Administrator", text, "Cancel", "OK", NULL,
		B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->SetDefaultButton(alert->ButtonAt(1));
	alert->Go(new BInvoker(new BMessage(what), BMessenger(this)));
}


void
AdministratorWindow::_Unlock()
{
	StringList* args = new StringList;
	args->AddItem(new BString("unlock"));
	BString error;
	status_t status = runAdminHelper(args, NULL, error);
	delete args;
	if (status != B_OK) {
		_ShowError(error.Length() > 0 ? error.String()
			: "Authentication failed.");
		_SetUnlocked(false);
		return;
	}
	_SetUnlocked(true);
}


void
AdministratorWindow::_AddUser()
{
	if (!fUnlocked)
		return;
	new AddUserDialog(BMessenger(this));
}


void
AdministratorWindow::_DeleteUser()
{
	if (!fUnlocked)
		return;
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError("Select an account first.");
		return;
	}
	if (account->isProtected) {
		_ShowError(kProtectedHint);
		return;
	}
	if (account->isSelf) {
		_ShowError("You cannot delete the account you are signed in as.");
		return;
	}
	if (account->hasSession) {
		BString text;
		text.SetToFormat(
			"%s has a running session. Log that user out before "
			"deleting the account.", account->name.String());
		_ShowError(text.String());
		return;
	}

	BString text;
	text.SetToFormat("Delete account %s?\n\n"
		"Keep Home leaves the files on disk; Remove Home deletes them.",
		account->name.String());
	BAlert* alert = new BAlert("Administrator", text.String(),
		"Cancel", "Keep Home", "Remove Home",
		B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->SetDefaultButton(alert->ButtonAt(1));
	alert->Go(new BInvoker(new BMessage(kMsgDeleteConfirmed),
		BMessenger(this)));
}


void
AdministratorWindow::_GrantAdmin()
{
	if (!fUnlocked)
		return;
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError("Select an account first.");
		return;
	}
	if (account->isProtected) {
		_ShowError(kProtectedHint);
		return;
	}
	if (account->isAdmin) {
		_ShowError("That account is already an administrator.");
		return;
	}
	BString text;
	text.SetToFormat("Give administrator rights to %s?",
		account->name.String());
	_Confirm(text.String(), kMsgGrantConfirmed);
}


void
AdministratorWindow::_RevokeAdmin()
{
	if (!fUnlocked)
		return;
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError("Select an account first.");
		return;
	}
	if (account->isProtected) {
		_ShowError(kProtectedHint);
		return;
	}
	if (!account->isAdmin) {
		_ShowError("That account is not an administrator.");
		return;
	}
	if (account->isSelf) {
		_ShowError("You cannot remove your own administrator rights.");
		return;
	}
	BString text;
	text.SetToFormat("Remove administrator rights from %s?",
		account->name.String());
	_Confirm(text.String(), kMsgRevokeConfirmed);
}


void
AdministratorWindow::_ResetPassword()
{
	if (!fUnlocked)
		return;
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError("Select an account first.");
		return;
	}
	if (account->isProtected) {
		_ShowError(kProtectedHint);
		return;
	}
	new ResetPasswordDialog(account->name.String(), BMessenger(this));
}


void
AdministratorWindow::_DeleteConfirmed(bool removeHome)
{
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError("Select an account first.");
		return;
	}
	StringList* args = new StringList;
	args->AddItem(new BString("delete"));
	args->AddItem(new BString(account->name.String()));
	if (removeHome)
		args->AddItem(new BString("--remove-home"));
	BString error;
	status_t status = runAdminHelper(args, NULL, error);
	delete args;
	if (status != B_OK)
		_ShowError(error.Length() > 0 ? error.String()
			: "Could not delete the account.");
	_Reload();
}
