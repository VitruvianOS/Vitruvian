/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "UsersWindow.h"

#include "PasswordDialog.h"
#include "UserPictureView.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Alert.h>
#include <Application.h>
#include <BitmapStream.h>
#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <File.h>
#include <FilePanel.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <NodeInfo.h>
#include <Path.h>
#include <StringView.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Users"


using namespace BPrivate::Accounts;


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
static const uint32 kMsgChangePassword = 'cpwd';
static const uint32 kMsgApplyRealName = 'aply';
static const uint32 kMsgAutologinToggled = 'atgl';
static const uint32 kMsgChoosePicture = 'picp';
static const uint32 kMsgClearPicture = 'picc';
static const uint32 kMsgPictureChosen = 'picv';


#define kProtectedHint B_TRANSLATE("Live session account, cannot be changed.")


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


class ImageRefFilter : public BRefFilter {
public:
	virtual bool Filter(const entry_ref* ref, BNode* node,
		struct stat_beos* stat, const char* mimeType)
	{
		(void)stat;
		if (ref == NULL)
			return false;
		if (mimeType != NULL && mimeType[0] != '\0')
			return strncmp(mimeType, "image/", 6) == 0;
		if (node != NULL) {
			BNodeInfo info(node);
			if (info.InitCheck() == B_OK) {
				char type[B_MIME_TYPE_LENGTH];
				if (info.GetType(type) == B_OK)
					return strncmp(type, "image/", 6) == 0;
			}
		}
		return true;
	}
};


// Owns only the string fields; the Account* stays with AccountList.
class AccountRow : public BRow {
public:
								AccountRow(Account* account);
								AccountRow(const char* mask);

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
		? B_TRANSLATE("Administrator") : B_TRANSLATE("Standard"), dim),
		kColumnType);
	BString session(account->hasSession ? B_TRANSLATE("Running") : "-");
	if (account->isSelf)
		session << B_TRANSLATE("  (you)");
	SetField(new AccountField(session.String(), dim), kColumnSession);
}


// A greyed stand-in that hides other accounts until authentication.
AccountRow::AccountRow(const char* mask)
	:
	fAccount(NULL)
{
	SetField(new AccountField(mask, true), kColumnUser);
	SetField(new AccountField(mask, true), kColumnFullName);
	SetField(new AccountField("", true), kColumnType);
	SetField(new AccountField("", true), kColumnSession);
}


AddUserDialog::AddUserDialog(BMessenger target)
	:
	BWindow(BRect(0, 0, 400, 300), B_TRANSLATE("Add User"),
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target)
{
	fName = new BTextControl("name", B_TRANSLATE("Username:"), "", NULL);
	fFullName = new BTextControl("fullname", B_TRANSLATE("Full name:"), "",
		NULL);
	fPassword = new BTextControl("password", B_TRANSLATE("Password:"), "",
		NULL);
	fPassword->TextView()->HideTyping(true);
	fConfirm = new BTextControl("confirm", B_TRANSLATE("Confirm:"), "", NULL);
	fConfirm->TextView()->HideTyping(true);
	fAdmin = new BCheckBox("admin",
		B_TRANSLATE("Give this account administrator rights"), NULL);
	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);

	BButton* ok = new BButton("ok", B_TRANSLATE("Create"),
		new BMessage(kMsgAddConfirmed));
	ok->MakeDefault(true);
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
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
		fStatus->SetText(B_TRANSLATE("Username must not be empty."));
		return;
	}
	if (password.Length() == 0) {
		fStatus->SetText(B_TRANSLATE("Password must not be empty."));
		return;
	}
	if (password != confirm) {
		fStatus->SetText(B_TRANSLATE("Passwords do not match."));
		fPassword->SetText("");
		fConfirm->SetText("");
		fPassword->MakeFocus(true);
		return;
	}
	// Same floor as pwquality.conf so the UI and policy agree.
	if (password.Length() < 6) {
		fStatus->SetText(
			B_TRANSLATE("Password is too short (minimum 6 characters)."));
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
	BWindow(BRect(0, 0, 380, 220), B_TRANSLATE("Reset Password"),
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fUser(user)
{
	BString heading;
	heading.SetToFormat(B_TRANSLATE("New password for %s:"), user);
	fPassword = new BTextControl("password", heading.String(), "", NULL);
	fPassword->TextView()->HideTyping(true);
	fConfirm = new BTextControl("confirm", B_TRANSLATE("Confirm:"), "", NULL);
	fConfirm->TextView()->HideTyping(true);
	fStatus = new BStringView("status", "");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);

	BButton* ok = new BButton("ok", B_TRANSLATE("Set Password"),
		new BMessage(kMsgPasswordConfirmed));
	ok->MakeDefault(true);
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
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
		fStatus->SetText(B_TRANSLATE("Password must not be empty."));
		return;
	}
	if (password != confirm) {
		fStatus->SetText(B_TRANSLATE("Passwords do not match."));
		fPassword->SetText("");
		fConfirm->SetText("");
		fPassword->MakeFocus(true);
		return;
	}
	if (password.Length() < 6) {
		fStatus->SetText(
			B_TRANSLATE("Password is too short (minimum 6 characters)."));
		fPassword->MakeFocus(true);
		return;
	}

	BMessage reply(kMsgPasswordConfirmed);
	reply.AddString("user", fUser.String());
	reply.AddString("password", password.String());
	fTarget.SendMessage(&reply);
	PostMessage(B_QUIT_REQUESTED);
}


UsersWindow::UsersWindow()
	:
	BWindow(BRect(0, 0, 620, 520), B_TRANSLATE("Users"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
	fAuthenticated(false)
{
	BStringView* header = new BStringView("header",
		B_TRANSLATE("Accounts on this V\\OS computer"));
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
	fList->AddColumn(new AccountColumn(B_TRANSLATE("Username"),
		120.0, 80.0, 200.0), kColumnUser);
	fList->AddColumn(new AccountColumn(B_TRANSLATE("Full name"),
		160.0, 80.0, 280.0), kColumnFullName);
	fList->AddColumn(new AccountColumn(B_TRANSLATE("Type"),
		110.0, 80.0, 180.0), kColumnType);
	fList->AddColumn(new AccountColumn(B_TRANSLATE("Session"),
		120.0, 80.0, 200.0), kColumnSession);

	fAddButton = new BButton("add", B_TRANSLATE("Add User" B_UTF8_ELLIPSIS),
		new BMessage(kMsgAddUser));
	fDeleteButton = new BButton("delete",
		B_TRANSLATE("Delete User" B_UTF8_ELLIPSIS),
		new BMessage(kMsgDeleteUser));
	fGrantButton = new BButton("grant", B_TRANSLATE("Grant Administrator"),
		new BMessage(kMsgGrantAdmin));
	fRevokeButton = new BButton("revoke", B_TRANSLATE("Revoke Administrator"),
		new BMessage(kMsgRevokeAdmin));
	fResetPasswordButton = new BButton("resetpw",
		B_TRANSLATE("Reset Password" B_UTF8_ELLIPSIS),
		new BMessage(kMsgResetPassword));

	struct passwd* pw = getpwuid(getuid());
	if (pw != NULL && pw->pw_name != NULL)
		fSelfName = pw->pw_name;

	fSelfHeader = new BStringView("selfheader", "");
	fSelfHeader->SetFont(be_bold_font);
	fRealName = new BTextControl("realname", B_TRANSLATE("Full name:"), "",
		new BMessage(kMsgApplyRealName));
	fApplyButton = new BButton("apply", B_TRANSLATE("Apply full name"),
		new BMessage(kMsgApplyRealName));
	fChangePasswordButton = new BButton("passwd",
		B_TRANSLATE("Change password" B_UTF8_ELLIPSIS),
		new BMessage(kMsgChangePassword));
	fAutologinBox = new BCheckBox("autologin",
		B_TRANSLATE("Log in automatically at boot"),
		new BMessage(kMsgAutologinToggled));
	fPictureView = new UserPictureView();
	fChoosePictureButton = new BButton("choosepic",
		B_TRANSLATE("Choose picture" B_UTF8_ELLIPSIS),
		new BMessage(kMsgChoosePicture));
	fClearPictureButton = new BButton("clearpic", B_TRANSLATE("Clear"),
		new BMessage(kMsgClearPicture));

	BGroupView* adminPanel = new BGroupView(B_HORIZONTAL,
		B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(adminPanel)
		.Add(fDeleteButton)
		.AddGlue()
		.Add(fResetPasswordButton)
		.Add(fGrantButton)
		.Add(fRevokeButton);

	BGroupView* selfPanel = new BGroupView(B_VERTICAL, B_USE_ITEM_SPACING);
	BLayoutBuilder::Group<>(selfPanel)
		.Add(fSelfHeader)
		.AddGroup(B_HORIZONTAL, B_USE_ITEM_SPACING)
			.Add(fPictureView)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.Add(fRealName)
				.AddGroup(B_HORIZONTAL)
					.Add(fChoosePictureButton)
					.Add(fClearPictureButton)
					.AddGlue()
					.Add(fApplyButton)
				.End()
				.AddGlue()
			.End()
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_ITEM_SPACING)
			.Add(fChangePasswordButton)
			.Add(fAutologinBox)
			.AddGlue()
		.End();

	BView* cards = new BView("cards", 0);
	fCards = new BCardLayout();
	cards->SetLayout(fCards);
	fCards->AddView(adminPanel);
	fCards->AddView(selfPanel);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(header)
		.Add(fList, 1.0f)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fAddButton)
			.AddGlue()
		.End()
		.Add(cards)
		.Add(fStatus);

	if (fSelfName.Length() == 0) {
		fRealName->SetEnabled(false);
		fApplyButton->SetEnabled(false);
		fChangePasswordButton->SetEnabled(false);
		fAutologinBox->SetEnabled(false);
		fChoosePictureButton->SetEnabled(false);
		fClearPictureButton->SetEnabled(false);
	}
	_ShowEditor(false);

	CenterOnScreen();
	// Until the password is given the list holds only masked rows.
	_Reload();
	_LoadAutologin();
	_LoadPicture();
	Show();
}


UsersWindow::~UsersWindow()
{
}


void
UsersWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
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
		{
			AccountRow* row = static_cast<AccountRow*>(
				fList->CurrentSelection());
			if (row != NULL && row->GetAccount() == NULL) {
				fList->DeselectAll();
				_Authenticate();
			}
			_UpdateStatus();
			break;
		}
		case kMsgApplyRealName:
			_ApplyRealName();
			break;
		case kMsgChangePassword:
			_ChangePassword();
			break;
		case kMsgAutologinToggled:
			_ToggleAutologin();
			break;
		case kMsgChoosePicture:
			_ChoosePicture();
			break;
		case kMsgClearPicture:
			fPictureView->Clear();
			break;
		case kMsgPictureChosen:
		{
			entry_ref ref;
			if (message->FindRef("refs", 0, &ref) == B_OK)
				_ApplyPicture(&ref);
			break;
		}
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
				_ShowError(B_TRANSLATE("Select an account first."));
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
					: B_TRANSLATE("Could not grant administrator rights."));
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
				_ShowError(B_TRANSLATE("Select an account first."));
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
					: B_TRANSLATE("Could not revoke administrator rights."));
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
					: B_TRANSLATE("Could not create the account."));
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
					: B_TRANSLATE("Could not set the password."));
			_Reload();
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


bool
UsersWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


Account*
UsersWindow::_SelectedAccount()
{
	AccountRow* row = static_cast<AccountRow*>(fList->CurrentSelection());
	if (row == NULL)
		return NULL;
	return row->GetAccount();
}


void
UsersWindow::_Reload()
{
	BString selected;
	if (Account* account = _SelectedAccount())
		selected = account->name;

	fList->Clear();
	fAccounts.MakeEmpty();
	if (listAccounts(&fAccounts) != B_OK) {
		_ShowError(B_TRANSLATE("Could not read the account list."));
		_UpdateStatus();
		return;
	}
	fAccounts.SortItems(_compareAccountName);

	for (int32 i = 0; i < fAccounts.CountItems(); i++) {
		Account* account = fAccounts.ItemAt(i);
		if (!fAuthenticated && !account->isSelf)
			continue;
		AccountRow* row = new AccountRow(account);
		fList->AddRow(row);
		if (selected.Length() > 0 && account->name == selected)
			fList->AddToSelection(row);
	}

	if (!fAuthenticated) {
		static const char* kMasks[] = { "\xe2\x80\xa2\xe2\x80\xa2"
			"\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2",
			"\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
			"\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2",
			"\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"
			"\xe2\x80\xa2" };
		for (size_t i = 0; i < sizeof(kMasks) / sizeof(kMasks[0]); i++)
			fList->AddRow(new AccountRow(kMasks[i]));
	}
	_UpdateStatus();
}


void
UsersWindow::_UpdateStatus()
{
	if (fList->CountRows() > 0) {
		Account* selected = _SelectedAccount();
		_ShowEditor(selected != NULL && selected->isSelf
			&& !selected->isProtected);
		if (selected != NULL && selected->isProtected) {
			fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR);
			fStatus->SetText(kProtectedHint);
			return;
		}
		fStatus->SetHighUIColor(fAuthenticated ? B_FAILURE_COLOR
			: B_PANEL_TEXT_COLOR);
		fStatus->SetText(fAuthenticated ? ""
			: B_TRANSLATE("Changing other accounts asks for the "
				"administrator password."));
		return;
	}

	_ShowEditor(false);
	fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR);
	fStatus->SetText(B_TRANSLATE("No accounts to show. Accounts with a login "
		"shell and a user ID of 1000 or higher are listed here. Use Add "
		"User to create one."));
}


void
UsersWindow::_ShowError(const char* text)
{
	if (text == NULL || text[0] == '\0')
		text = B_TRANSLATE("The operation failed.");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);
	fStatus->SetText(text);
	BAlert* alert = new BAlert(B_TRANSLATE("Users"), text,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


void
UsersWindow::_Confirm(const char* text, uint32 what)
{
	BAlert* alert = new BAlert(B_TRANSLATE("Users"), text,
		B_TRANSLATE("Cancel"), B_TRANSLATE("OK"), NULL, B_WIDTH_AS_USUAL,
		B_INFO_ALERT);
	alert->SetDefaultButton(alert->ButtonAt(1));
	alert->Go(new BInvoker(new BMessage(what), BMessenger(this)));
}


// Asks for the password once; a dismissed prompt changes nothing.
bool
UsersWindow::_Authenticate()
{
	if (fAuthenticated)
		return true;

	StringList* args = new StringList;
	args->AddItem(new BString("auth"));
	BString error;
	status_t status = runAdminHelper(args, NULL, error);
	delete args;
	if (status != B_OK) {
		if (status != B_CANCELED)
			_ShowError(error.Length() > 0 ? error.String()
				: B_TRANSLATE("Authentication failed."));
		return false;
	}

	fAuthenticated = true;
	_Reload();
	return true;
}


void
UsersWindow::_AddUser()
{
	if (!_Authenticate())
		return;
	new AddUserDialog(BMessenger(this));
}


void
UsersWindow::_DeleteUser()
{
	if (!_Authenticate())
		return;
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError(B_TRANSLATE("Select an account first."));
		return;
	}
	if (account->isProtected) {
		_ShowError(kProtectedHint);
		return;
	}
	if (account->isSelf) {
		_ShowError(B_TRANSLATE(
			"You cannot delete the account you are signed in as."));
		return;
	}
	if (account->hasSession) {
		BString text;
		text.SetToFormat(
			B_TRANSLATE("%s has a running session. Log that user out "
				"before deleting the account."), account->name.String());
		_ShowError(text.String());
		return;
	}

	BString text;
	text.SetToFormat(B_TRANSLATE("Delete account %s?\n\n"
		"Keep Home leaves the files on disk; Remove Home deletes them."),
		account->name.String());
	BAlert* alert = new BAlert(B_TRANSLATE("Users"), text.String(),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Keep Home"),
		B_TRANSLATE("Remove Home"),
		B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->SetDefaultButton(alert->ButtonAt(1));
	alert->Go(new BInvoker(new BMessage(kMsgDeleteConfirmed),
		BMessenger(this)));
}


void
UsersWindow::_GrantAdmin()
{
	if (!_Authenticate())
		return;
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError(B_TRANSLATE("Select an account first."));
		return;
	}
	if (account->isProtected) {
		_ShowError(kProtectedHint);
		return;
	}
	if (account->isAdmin) {
		_ShowError(B_TRANSLATE("That account is already an administrator."));
		return;
	}
	BString text;
	text.SetToFormat(B_TRANSLATE("Give administrator rights to %s?"),
		account->name.String());
	_Confirm(text.String(), kMsgGrantConfirmed);
}


void
UsersWindow::_RevokeAdmin()
{
	if (!_Authenticate())
		return;
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError(B_TRANSLATE("Select an account first."));
		return;
	}
	if (account->isProtected) {
		_ShowError(kProtectedHint);
		return;
	}
	if (!account->isAdmin) {
		_ShowError(B_TRANSLATE("That account is not an administrator."));
		return;
	}
	if (account->isSelf) {
		_ShowError(B_TRANSLATE(
			"You cannot remove your own administrator rights."));
		return;
	}
	BString text;
	text.SetToFormat(B_TRANSLATE("Remove administrator rights from %s?"),
		account->name.String());
	_Confirm(text.String(), kMsgRevokeConfirmed);
}


void
UsersWindow::_ResetPassword()
{
	if (!_Authenticate())
		return;
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError(B_TRANSLATE("Select an account first."));
		return;
	}
	if (account->isProtected) {
		_ShowError(kProtectedHint);
		return;
	}
	new ResetPasswordDialog(account->name.String(), BMessenger(this));
}


void
UsersWindow::_DeleteConfirmed(bool removeHome)
{
	Account* account = _SelectedAccount();
	if (account == NULL) {
		_ShowError(B_TRANSLATE("Select an account first."));
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
			: B_TRANSLATE("Could not delete the account."));
	_Reload();
}


void
UsersWindow::_ShowEditor(bool self)
{
	fCards->SetVisibleItem(self ? 1 : 0);
	if (self)
		_LoadSelf();
}


void
UsersWindow::_LoadSelf()
{
	BString heading;
	heading.SetToFormat(B_TRANSLATE("Account: %s"), fSelfName.String());
	fSelfHeader->SetText(heading.String());

	struct passwd* pw = getpwnam(fSelfName.String());
	if (pw != NULL && pw->pw_gecos != NULL) {
		BString gecos(pw->pw_gecos);
		int32 comma = gecos.FindFirst(',');
		if (comma > 0)
			gecos.Truncate(comma);
		fRealName->SetText(gecos.String());
	}
}


void
UsersWindow::_LoadAutologin()
{
	FILE* f = fopen("/etc/vos/autologin", "r");
	if (f == NULL)
		return;
	char name[64];
	if (fgets(name, sizeof(name), f) != NULL) {
		size_t len = strlen(name);
		while (len > 0 && (name[len-1] == '\n' || name[len-1] == '\r'
				|| name[len-1] == ' ' || name[len-1] == '\t'))
			name[--len] = '\0';
		if (len > 0 && fSelfName == name)
			fAutologinBox->SetValue(B_CONTROL_ON);
	}
	fclose(f);
}


// Asks for the current password itself, like passwd(1).
void
UsersWindow::_ChangePassword()
{
	if (fSelfName.Length() == 0)
		return;
	new PasswordDialog(fSelfName.String());
}


void
UsersWindow::_ApplyRealName()
{
	if (fSelfName.Length() == 0)
		return;
	const char* text = fRealName->Text();
	if (text == NULL)
		return;
	if (strchr(text, ',') != NULL || strchr(text, ':') != NULL) {
		_ShowError(B_TRANSLATE("Full name cannot contain commas or colons."));
		return;
	}

	StringList* args = new StringList;
	args->AddItem(new BString("set-gecos"));
	args->AddItem(new BString(fSelfName.String()));
	args->AddItem(new BString(text));
	BString error;
	status_t status = runUserHelper(args, error);
	delete args;
	if (status != B_OK) {
		_ShowError(error.Length() > 0 ? error.String()
			: B_TRANSLATE("Could not apply the full name."));
		_LoadSelf();
		return;
	}
	_Reload();
}


void
UsersWindow::_ToggleAutologin()
{
	const char* user = "";
	if (fAutologinBox->Value() == B_CONTROL_ON)
		user = fSelfName.String();

	pid_t pid = fork();
	if (pid < 0) {
		fAutologinBox->SetValue(B_CONTROL_OFF);
		_LoadAutologin();
		_ShowError(B_TRANSLATE("Could not start the autologin helper."));
		return;
	}
	if (pid == 0) {
		execlp("pkexec", "pkexec",
			"/usr/libexec/vos-set-autologin", user, (char*)NULL);
		_exit(127);
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fAutologinBox->SetValue(B_CONTROL_OFF);
		_LoadAutologin();
	}
}


void
UsersWindow::_ChoosePicture()
{
	if (fSelfName.Length() == 0)
		return;
	BFilePanel* panel = new BFilePanel(B_OPEN_PANEL,
		new BMessenger(this), NULL, B_FILE_NODE, false,
		new BMessage(kMsgPictureChosen), new ImageRefFilter,
		true, true);
	panel->Show();
}


void
UsersWindow::_ApplyPicture(const entry_ref* ref)
{
	if (ref == NULL || fSelfName.Length() == 0)
		return;

	BBitmap* picture = BTranslationUtils::GetBitmap(ref);
	if (picture == NULL) {
		_ShowError(B_TRANSLATE("Could not load that image."));
		return;
	}

	// The preflet writes ~/.face itself; the helper never touches HOME.
	struct passwd* pw = getpwuid(getuid());
	const char* home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		home = pw != NULL ? pw->pw_dir : NULL;
	if (home == NULL || home[0] == '\0') {
		delete picture;
		_ShowError(B_TRANSLATE("Could not resolve the home directory."));
		return;
	}

	BPath facePath(home);
	facePath.Append(".face");
	BFile faceFile(facePath.Path(), B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	if (faceFile.InitCheck() != B_OK) {
		delete picture;
		_ShowError(B_TRANSLATE("Could not write ~/.face."));
		return;
	}

	BTranslatorRoster* roster = BTranslatorRoster::Default();
	if (roster == NULL) {
		delete picture;
		_ShowError(B_TRANSLATE("Translation kit unavailable."));
		return;
	}

	// PNG: the helper checks magic bytes and rejects anything else.
	// BBitmapStream owns picture; its destructor deletes it.
	BBitmapStream stream(picture);
	status_t status = roster->Translate(&stream, NULL, NULL, &faceFile,
		B_PNG_FORMAT, B_TRANSLATOR_BITMAP);
	faceFile.Flush();
	if (status != B_OK) {
		_ShowError(B_TRANSLATE("Could not encode the picture."));
		return;
	}

	BNodeInfo faceInfo(&faceFile);
	if (faceInfo.InitCheck() == B_OK)
		faceInfo.SetType("image/png");

	StringList* args = new StringList;
	args->AddItem(new BString("set-face"));
	args->AddItem(new BString(fSelfName.String()));
	args->AddItem(new BString(facePath.Path()));
	BString error;
	status = runUserHelper(args, error);
	delete args;

	if (status != B_OK) {
		_ShowError(error.Length() > 0 ? error.String()
			: B_TRANSLATE("Could not set the account picture."));
		return;
	}

	_LoadPicture();
}


void
UsersWindow::_LoadPicture()
{
	if (fSelfName.Length() == 0)
		return;

	// Prefer the AccountsService icon; fall back to ~/.face.
	BPath systemIcon;
	systemIcon.SetTo("/var/lib/AccountsService/icons");
	systemIcon.Append(fSelfName.String());

	BPath homeIcon;
	const char* home = getenv("HOME");
	if (home != NULL) {
		homeIcon.SetTo(home);
		homeIcon.Append(".face");
	}

	const char* candidates[3];
	candidates[0] = systemIcon.Path();
	candidates[1] = home != NULL ? homeIcon.Path() : NULL;
	candidates[2] = NULL;

	for (int32 i = 0; candidates[i] != NULL; i++) {
		BFile file(candidates[i], B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			continue;
		off_t size = 0;
		if (file.GetSize(&size) != B_OK || size < 1)
			continue;
		BBitmap* picture = BTranslationUtils::GetBitmap(&file);
		if (picture != NULL) {
			fPictureView->SetPicture(picture);
			return;
		}
	}
}
