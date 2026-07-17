/*
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>. Distributed under the terms of the
 * MIT License.
 */

#include "UserWindow.h"

#include "PasswordDialog.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>


static const uint32 kMsgChangePassword = 'cpwd';
static const uint32 kMsgApplyRealName = 'aply';
static const uint32 kMsgAutologinToggled = 'atgl';


UserWindow::UserWindow()
	:
	BWindow(BRect(0, 0, 480, 240), "User", B_TITLED_WINDOW,
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

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(fHeader)
		.Add(fRealName)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fApplyButton)
		.End()
		.Add(fChangePasswordButton)
		.Add(fAutologinBox);

	if (fUserName.Length() == 0) {
		fRealName->SetEnabled(false);
		fApplyButton->SetEnabled(false);
		fChangePasswordButton->SetEnabled(false);
		fAutologinBox->SetEnabled(false);
	}

	CenterOnScreen();
	_LoadAutologin();
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
	if (strchr(text, ',') != NULL || strchr(text, ':') != NULL)
		return;

	pid_t pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		execlp("pkexec", "pkexec",
			"/usr/sbin/chfn", "-f", text, fUserName.String(),
			(char*)NULL);
		_exit(127);
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
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
