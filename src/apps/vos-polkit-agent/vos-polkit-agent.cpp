/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>

#include <new>
#include <unistd.h>

#include <systemd/sd-bus.h>

#include <Application.h>
#include <Autolock.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <Menu.h>
#include <MenuField.h>
#include <Messenger.h>
#include <Notification.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <String.h>
#include <StringList.h>
#include <StringView.h>
#include <TextView.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>

#include <IconView.h>

#include <LaunchDaemonDefs.h>

#include "grant_store.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PolkitAuthDialog"


static const char* kAppSignature = "application/x-vnd.vos-polkit-agent";

static const char* kAgentObjectPath = "/org/vos/PolkitAgent";
static const char* kAgentInterface  = "org.freedesktop.PolicyKit1.AuthenticationAgent";
static const char* kAuthorityBus    = "org.freedesktop.PolicyKit1";
static const char* kAuthorityPath   = "/org/freedesktop/PolicyKit1/Authority";
static const char* kAuthorityIface  = "org.freedesktop.PolicyKit1.Authority";

static const char* kHelperPath = "/usr/lib/polkit-1/polkit-agent-helper-1";
static const char* kGrantHelperPath = "/usr/libexec/vos-polkit-grant-helper";
static const char* kGrantStateDir = "/run/vos/polkit-grant";
static const char* kLockWatchPortName = "vos-polkit-agent:session-lock";

static const uint32 kMsgLogin  = 'lgin';
static const uint32 kMsgCancel = 'cncl';
static const uint32 kMsgClosed = 'clsd';
static const uint32 kMsgAttempt = 'attm';
static const uint32 kMsgAuthResult = 'ares';
static const uint32 kMsgIdentity = 'idnt';
static const uint32 kMsgRemember = 'rmbr';
static const uint32 kMsgDuration = 'durm';
static const uint32 kMsgSaveDuration = 'sver';
static const uint32 kMsgGrant = 'grnt';
static const uint32 kMsgGrantResult = 'grrs';

static const char* kIconResource = "PolkitLock";


// Operator knobs; the settings file may override every field. Revokes
// default on except the network one, which is the noisiest.
struct AgentSettings {
	int32		duration;		// minutes; 1, 5 or 15
	bool		revokeSuspend;
	bool		revokeLock;
	bool		revokeSession;
	bool		revokeNetwork;
};

static AgentSettings gSettings = { 5, true, true, true, false };


struct AuthRequest {
	BString		actionId;
	BString		message;
	BString		iconName;
	BString		cookie;
	BString		identityUser;
	BStringList	users;
};


class AuthDialog : public BWindow {
public:
			AuthDialog(const AuthRequest& req, BMessenger reply,
				BMessenger agent);

	virtual	void	MessageReceived(BMessage* msg);
	virtual	bool	QuitRequested();

private:
			void	_Send(bool ok);
			void	_UpdateDetails();
			void	_Attempt();
			void	_SetBusy(bool busy);

			BMessenger		fAgent;
			BMessenger		fReply;
			BString			fCookie;
			BString			fActionId;
			BStringList		fUsers;
			BString			fCurrentUser;
			BTextView*		fMessageView;
			BTextView*		fDetailsView;
			BMenuField*		fIdentityField;
			BTextControl*	fPassword;
			BCheckBox*		fRemember;
			BMenuField*		fDurationField;
			BStringView*	fStatus;
			BButton*		fLogin;
			bool			fSent;
			bool			fBusy;
};


static void
load_settings()
{
	BPath directory;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &directory) != B_OK)
		return;
	BPath folder(directory.Path(), "vos-polkit-agent");
	BPath file(folder.Path(), "settings");

	FILE* fp = fopen(file.Path(), "r");
	if (fp == NULL)
		return;

	char line[256];
	while (fgets(line, sizeof(line), fp) != NULL) {
		char* value = strchr(line, '=');
		if (value == NULL)
			continue;
		*value++ = '\0';
		char* end = strchr(value, '\n');
		if (end != NULL)
			*end = '\0';

		if (strcmp(line, "duration") == 0) {
			int32 minutes = atoi(value);
			if (minutes == 1 || minutes == 5 || minutes == 15)
				gSettings.duration = minutes;
		} else if (strcmp(line, "revoke-suspend") == 0)
			gSettings.revokeSuspend = atoi(value) != 0;
		else if (strcmp(line, "revoke-lock") == 0)
			gSettings.revokeLock = atoi(value) != 0;
		else if (strcmp(line, "revoke-session") == 0)
			gSettings.revokeSession = atoi(value) != 0;
		else if (strcmp(line, "revoke-network") == 0)
			gSettings.revokeNetwork = atoi(value) != 0;
	}
	fclose(fp);
}


static void
save_settings()
{
	BPath directory;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &directory) != B_OK)
		return;
	BPath folder(directory.Path(), "vos-polkit-agent");
	create_directory(folder.Path(), 0755);
	BPath file(folder.Path(), "settings");

	FILE* fp = fopen(file.Path(), "w");
	if (fp == NULL)
		return;

	fprintf(fp, "duration=%d\n", gSettings.duration);
	fprintf(fp, "revoke-suspend=%d\n", gSettings.revokeSuspend ? 1 : 0);
	fprintf(fp, "revoke-lock=%d\n", gSettings.revokeLock ? 1 : 0);
	fprintf(fp, "revoke-session=%d\n", gSettings.revokeSession ? 1 : 0);
	fprintf(fp, "revoke-network=%d\n", gSettings.revokeNetwork ? 1 : 0);
	fclose(fp);
}


static BTextView*
make_read_only_text_view(const char* name, const BFont* font,
	const rgb_color& color)
{
	BTextView* view = new BTextView(name);
	view->MakeEditable(false);
	view->MakeSelectable(false);
	view->SetWordWrap(true);
	view->SetFontAndColor(font, B_FONT_ALL, &color);
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	view->SetInsets(0, 0, 0, 0);
	return view;
}


AuthDialog::AuthDialog(const AuthRequest& req, BMessenger reply,
	BMessenger agent)
	:
	BWindow(BRect(0, 0, 400, 10), B_TRANSLATE("Authenticate"),
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_MOVABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
		| B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
		| B_CLOSE_ON_ESCAPE | B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES),
	fAgent(agent),
	fReply(reply),
	fCookie(req.cookie),
	fActionId(req.actionId),
	fUsers(req.users),
	fCurrentUser(req.identityUser),
	fMessageView(NULL),
	fDetailsView(NULL),
	fIdentityField(NULL),
	fPassword(NULL),
	fRemember(NULL),
	fDurationField(NULL),
	fStatus(NULL),
	fLogin(NULL),
	fSent(false),
	fBusy(false)
{
	rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);

	const char* message = req.message.String();
	if (message == NULL || *message == '\0')
		message = B_TRANSLATE("Authenticate to continue");

	// Cap wrap width so a long action message grows the height, not the window.
	const float maxWidth = 420.0f * be_plain_font->Size() / 12.0f;

	fMessageView = make_read_only_text_view("message", be_bold_font, textColor);
	fMessageView->SetText(message);
	fMessageView->SetExplicitMaxSize(BSize(maxWidth, B_SIZE_UNSET));

	// Details sit under a bold headline; slightly smaller than plain is enough.
	BFont smallFont(*be_plain_font);
	smallFont.SetSize(be_plain_font->Size() * 0.85f);
	fDetailsView = make_read_only_text_view("details", &smallFont, textColor);
	fDetailsView->SetExplicitMaxSize(BSize(maxWidth, B_SIZE_UNSET));

	IconView* iconView = new IconView(B_LARGE_ICON);
	BResources* res = BApplication::AppResources();
	size_t iconSize = 0;
	const uint8_t* iconData = static_cast<const uint8_t*>(
		res->LoadResource(B_VECTOR_ICON_TYPE, kIconResource, &iconSize));
	if (iconData != NULL)
		iconView->SetIcon(iconData, iconSize, B_LARGE_ICON);

	if (fUsers.CountStrings() > 1) {
		BPopUpMenu* menu = new BPopUpMenu("identity");
		for (int32 i = 0; i < fUsers.CountStrings(); i++) {
			BString name = fUsers.StringAt(i);
			BMenuItem* item = new BMenuItem(name.String(),
				new BMessage(kMsgIdentity));
			item->SetMarked(name == fCurrentUser);
			menu->AddItem(item);
		}
		menu->SetTargetForItems(this);
		fIdentityField = new BMenuField(B_TRANSLATE("Authenticate as:"),
			menu);
	}

	fPassword = new BTextControl("password", B_TRANSLATE("Password:"),
		"", new BMessage(kMsgLogin));
	fPassword->TextView()->HideTyping(true);

	fRemember = new BCheckBox("remember",
		B_TRANSLATE("Don't ask for my password again for"),
		new BMessage(kMsgRemember));

	BPopUpMenu* durationMenu = new BPopUpMenu("duration");
	static const int32 kDurations[] = { 1, 5, 15 };
	for (int32 i = 0; i < 3; i++) {
		BString label;
		label << kDurations[i]
			<< (kDurations[i] == 1 ? B_TRANSLATE(" minute")
				: B_TRANSLATE(" minutes"));
		BMessage* duration = new BMessage(kMsgDuration);
		duration->AddInt32("minutes", kDurations[i]);
		BMenuItem* item = new BMenuItem(label.String(), duration);
		item->SetMarked(kDurations[i] == gSettings.duration);
		durationMenu->AddItem(item);
	}
	durationMenu->SetTargetForItems(this);
	fDurationField = new BMenuField(NULL, durationMenu);

	fStatus = new BStringView("status", B_TRANSLATE("Wrong password, try again"));
	fStatus->SetHighUIColor(B_FAILURE_COLOR);
	fStatus->Hide();

	BButton* cancel = new BButton(B_TRANSLATE("Cancel"),
		new BMessage(kMsgCancel));
	fLogin = new BButton(B_TRANSLATE("Authenticate"),
		new BMessage(kMsgLogin));
	fLogin->MakeDefault(true);

	BLayoutBuilder::Group<> builder(this, B_VERTICAL, B_USE_DEFAULT_SPACING);
	builder.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(iconView)
			.AddGroup(B_VERTICAL, 0)
				.Add(fMessageView)
				.Add(fDetailsView)
			.End()
		.End();
	if (fIdentityField != NULL)
		builder.Add(fIdentityField);
	builder.Add(fPassword)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fRemember)
			.Add(fDurationField)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(cancel)
			.Add(fLogin)
		.End();

	SetDefaultButton(fLogin);
	_UpdateDetails();
	fPassword->MakeFocus(true);
	CenterOnScreen();
}


void
AuthDialog::_UpdateDetails()
{
	if (fDetailsView == NULL)
		return;

	if (fCurrentUser.IsEmpty() && fUsers.CountStrings() > 0)
		fCurrentUser = fUsers.First();

	BString action = fActionId.Length() > 0
		? fActionId : BString(B_TRANSLATE("(unknown)"));
	BString user = fCurrentUser.Length() > 0
		? fCurrentUser : BString(B_TRANSLATE("(unknown)"));
	BString details;
	details << B_TRANSLATE("Action:") << " " << action << "\n"
		<< B_TRANSLATE("Authenticating as:") << " " << user;
	fDetailsView->SetText(details.String());
	fDetailsView->InvalidateLayout(true);
}


void
AuthDialog::_SetBusy(bool busy)
{
	fBusy = busy;
	if (fLogin != NULL)
		fLogin->SetEnabled(!busy);
	if (fPassword != NULL)
		fPassword->SetEnabled(!busy);
	if (fIdentityField != NULL)
		fIdentityField->SetEnabled(!busy);
	if (fRemember != NULL)
		fRemember->SetEnabled(!busy);
	if (fDurationField != NULL)
		fDurationField->SetEnabled(!busy);
}


void
AuthDialog::_Attempt()
{
	if (fBusy || fSent)
		return;

	if (fStatus != NULL) {
		fStatus->SetText("");
		fStatus->Hide();
	}

	BMessage attempt(kMsgAttempt);
	attempt.AddString("password", fPassword != NULL ? fPassword->Text() : "");
	attempt.AddString("user", fCurrentUser.String());
	attempt.AddString("cookie", fCookie.String());
	attempt.AddMessenger("dialog", BMessenger(this));
	fAgent.SendMessage(&attempt);
	_SetBusy(true);
}


void
AuthDialog::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgLogin:
			_Attempt();
			break;

		case kMsgIdentity:
		{
			BMenuItem* item = NULL;
			if (msg->FindPointer("source", (void**)&item) == B_OK
				&& item != NULL) {
				fCurrentUser = item->Label();
				_UpdateDetails();
			}
			break;
		}

		case kMsgDuration:
		{
			BMenuItem* item = NULL;
			if (msg->FindPointer("source", (void**)&item) == B_OK
				&& item != NULL) {
				int32 minutes = msg->GetInt32("minutes", 5);
				if (minutes == 1 || minutes == 5 || minutes == 15) {
					gSettings.duration = minutes;
					BMessage save(kMsgSaveDuration);
					save.AddInt32("minutes", minutes);
					be_app->PostMessage(&save);
				}
			}
			break;
		}

		case kMsgAuthResult:
			_SetBusy(false);
			if (msg->GetBool("ok", false)) {
				if (fRemember != NULL
					&& fRemember->Value() == B_CONTROL_ON
					&& fPassword != NULL
					&& fPassword->Text()[0] != '\0'
					&& fCurrentUser.Length() > 0) {
					BMessage grant(kMsgGrant);
					grant.AddString("user", fCurrentUser.String());
					grant.AddInt32("minutes", gSettings.duration);
					grant.AddString("password", fPassword->Text());
					be_app->PostMessage(&grant);
				}
				_Send(true);
				PostMessage(B_QUIT_REQUESTED);
				break;
			}
			if (fStatus != NULL) {
				fStatus->SetText(B_TRANSLATE("Wrong password, try again"));
				fStatus->Show();
			}
			if (fPassword != NULL) {
				fPassword->SetText("");
				fPassword->MakeFocus(true);
			}
			break;

		case kMsgCancel:
			_Send(false);
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(msg);
	}
}


bool
AuthDialog::QuitRequested()
{
	if (!fSent)
		_Send(false);
	return true;
}


void
AuthDialog::_Send(bool ok)
{
	if (fSent)
		return;
	fSent = true;
	BMessage reply(kMsgClosed);
	reply.AddBool("ok", ok);
	fReply.SendMessage(&reply);
	if (fPassword != NULL)
		fPassword->SetText("");
}


// ---- polkit-agent-helper-1 driver -----------------------------------------

static bool
run_helper(const char* user, const char* cookie, const char* password)
{
	int inp[2], outp[2];
	if (pipe(inp) != 0 || pipe(outp) != 0)
		return false;

	pid_t pid = fork();
	if (pid < 0)
		return false;

	if (pid == 0) {
		dup2(inp[0],  STDIN_FILENO);
		dup2(outp[1], STDOUT_FILENO);
		close(inp[0]);  close(inp[1]);
		close(outp[0]); close(outp[1]);
		execl(kHelperPath, "polkit-agent-helper-1", user, cookie, NULL);
		_exit(127);
	}

	close(inp[0]);
	close(outp[1]);

	FILE* rd = fdopen(outp[0], "r");
	FILE* wr = fdopen(inp[1],  "w");
	if (rd == NULL || wr == NULL) {
		close(outp[0]); close(inp[1]);
		waitpid(pid, NULL, 0);
		return false;
	}

	bool success = false;
	bool sentPassword = false;
	char line[512];
	while (fgets(line, sizeof(line), rd) != NULL) {
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[--len] = '\0';

		if (strncmp(line, "PAM_PROMPT_ECHO_OFF ", 20) == 0) {
			if (sentPassword) {
				fprintf(stderr, "vos-polkit-agent: unsupported multi-prompt "
					"PAM stack (prompt: %s)\n", line + 20);
				break;
			}
			fprintf(wr, "%s\n", password);
			fflush(wr);
			sentPassword = true;
		} else if (strncmp(line, "PAM_PROMPT_ECHO_ON ", 19) == 0) {
			// Visible-echo field (username/OTP, not a password); there
			// is nothing correct to send here.
			fprintf(stderr, "vos-polkit-agent: unsupported multi-prompt "
				"PAM stack (prompt: %s)\n", line + 19);
			break;
		} else if (strncmp(line, "PAM_ERROR_MSG ", 14) == 0) {
			fprintf(stderr, "vos-polkit-agent: pam: %s\n", line + 14);
		} else if (strncmp(line, "PAM_TEXT_INFO ", 14) == 0) {
			fprintf(stderr, "vos-polkit-agent: pam: %s\n", line + 14);
		} else if (strcmp(line, "SUCCESS") == 0) {
			success = true;
			break;
		} else if (strcmp(line, "FAILURE") == 0) {
			break;
		}
	}
	fclose(rd);
	fclose(wr);

	int status;
	waitpid(pid, &status, 0);
	return success && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}


// Setuid root; the password goes down the pipe exactly like it does for
// polkit-agent-helper-1, and the helper re-checks it with PAM.
static bool
run_grant_helper(const char* user, int32 minutes, const char* password)
{
	int inp[2];
	if (pipe(inp) != 0)
		return false;

	pid_t pid = fork();
	if (pid < 0)
		return false;

	if (pid == 0) {
		dup2(inp[0], STDIN_FILENO);
		close(inp[0]);
		close(inp[1]);
		char minutesArg[16];
		snprintf(minutesArg, sizeof(minutesArg), "%d", minutes);
		execl(kGrantHelperPath, "vos-polkit-grant-helper", "grant",
			user, minutesArg, (char*)NULL);
		_exit(127);
	}

	close(inp[0]);
	FILE* wr = fdopen(inp[1], "w");
	if (wr == NULL) {
		close(inp[1]);
		waitpid(pid, NULL, 0);
		return false;
	}
	fprintf(wr, "%s\n", password);
	fclose(wr);

	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}


static void
run_forget_helper(const char* session)
{
	if (session == NULL || *session == '\0')
		return;

	pid_t pid = fork();
	if (pid < 0)
		return;

	if (pid == 0) {
		int nullFd = open("/dev/null", O_RDWR);
		if (nullFd >= 0) {
			dup2(nullFd, STDOUT_FILENO);
			dup2(nullFd, STDERR_FILENO);
			if (nullFd > STDERR_FILENO)
				close(nullFd);
		}
		execl(kGrantHelperPath, "vos-polkit-grant-helper", "forget",
			session, (char*)NULL);
		_exit(127);
	}

	int status;
	waitpid(pid, &status, 0);
}


struct HelperJob {
	BString		user;
	BString		cookie;
	BString		password;
	BMessenger	dialog;
};


static int32
helper_thread(void* data)
{
	HelperJob* job = (HelperJob*)data;
	bool ok = run_helper(job->user.String(), job->cookie.String(),
		job->password.String());
	BMessage result(kMsgAuthResult);
	result.AddBool("ok", ok);
	job->dialog.SendMessage(&result);
	delete job;
	return 0;
}


struct GrantJob {
	BString		user;
	BString		password;
	int32		minutes;
};


static int32
grant_thread(void* data)
{
	GrantJob* job = (GrantJob*)data;
	bool ok = run_grant_helper(job->user.String(), job->minutes,
		job->password.String());
	explicit_bzero(const_cast<char*>(job->password.String()),
		job->password.Length());

	BMessage result(kMsgGrantResult);
	result.AddBool("ok", ok);
	result.AddInt32("minutes", job->minutes);
	be_app->PostMessage(&result);
	delete job;
	return 0;
}


// ---- sd-bus glue ----------------------------------------------------------

// cookie -> live AuthDialog, so cancel_auth_method (bus thread) can reach a
// dialog owned by the app thread (BMessenger::SendMessage is cross-thread safe).
struct PendingAuth {
	BString    cookie;
	BMessenger dialog;
	bool       cancelPending;
	bool       inUse;
};

static const int kMaxPendingAuths = 8;
static PendingAuth sPendingAuths[kMaxPendingAuths];
static BLocker     sPendingAuthsLock;


// BeginAuthentication replies asynchronously via a worker thread; sd_bus is
// not safe for concurrent use, so the worker never touches the connection.
struct AuthReply {
	sd_bus_message*	msg;
	bool			ok;
	bool			inUse;
};

static const int kMaxAuthReplies = kMaxPendingAuths;
static AuthReply sAuthReplies[kMaxAuthReplies];
static BLocker   sAuthRepliesLock;


static bool
queue_auth_reply(sd_bus_message* msg, bool ok)
{
	BAutolock lock(sAuthRepliesLock);
	for (int i = 0; i < kMaxAuthReplies; i++) {
		if (!sAuthReplies[i].inUse) {
			sAuthReplies[i].inUse = true;
			sAuthReplies[i].msg = msg;
			sAuthReplies[i].ok = ok;
			return true;
		}
	}
	return false;
}


// Called only from the bus loop thread.
static void
flush_auth_replies()
{
	for (;;) {
		sd_bus_message* msg = NULL;
		bool ok = false;
		{
			BAutolock lock(sAuthRepliesLock);
			int i = 0;
			for (; i < kMaxAuthReplies; i++) {
				if (sAuthReplies[i].inUse)
					break;
			}
			if (i == kMaxAuthReplies)
				return;
			msg = sAuthReplies[i].msg;
			ok = sAuthReplies[i].ok;
			sAuthReplies[i].inUse = false;
			sAuthReplies[i].msg = NULL;
		}

		if (ok)
			sd_bus_reply_method_return(msg, "");
		else {
			sd_bus_reply_method_errorf(msg,
				"org.freedesktop.PolicyKit1.Error.Failed",
				"Authentication failed or cancelled");
		}
		sd_bus_message_unref(msg);
	}
}


static void
register_pending_auth(const char* cookie)
{
	BAutolock lock(sPendingAuthsLock);
	for (int i = 0; i < kMaxPendingAuths; i++) {
		if (!sPendingAuths[i].inUse) {
			sPendingAuths[i].inUse = true;
			sPendingAuths[i].cookie = cookie;
			sPendingAuths[i].dialog = BMessenger();
			sPendingAuths[i].cancelPending = false;
			return;
		}
	}
}


static void
unregister_pending_auth(const char* cookie)
{
	BAutolock lock(sPendingAuthsLock);
	for (int i = 0; i < kMaxPendingAuths; i++) {
		if (sPendingAuths[i].inUse && sPendingAuths[i].cookie == cookie) {
			sPendingAuths[i].inUse = false;
			sPendingAuths[i].dialog = BMessenger();
			return;
		}
	}
}


// True if a cancel already arrived before the dialog existed; caller must
// quit it immediately instead of leaving it up.
static bool
attach_pending_dialog(const char* cookie, BMessenger dialog)
{
	BAutolock lock(sPendingAuthsLock);
	for (int i = 0; i < kMaxPendingAuths; i++) {
		if (sPendingAuths[i].inUse && sPendingAuths[i].cookie == cookie) {
			sPendingAuths[i].dialog = dialog;
			return sPendingAuths[i].cancelPending;
		}
	}
	return false;
}


static void
cancel_pending_auth(const char* cookie)
{
	BAutolock lock(sPendingAuthsLock);
	for (int i = 0; i < kMaxPendingAuths; i++) {
		if (sPendingAuths[i].inUse && sPendingAuths[i].cookie == cookie) {
			// Never touch the reply port here; it's capacity 1, and if the
			// dialog also replies a second write would block it forever.
			if (sPendingAuths[i].dialog.IsValid())
				sPendingAuths[i].dialog.SendMessage(B_QUIT_REQUESTED);
			else
				sPendingAuths[i].cancelPending = true;
			return;
		}
	}
}


class AgentApp : public BApplication {
public:
	AgentApp();
	virtual ~AgentApp();

	virtual void	ReadyToRun();
	virtual	void	MessageReceived(BMessage* msg);

	bool	Authenticate(const AuthRequest& req);
	void	Revoke(const char* reason);

private:
	static	int32	_BusThread(void* self);
			int		_RunBus();
			void	_Notify(const char* title, const char* content);
			void	_CheckActiveGrant();

			sd_bus*		fBus;
			thread_id	fBusThread;
			volatile bool fRunning;
			BString		fSessionId;
			bool		fGrantActive;
			int32		fGrantMinutes;
			port_id		fLockPort;
};


AgentApp::AgentApp()
	:
	BApplication(kAppSignature),
	fBus(NULL),
	fBusThread(-1),
	fRunning(true),
	fGrantActive(false),
	fGrantMinutes(0),
	fLockPort(-1)
{
}


AgentApp::~AgentApp()
{
	fRunning = false;
	if (fBusThread >= 0) {
		status_t s;
		wait_for_thread(fBusThread, &s);
	}
	if (fBus != NULL)
		sd_bus_unref(fBus);
}


void
AgentApp::ReadyToRun()
{
	fBusThread = spawn_thread(_BusThread, "polkit-agent-bus",
		B_NORMAL_PRIORITY, this);
	if (fBusThread >= 0)
		resume_thread(fBusThread);
	else
		Quit();
}


static void show_dialog(BMessage* req);


void
AgentApp::MessageReceived(BMessage* msg)
{
	if (msg->what == kMsgLogin) {
		show_dialog(msg);
		return;
	}
	if (msg->what == kMsgAttempt) {
		HelperJob* job = new(std::nothrow) HelperJob;
		if (job == NULL)
			return;
		job->user = msg->GetString("user", "");
		job->cookie = msg->GetString("cookie", "");
		job->password = msg->GetString("password", "");
		if (msg->FindMessenger("dialog", &job->dialog) != B_OK
			|| !job->dialog.IsValid()) {
			delete job;
			return;
		}
		thread_id worker = spawn_thread(helper_thread, "polkit-agent-helper",
			B_NORMAL_PRIORITY, job);
		if (worker < 0 || resume_thread(worker) != B_OK) {
			if (worker >= 0)
				kill_thread(worker);
			delete job;
		}
		return;
	}
	if (msg->what == kMsgGrant) {
		GrantJob* job = new(std::nothrow) GrantJob;
		if (job == NULL)
			return;
		job->user = msg->GetString("user", "");
		job->password = msg->GetString("password", "");
		job->minutes = msg->GetInt32("minutes", 5);
		thread_id worker = spawn_thread(grant_thread,
			"polkit-agent-grant", B_NORMAL_PRIORITY, job);
		if (worker < 0 || resume_thread(worker) != B_OK) {
			if (worker >= 0)
				kill_thread(worker);
			delete job;
		}
		return;
	}
	if (msg->what == kMsgGrantResult) {
		if (msg->GetBool("revoked", false)) {
			_Notify(B_TRANSLATE("Admin access reminder cleared"),
				B_TRANSLATE("Password prompts are required "
					"again."));
			return;
		}
		if (!msg->GetBool("ok", false)) {
			syslog(LOG_AUTH | LOG_ERR, "vos-polkit-agent: grant "
				"failed");
			_Notify(B_TRANSLATE("Could not remember admin "
				"access"),
				B_TRANSLATE("The password was not saved; you "
					"will be asked again."));
			return;
		}
		fGrantActive = true;
		fGrantMinutes = msg->GetInt32("minutes", 5);
		BString content;
		content << B_TRANSLATE("System administration tasks will not "
				"ask for your password for the next ")
			<< fGrantMinutes
			<< B_TRANSLATE(" minutes. Click to forget now.");
		_Notify(B_TRANSLATE("Admin access remembered"),
			content.String());
		syslog(LOG_AUTH | LOG_NOTICE, "vos-polkit-agent: grant "
			"active for %d minutes", fGrantMinutes);
		return;
	}
	if (msg->what == kMsgSaveDuration) {
		int32 minutes = msg->GetInt32("minutes", 5);
		if (minutes == 1 || minutes == 5 || minutes == 15) {
			gSettings.duration = minutes;
			save_settings();
		}
		return;
	}
	if (msg->what == B_ARGV_RECEIVED) {
		const char* arg = NULL;
		if (msg->FindString("argv", 1, &arg) == B_OK && arg != NULL
			&& strcmp(arg, "forget") == 0)
			Revoke("user request");
		return;
	}
	BApplication::MessageReceived(msg);
}


void
AgentApp::Revoke(const char* reason)
{
	if (!fSessionId.IsEmpty())
		run_forget_helper(fSessionId.String());

	syslog(LOG_AUTH | LOG_NOTICE, "vos-polkit-agent: grant revoked "
		"(%s)", reason);

	if (fGrantActive) {
		fGrantActive = false;
		fGrantMinutes = 0;
		BMessage cleared(kMsgGrantResult);
		cleared.AddBool("revoked", true);
		cleared.AddString("reason", reason);
		PostMessage(&cleared);
	}
}


void
AgentApp::_Notify(const char* title, const char* content)
{
	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetTitle(title);
	notification.SetContent(content);
	notification.SetGroup("remembered-admin");
	notification.SetOnClickApp(kAppSignature);
	notification.AddOnClickArg("forget");
	notification.Send();
}


// An agent restart must not lose track of a live grant: the rule and
// its expiry live in /run, the helper owns the deadline.
void
AgentApp::_CheckActiveGrant()
{
	if (fSessionId.IsEmpty() || !grant_session_is_sane(fSessionId.String()))
		return;

	char path[256];
	snprintf(path, sizeof(path), "%s/%s.state", kGrantStateDir,
		fSessionId.String());

	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	char text[512];
	ssize_t length = read(fd, text, sizeof(text) - 1);
	close(fd);
	if (length <= 0)
		return;
	text[length] = '\0';

	GrantRecord record;
	if (!grant_parse_state(text, record))
		return;

	int64_t now = (int64_t)time(NULL);
	if (record.expiresUnix <= now)
		return;

	int32 remaining = (int32)((record.expiresUnix - now + 59) / 60);
	BMessage result(kMsgGrantResult);
	result.AddBool("ok", true);
	result.AddInt32("minutes", remaining);
	PostMessage(&result);
}


// logind escapes anything outside [A-Za-z0-9_] as _xHH in object paths.
static void
session_object_path(const char* session, char* out, size_t outSize)
{
	size_t used = 0;
	const char* prefix = "/org/freedesktop/login1/session/";
	for (size_t i = 0; prefix[i] != '\0' && used + 1 < outSize; i++)
		out[used++] = prefix[i];

	for (size_t i = 0; session[i] != '\0' && used + 5 < outSize; i++) {
		unsigned char c = (unsigned char)session[i];
		bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_';
		if (plain)
			out[used++] = (char)c;
		else {
			static const char* kHex = "0123456789abcdef";
			out[used++] = '_';
			out[used++] = 'x';
			out[used++] = kHex[(c >> 4) & 0xf];
			out[used++] = kHex[c & 0xf];
		}
	}
	out[used] = '\0';
}


// The revoke watcher's bus half: suspend, lock (logind path), session
// switch and, when enabled, network changes. Janus's direct lock
// notification arrives on fLockPort instead.
static int
logind_signal_method(sd_bus_message* message, void* userdata,
	sd_bus_error*)
{
	AgentApp* app = (AgentApp*)userdata;
	const char* member = sd_bus_message_get_member(message);
	if (member == NULL)
		return 0;

	if (strcmp(member, "PrepareForSleep") == 0) {
		int sleeping = 0;
		if (sd_bus_message_read(message, "b", &sleeping) >= 0
			&& sleeping && gSettings.revokeSuspend)
			app->Revoke("suspend");
	} else if (strcmp(member, "Lock") == 0) {
		if (gSettings.revokeLock)
			app->Revoke("screen lock");
	} else if (strcmp(member, "PropertiesChanged") == 0) {
		const char* interface = NULL;
		if (sd_bus_message_read(message, "s", &interface) < 0
			|| interface == NULL
			|| strcmp(interface, "org.freedesktop.login1.Session") != 0)
			return 0;

		const char* key = NULL;
		bool revoke = false;
		const char* reason = NULL;
		while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
			if (sd_bus_message_read(message, "s", &key) < 0) {
				sd_bus_message_exit_container(message);
				break;
			}
			if (key != NULL && strcmp(key, "Active") == 0) {
				int active = 1;
				if (sd_bus_message_read(message, "v", "b", &active)
						>= 0 && !active
					&& gSettings.revokeSession) {
					revoke = true;
					reason = "session switched";
				}
			} else if (key != NULL && strcmp(key, "LockedHint") == 0) {
				int locked = 0;
				if (sd_bus_message_read(message, "v", "b", &locked)
						>= 0 && locked && gSettings.revokeLock) {
					revoke = true;
					reason = "screen lock";
				}
			} else
				sd_bus_message_skip(message, "v");
			sd_bus_message_exit_container(message);
		}
		if (revoke)
			app->Revoke(reason);
	} else 	if (strcmp(member, "StateChanged") == 0) {
		// NetworkManager flips state on any connectivity change.
		if (gSettings.revokeNetwork)
			app->Revoke("network change");
	}
	return 0;
}


bool
AgentApp::Authenticate(const AuthRequest& req)
{
	port_id replyPort = create_port(1, "vos-polkit-reply");
	if (replyPort < 0) {
		fprintf(stderr, "vos-polkit-agent: create_port: %s\n",
			strerror(replyPort < 0 ? -replyPort : 0));
		return false;
	}

	register_pending_auth(req.cookie.String());

	BMessenger self(this);
	BMessage show(kMsgLogin);
	show.AddString("action_id", req.actionId);
	show.AddString("message",   req.message);
	show.AddString("icon",      req.iconName);
	show.AddString("cookie",    req.cookie);
	show.AddString("user",      req.identityUser);
	for (int32 i = 0; i < req.users.CountStrings(); i++)
		show.AddString("users", req.users.StringAt(i));
	show.AddInt32("reply_port", replyPort);
	self.SendMessage(&show, (BHandler*)NULL);

	char buf[4096];
	int32 code = 0;
	ssize_t n = read_port(replyPort, &code, buf, sizeof(buf));
	delete_port(replyPort);
	unregister_pending_auth(req.cookie.String());

	if (n < 0) {
		fprintf(stderr, "vos-polkit-agent: read_port: %s\n", strerror(-n));
		return false;
	}

	BMessage reply;
	if (reply.Unflatten(buf) != B_OK) {
		fprintf(stderr, "vos-polkit-agent: reply Unflatten failed\n");
		return false;
	}

	// Helper verdict arrives on the dialog path; this port carries cancel vs done.
	return reply.GetBool("ok", false);
}


struct AuthJob {
	AgentApp*		agent;
	AuthRequest		req;
	sd_bus_message*	msg;
};


// Runs the dialog off the bus loop. Never touches the bus connection: the
// verdict goes through queue_auth_reply().
static int32
auth_job_thread(void* data)
{
	AuthJob* job = (AuthJob*)data;
	bool ok = job->agent->Authenticate(job->req);
	if (!queue_auth_reply(job->msg, ok)) {
		fprintf(stderr, "vos-polkit-agent: reply queue full; dropping\n");
		sd_bus_message_unref(job->msg);
	}
	delete job;
	return 0;
}


static int
begin_auth_method(sd_bus_message* m, void* userdata, sd_bus_error* /*err*/)
{
	AuthRequest req;
	const char* action_id = NULL;
	const char* message   = NULL;
	const char* icon_name = NULL;
	const char* cookie    = NULL;

	int r = sd_bus_message_read(m, "sss", &action_id, &message, &icon_name);
	if (r < 0)
		return r;

	r = sd_bus_message_skip(m, "a{ss}");
	if (r < 0)
		return r;

	r = sd_bus_message_read(m, "s", &cookie);
	if (r < 0)
		return r;

	r = sd_bus_message_enter_container(m, 'a', "(sa{sv})");
	if (r < 0)
		return r;

	while ((r = sd_bus_message_enter_container(m, 'r', "sa{sv}")) > 0) {
		const char* kind = NULL;
		sd_bus_message_read(m, "s", &kind);
		if (kind != NULL && strcmp(kind, "unix-user") == 0) {
			BString userName;
			sd_bus_message_enter_container(m, 'a', "{sv}");
			while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
				const char* key = NULL;
				sd_bus_message_read(m, "s", &key);
				if (key != NULL && strcmp(key, "name") == 0) {
					const char* name = NULL;
					sd_bus_message_read(m, "v", "s", &name);
					if (name != NULL && *name != '\0')
						userName = name;
					sd_bus_message_exit_container(m);
				} else if (key != NULL && strcmp(key, "uid") == 0) {
					uint32_t uid = 0;
					sd_bus_message_enter_container(m, 'v', "u");
					sd_bus_message_read(m, "u", &uid);
					sd_bus_message_exit_container(m);
					if (userName.IsEmpty()) {
						struct passwd* pw = getpwuid((uid_t)uid);
						if (pw != NULL)
							userName = pw->pw_name;
					}
				} else {
					sd_bus_message_skip(m, "v");
				}
				sd_bus_message_exit_container(m);
			}
			sd_bus_message_exit_container(m);
			if (!userName.IsEmpty() && !req.users.HasString(userName))
				req.users.Add(userName);
		} else {
			sd_bus_message_skip(m, "a{sv}");
		}
		sd_bus_message_exit_container(m);
	}
	sd_bus_message_exit_container(m);

	req.actionId = action_id;
	req.message  = message;
	req.iconName = icon_name;
	req.cookie   = cookie;
	if (req.users.CountStrings() > 0)
		req.identityUser = req.users.First();
	if (req.identityUser.Length() == 0) {
		struct passwd* pw = getpwuid(getuid());
		if (pw != NULL) {
			req.identityUser = pw->pw_name;
			req.users.Add(req.identityUser);
		}
	}

	AuthJob* job = new(std::nothrow) AuthJob;
	if (job == NULL) {
		return sd_bus_reply_method_errorf(m,
			"org.freedesktop.PolicyKit1.Error.Failed", "out of memory");
	}
	job->agent = (AgentApp*)userdata;
	job->req = req;
	job->msg = sd_bus_message_ref(m);

	thread_id worker = spawn_thread(auth_job_thread, "polkit-agent-auth",
		B_NORMAL_PRIORITY, job);
	if (worker < 0 || resume_thread(worker) != B_OK) {
		if (worker >= 0)
			kill_thread(worker);
		sd_bus_message_unref(job->msg);
		delete job;
		return sd_bus_reply_method_errorf(m,
			"org.freedesktop.PolicyKit1.Error.Failed",
			"could not start authentication thread");
	}
	return 1;
}


static int
cancel_auth_method(sd_bus_message* m, void* /*userdata*/, sd_bus_error* /*err*/)
{
	const char* cookie = NULL;
	sd_bus_message_read(m, "s", &cookie);
	if (cookie != NULL)
		cancel_pending_auth(cookie);
	return sd_bus_reply_method_return(m, "");
}


// SD_BUS_VTABLE_UNPRIVILEGED is mandatory: sd_bus_open_system() is an
// untrusted connection; polkitd would be rejected without it.
static const sd_bus_vtable kAgentVtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("BeginAuthentication",
		"sssa{ss}sa(sa{sv})", "", begin_auth_method,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("CancelAuthentication", "s", "", cancel_auth_method,
		SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END
};


int32
AgentApp::_BusThread(void* self)
{
	return ((AgentApp*)self)->_RunBus();
}


int
AgentApp::_RunBus()
{
	// SYSTEM bus, not the session bus: polkitd owns org.freedesktop.PolicyKit1
	// there and calls back into our AuthenticationAgent object on it.
	int r = sd_bus_open_system(&fBus);
	if (r < 0) {
		fprintf(stderr, "vos-polkit-agent: sd_bus_open_system: %s\n", strerror(-r));
		return -1;
	}

	r = sd_bus_add_object_vtable(fBus, NULL, kAgentObjectPath,
		kAgentInterface, kAgentVtable, this);
	if (r < 0) {
		fprintf(stderr, "vos-polkit-agent: add_object_vtable: %s\n", strerror(-r));
		return -1;
	}

	const char* session = getenv("XDG_SESSION_ID");
	if (session == NULL || *session == '\0')
		session = "auto";
	fSessionId = session;

	// janus writes here after locking the screen; the logind matches
	// below cover the paths that do not go through janus.
	fLockPort = create_port(1, kLockWatchPortName);

	sd_bus_error err = SD_BUS_ERROR_NULL;
	r = sd_bus_call_method(fBus, kAuthorityBus, kAuthorityPath,
		kAuthorityIface, "RegisterAuthenticationAgent", &err, NULL,
		"(sa{sv})ss",
		"unix-session", 1, "session-id", "s", session,
		"C",	// locale
		kAgentObjectPath);
	if (r < 0) {
		fprintf(stderr, "vos-polkit-agent: RegisterAuthenticationAgent: %s\n",
			err.message != NULL ? err.message : strerror(-r));
		sd_bus_error_free(&err);
		return -1;
	}
	sd_bus_error_free(&err);
	printf("vos-polkit-agent: registered for session %s\n", session);

	sd_bus_add_match(fBus, NULL,
		"type='signal',sender='org.freedesktop.login1',"
		"interface='org.freedesktop.login1.Manager',"
		"member='PrepareForSleep'", logind_signal_method, this);
	sd_bus_add_match(fBus, NULL,
		"type='signal',sender='org.freedesktop.login1',"
		"interface='org.freedesktop.login1.Manager',member='Lock'",
		logind_signal_method, this);
	if (gSettings.revokeNetwork)
		sd_bus_add_match(fBus, NULL,
			"type='signal',sender='org.freedesktop.NetworkManager',"
			"interface='org.freedesktop.NetworkManager',"
			"member='StateChanged'", logind_signal_method, this);

	if (grant_session_is_sane(session)) {
		char sessionPath[256];
		session_object_path(session, sessionPath, sizeof(sessionPath));

		BString lockMatch = "type='signal',sender='org.freedesktop.login1',"
			"path='";
		lockMatch << sessionPath
			<< "',interface='org.freedesktop.login1.Session',"
			"member='Lock'";
		sd_bus_add_match(fBus, NULL, lockMatch.String(),
			logind_signal_method, this);

		BString propsMatch = "type='signal',sender='org.freedesktop.login1',"
			"path='";
		propsMatch << sessionPath
			<< "',interface='org.freedesktop.DBus.Properties',"
			"member='PropertiesChanged',"
			"arg0='org.freedesktop.login1.Session'";
		sd_bus_add_match(fBus, NULL, propsMatch.String(),
			logind_signal_method, this);
	}

	// A grant that predates an agent restart still owns the window.
	_CheckActiveGrant();

	while (fRunning) {
		flush_auth_replies();
		if (fLockPort >= 0 && gSettings.revokeLock) {
			int32 code = 0;
			char byte = 0;
			if (read_port_etc(fLockPort, &code, &byte, 1,
					B_RELATIVE_TIMEOUT, 0) == B_OK
				&& code == (int32)BPrivate::B_JANUS_SESSION_LOCKED)
				Revoke("screen lock");
		}
		r = sd_bus_process(fBus, NULL);
		if (r < 0)
			break;
		if (r > 0)
			continue;
		// The 500ms cap doubles as the poll interval for verdicts queued
		// by auth_job_thread, which cannot wake this loop itself.
		r = sd_bus_wait(fBus, 500 * 1000);
		if (r < 0)
			break;
	}
	flush_auth_replies();

	sd_bus_call_method(fBus, kAuthorityBus, kAuthorityPath,
		kAuthorityIface, "UnregisterAuthenticationAgent", NULL, NULL,
		"(sa{sv})s",
		"unix-session", 1, "session-id", "s", session,
		kAgentObjectPath);
	return 0;
}


static void
show_dialog(BMessage* req)
{
	int32 replyPort = req->GetInt32("reply_port", -1);
	if (replyPort < 0) {
		fprintf(stderr, "vos-polkit-agent: show_dialog: no reply_port\n");
		return;
	}

	AuthRequest r;
	r.actionId     = req->GetString("action_id", "");
	r.message      = req->GetString("message",   "Authenticate to continue");
	r.iconName     = req->GetString("icon",      "");
	r.cookie       = req->GetString("cookie",    "");
	r.identityUser = req->GetString("user",      "");
	const char* user = NULL;
	int32 index = 0;
	while (req->FindString("users", index++, &user) == B_OK) {
		if (user != NULL && *user != '\0' && !r.users.HasString(user))
			r.users.Add(BString(user));
	}
	if (r.users.CountStrings() == 0 && !r.identityUser.IsEmpty())
		r.users.Add(r.identityUser);

	BLooper* replyLooper = new BLooper("polkit-reply");
	class Forwarder : public BHandler {
	public:
		Forwarder(int32 port) : fPort(port) {}
		virtual void MessageReceived(BMessage* m) {
			if (m->what != kMsgClosed)
				return;
			ssize_t sz = m->FlattenedSize();
			char* buf = new char[sz];
			m->Flatten(buf, sz);
			write_port(fPort, kMsgClosed, buf, sz);
			delete[] buf;
			Looper()->Quit();
		}
	private:
		int32 fPort;
	};
	Forwarder* fwd = new Forwarder(replyPort);
	replyLooper->AddHandler(fwd);
	replyLooper->Run();

	AuthDialog* dlg = new AuthDialog(r, BMessenger(fwd, replyLooper),
		BMessenger(be_app));
	dlg->Show();

	if (attach_pending_dialog(r.cookie.String(), BMessenger(dlg)))
		dlg->PostMessage(B_QUIT_REQUESTED);
}


int
main(int, char**)
{
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	openlog("vos-polkit-agent", LOG_PID, LOG_AUTH);
	load_settings();
	AgentApp app;
	app.Run();
	return 0;
}
