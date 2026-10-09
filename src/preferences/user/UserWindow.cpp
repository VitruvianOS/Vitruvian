/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "UserWindow.h"

#include "AccountUtil.h"
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
#include <CheckBox.h>
#include <File.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <NodeInfo.h>
#include <Path.h>
#include <StringView.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <TranslatorRoster.h>


using namespace BPrivate::Accounts;


static const uint32 kMsgChangePassword = 'cpwd';
static const uint32 kMsgApplyRealName = 'aply';
static const uint32 kMsgAutologinToggled = 'atgl';
static const uint32 kMsgChoosePicture = 'picp';
static const uint32 kMsgClearPicture = 'picc';
static const uint32 kMsgPictureChosen = 'picv';


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


UserWindow::UserWindow()
	:
	BWindow(BRect(0, 0, 520, 340), "User", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS)
{
	struct passwd* pw = getpwuid(getuid());
	if (pw != NULL && pw->pw_name != NULL)
		fUserName = pw->pw_name;

	BString heading;
	heading.SetToFormat("Account: %s", fUserName.String());
	fHeader = new BStringView("header", heading.String());
	fHeader->SetFont(be_bold_font);

	BString gecos;
	if (pw != NULL && pw->pw_gecos != NULL) {
		gecos = pw->pw_gecos;
		int32 comma = gecos.FindFirst(',');
		if (comma > 0)
			gecos.Truncate(comma);
	}

	fRealName = new BTextControl("realname", "Full name:", gecos.String(),
		new BMessage(kMsgApplyRealName));

	fApplyButton = new BButton("apply", "Apply full name",
		new BMessage(kMsgApplyRealName));

	fChangePasswordButton = new BButton("passwd",
		"Change password" B_UTF8_ELLIPSIS,
		new BMessage(kMsgChangePassword));

	fAutologinBox = new BCheckBox("autologin",
		"Log in automatically at boot",
		new BMessage(kMsgAutologinToggled));

	fPictureView = new UserPictureView();
	fChoosePictureButton = new BButton("choosepic",
		"Choose Picture" B_UTF8_ELLIPSIS,
		new BMessage(kMsgChoosePicture));
	fClearPictureButton = new BButton("clearpic", "Clear",
		new BMessage(kMsgClearPicture));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fHeader)
		.AddGroup(B_HORIZONTAL, B_USE_ITEM_SPACING)
			.Add(fPictureView)
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.Add(fRealName)
				.AddGroup(B_HORIZONTAL)
					.Add(fChoosePictureButton)
					.Add(fClearPictureButton)
				.End()
				.AddGlue()
			.End()
		.End()
		.Add(fChangePasswordButton)
		.Add(fAutologinBox)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApplyButton)
		.End();

	if (fUserName.Length() == 0) {
		fRealName->SetEnabled(false);
		fApplyButton->SetEnabled(false);
		fChangePasswordButton->SetEnabled(false);
		fAutologinBox->SetEnabled(false);
		fChoosePictureButton->SetEnabled(false);
		fClearPictureButton->SetEnabled(false);
	}

	CenterOnScreen();
	_LoadAutologin();
	_LoadPicture();
	Show();
}


UserWindow::~UserWindow()
{
}


void
UserWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
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
			_ChoosePicture(message);
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

		default:
			BWindow::MessageReceived(message);
	}
}


bool
UserWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
UserWindow::_LoadAutologin()
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
		if (len > 0 && fUserName == name)
			fAutologinBox->SetValue(B_CONTROL_ON);
	}
	fclose(f);
}


void
UserWindow::_ChangePassword()
{
	if (fUserName.Length() == 0)
		return;
	new PasswordDialog(fUserName.String());
}


void
UserWindow::_ApplyRealName()
{
	if (fUserName.Length() == 0)
		return;
	const char* text = fRealName->Text();
	if (text == NULL)
		return;
	if (strchr(text, ',') != NULL || strchr(text, ':') != NULL) {
		_ShowError("Full name cannot contain commas or colons.");
		return;
	}

	StringList* args = new StringList;
	args->AddItem(new BString("set-gecos"));
	args->AddItem(new BString(fUserName.String()));
	args->AddItem(new BString(text));
	BString error;
	status_t status = runAdminHelper(args, NULL, error);
	delete args;
	if (status != B_OK) {
		_ShowError(error.Length() > 0 ? error.String()
			: "Could not apply the full name.");
		// Restore the field from GECOS on failure/cancel.
		struct passwd* pw = getpwnam(fUserName.String());
		if (pw != NULL && pw->pw_gecos != NULL) {
			BString gecos(pw->pw_gecos);
			int32 comma = gecos.FindFirst(',');
			if (comma > 0)
				gecos.Truncate(comma);
			fRealName->SetText(gecos.String());
		}
	}
}


void
UserWindow::_ToggleAutologin()
{
	const char* user = "";
	if (fAutologinBox->Value() == B_CONTROL_ON)
		user = fUserName.String();

	pid_t pid = fork();
	if (pid < 0) {
		fAutologinBox->SetValue(B_CONTROL_OFF);
		_LoadAutologin();
		_ShowError("Could not start the autologin helper.");
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
		// pkexec cancelled or helper failed — revert to the on-disk state.
		fAutologinBox->SetValue(B_CONTROL_OFF);
		_LoadAutologin();
	}
}


void
UserWindow::_ChoosePicture(BMessage* /*message*/)
{
	if (fUserName.Length() == 0)
		return;
	BFilePanel* panel = new BFilePanel(B_OPEN_PANEL,
		new BMessenger(this), NULL, B_FILE_NODE, false,
		new BMessage(kMsgPictureChosen), new ImageRefFilter,
		true, true);
	panel->Show();
}


void
UserWindow::_ApplyPicture(const entry_ref* ref)
{
	if (ref == NULL || fUserName.Length() == 0)
		return;

	BBitmap* picture = BTranslationUtils::GetBitmap(ref);
	if (picture == NULL) {
		_ShowError("Could not load that image.");
		return;
	}

	// The preflet writes ~/.face itself; the helper never touches HOME.
	struct passwd* pw = getpwuid(getuid());
	const char* home = getenv("HOME");
	if (home == NULL || home[0] == '\0')
		home = pw != NULL ? pw->pw_dir : NULL;
	if (home == NULL || home[0] == '\0') {
		delete picture;
		_ShowError("Could not resolve the home directory.");
		return;
	}

	BPath facePath(home);
	facePath.Append(".face");
	BFile faceFile(facePath.Path(), B_WRITE_ONLY | B_CREATE_FILE
		| B_ERASE_FILE);
	if (faceFile.InitCheck() != B_OK) {
		delete picture;
		_ShowError("Could not write ~/.face.");
		return;
	}

	BTranslatorRoster* roster = BTranslatorRoster::Default();
	if (roster == NULL) {
		delete picture;
		_ShowError("Translation kit unavailable.");
		return;
	}

	// PNG: the helper checks magic bytes and rejects anything else.
	// BBitmapStream owns picture; its destructor deletes it.
	BBitmapStream stream(picture);
	status_t status = roster->Translate(&stream, NULL, NULL, &faceFile,
		B_PNG_FORMAT, B_TRANSLATOR_BITMAP);
	faceFile.Flush();
	if (status != B_OK) {
		_ShowError("Could not encode the picture.");
		return;
	}

	BNodeInfo faceInfo(&faceFile);
	if (faceInfo.InitCheck() == B_OK)
		faceInfo.SetType("image/png");

	StringList* args = new StringList;
	args->AddItem(new BString("set-face"));
	args->AddItem(new BString(fUserName.String()));
	args->AddItem(new BString(facePath.Path()));
	BString error;
	status = runUserHelper(args, error);
	delete args;

	if (status != B_OK) {
		_ShowError(error.Length() > 0 ? error.String()
			: "Could not set the account picture.");
		return;
	}

	_LoadPicture();
}


void
UserWindow::_LoadPicture()
{
	if (fUserName.Length() == 0)
		return;

	// Prefer the AccountsService icon; fall back to ~/.face.
	BPath systemIcon;
	systemIcon.SetTo("/var/lib/AccountsService/icons");
	systemIcon.Append(fUserName.String());

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


void
UserWindow::_ShowError(const char* text)
{
	if (text == NULL || text[0] == '\0')
		text = "The operation failed.";
	BAlert* alert = new BAlert("User", text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}
