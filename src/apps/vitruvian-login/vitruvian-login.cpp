/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the GPL License.
 */

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Screen.h>
#include <StringView.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>

#include <WindowPrivate.h>
#include <kernel/util/KMessage.h>
#include <LaunchDaemonDefs.h>


static const char* kAppSignature = "application/x-vnd.Vitruvian-login";
static const uint32 kMsgLogin  = 'lgin';
static const uint32 kMsgCancel = 'cncl';


class LoginWindow : public BWindow {
public:
	LoginWindow();

	virtual void MessageReceived(BMessage* msg);

private:
	status_t _SendAuthRequest(const char* user, const char* password);
	status_t _SendLoginOk(const char* user);
	void     _ShowError(const char* text);

	BTextControl* fUser;
	BTextControl* fPassword;
	BStringView*  fStatus;
	BButton*      fLoginButton;
};


LoginWindow::LoginWindow()
	:
	BWindow(BRect(0, 0, 420, 220), "Vitruvian",
		B_NO_BORDER_WINDOW_LOOK,
		kPasswordWindowFeel,
		B_NOT_MOVABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
		| B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS, B_ALL_WORKSPACES)
{
	BView* top = new BView(Bounds(), "top", B_FOLLOW_ALL, B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	top->SetHighUIColor(B_PANEL_TEXT_COLOR);
	AddChild(top);

	BRect boxFrame(Bounds());
	boxFrame.InsetBy(10, 10);
	BBox* box = new BBox(boxFrame, "box", B_FOLLOW_NONE);
	box->SetLabel("Log in");
	top->AddChild(box);

	// Pre-fill with our own login name unless we are vos_login.
	const char* defaultUser = "";
	struct passwd* self = getpwuid(getuid());
	if (self != NULL && self->pw_name != NULL
			&& strcmp(self->pw_name, "vos_login") != 0) {
		defaultUser = self->pw_name;
	}

	BRect r(20, 30, 380, 55);
	fUser = new BTextControl(r, "user", "Username:", defaultUser, NULL,
		B_FOLLOW_NONE);
	fUser->SetDivider(be_plain_font->StringWidth("Password:") + 12);
	box->AddChild(fUser);

	r.OffsetBy(0, 32);
	fPassword = new BTextControl(r, "password", "Password:", "", NULL,
		B_FOLLOW_NONE);
	fPassword->TextView()->HideTyping(true);
	fPassword->SetDivider(be_plain_font->StringWidth("Password:") + 12);
	box->AddChild(fPassword);

	r.OffsetBy(0, 32);
	fStatus = new BStringView(r, "status", "");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);
	box->AddChild(fStatus);

	fLoginButton = new BButton(BRect(300, 130, 380, 155), "login",
		"Log in", new BMessage(kMsgLogin), B_FOLLOW_NONE);
	fLoginButton->MakeDefault(true);
	box->AddChild(fLoginButton);

	if (*defaultUser != '\0')
		fPassword->MakeFocus(true);
	else
		fUser->MakeFocus(true);

	BScreen screen(this);
	BRect scr = screen.Frame();
	MoveTo(scr.left + (scr.Width()  - Bounds().Width())  / 2,
	       scr.top  + (scr.Height() - Bounds().Height()) / 2);
}


void
LoginWindow::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgLogin: {
			const char* user = fUser->Text();
			const char* pass = fPassword->Text();
			if (user == NULL || *user == '\0') {
				_ShowError("Enter a user name.");
				break;
			}
			fLoginButton->SetEnabled(false);
			fStatus->SetText("Authenticating...");
			status_t s = _SendAuthRequest(user, pass);
			fPassword->SetText("");
			if (s != B_OK) {
				_ShowError("Authentication failed.");
				fLoginButton->SetEnabled(true);
				break;
			}
			_SendLoginOk(user);
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		}
		case kMsgCancel:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		default:
			BWindow::MessageReceived(msg);
	}
}


void
LoginWindow::_ShowError(const char* text)
{
	fStatus->SetText(text);
}


status_t
LoginWindow::_SendAuthRequest(const char* user, const char* password)
{
	port_id janus = find_port(B_LAUNCH_DAEMON_PORT_NAME);
	if (janus < 0)
		return janus;

	BPrivate::KMessage req(BPrivate::B_JANUS_AUTH_REQUEST);
	req.AddString("user", user);
	req.AddString("password", password);

	BPrivate::KMessage reply;
	status_t s = req.SendTo(janus, -1, &reply);
	if (s != B_OK)
		return s;
	return reply.What() == B_OK ? B_OK : B_PERMISSION_DENIED;
}


status_t
LoginWindow::_SendLoginOk(const char* user)
{
	port_id janus = find_port(B_LAUNCH_DAEMON_PORT_NAME);
	if (janus < 0)
		return janus;

	BPrivate::KMessage msg(BPrivate::B_JANUS_LOGIN_OK);
	msg.AddString("user", user);
	BPrivate::KMessage reply;
	return msg.SendTo(janus, -1, &reply);
}


class LoginApp : public BApplication {
public:
	LoginApp() : BApplication(kAppSignature) {}

	virtual void ReadyToRun() {
		LoginWindow* w = new LoginWindow();
		w->Show();
	}
};


int
main(int, char**)
{
	LoginApp app;
	app.Run();
	return 0;
}
