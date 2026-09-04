/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#include <new>
#include <unistd.h>

#include <systemd/sd-bus.h>

#include <Application.h>
#include <Autolock.h>
#include <Box.h>
#include <Button.h>
#include <Locker.h>
#include <Messenger.h>
#include <Screen.h>
#include <StringView.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>


static const char* kAppSignature = "application/x-vnd.Vitruvian-polkit-agent";

static const char* kAgentObjectPath = "/org/vos/PolkitAgent";
static const char* kAgentInterface  = "org.freedesktop.PolicyKit1.AuthenticationAgent";
static const char* kAuthorityBus    = "org.freedesktop.PolicyKit1";
static const char* kAuthorityPath   = "/org/freedesktop/PolicyKit1/Authority";
static const char* kAuthorityIface  = "org.freedesktop.PolicyKit1.Authority";

static const char* kHelperPath = "/usr/lib/polkit-1/polkit-agent-helper-1";

static const uint32 kMsgLogin  = 'lgin';
static const uint32 kMsgCancel = 'cncl';
static const uint32 kMsgClosed = 'clsd';


struct AuthRequest {
	BString	actionId;
	BString	message;
	BString	iconName;
	BString	cookie;
	BString	identityUser;
};


class AuthDialog : public BWindow {
public:
			AuthDialog(const AuthRequest& req, BMessenger reply);

	virtual	void	MessageReceived(BMessage* msg);
	virtual	bool	QuitRequested();

private:
			void	_Send(bool ok);

			BTextControl*	fPassword;
			BStringView*	fStatus;
			BButton*		fLogin;
			BMessenger		fReply;
			bool			fSent;
};


AuthDialog::AuthDialog(const AuthRequest& req, BMessenger reply)
	:
	BWindow(BRect(0, 0, 460, 220), "Authenticate",
		B_MODAL_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
		B_NOT_MOVABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
		| B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS, B_ALL_WORKSPACES),
	fReply(reply),
	fSent(false)
{
	BView* top = new BView(Bounds(), "top", B_FOLLOW_ALL, B_WILL_DRAW);
	top->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	top->SetHighUIColor(B_PANEL_TEXT_COLOR);
	AddChild(top);

	BRect frame = Bounds();
	frame.InsetBy(10, 10);
	BBox* box = new BBox(frame, "box", B_FOLLOW_NONE);
	box->SetLabel("Authorization required");
	top->AddChild(box);

	BRect r(20, 30, 420, 55);
	BStringView* prompt = new BStringView(r, "prompt", req.message.String());
	box->AddChild(prompt);

	r.OffsetBy(0, 26);
	BString sub;
	sub.SetToFormat("Action: %s   User: %s",
		req.actionId.String(), req.identityUser.String());
	BStringView* details = new BStringView(r, "details", sub.String());
	box->AddChild(details);

	r.OffsetBy(0, 32);
	fPassword = new BTextControl(r, "password", "Password:", "",
		new BMessage(kMsgLogin), B_FOLLOW_NONE);
	fPassword->TextView()->HideTyping(true);
	fPassword->SetDivider(be_plain_font->StringWidth("Password:") + 12);
	box->AddChild(fPassword);

	r.OffsetBy(0, 32);
	fStatus = new BStringView(r, "status", "");
	fStatus->SetHighUIColor(B_FAILURE_COLOR);
	box->AddChild(fStatus);

	fLogin = new BButton(BRect(320, 150, 420, 175), "login",
		"Authenticate", new BMessage(kMsgLogin), B_FOLLOW_NONE);
	fLogin->MakeDefault(true);
	box->AddChild(fLogin);

	BButton* cancel = new BButton(BRect(220, 150, 315, 175), "cancel",
		"Cancel", new BMessage(kMsgCancel), B_FOLLOW_NONE);
	box->AddChild(cancel);

	fPassword->MakeFocus(true);

	BScreen s(this);
	BRect scr = s.Frame();
	MoveTo(scr.left + (scr.Width()  - Bounds().Width())  / 2,
	       scr.top  + (scr.Height() - Bounds().Height()) / 2);
}


void
AuthDialog::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgLogin:
			_Send(true);
			PostMessage(B_QUIT_REQUESTED);
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
	if (ok)
		reply.AddString("password", fPassword->Text());
	fReply.SendMessage(&reply);
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
	virtual void	MessageReceived(BMessage* msg);

	bool	Authenticate(const AuthRequest& req);

private:
	static	int32	_BusThread(void* self);
			int		_RunBus();

			sd_bus*		fBus;
			thread_id	fBusThread;
			volatile bool fRunning;
};


AgentApp::AgentApp()
	:
	BApplication(kAppSignature),
	fBus(NULL),
	fBusThread(-1),
	fRunning(true)
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
	BApplication::MessageReceived(msg);
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

	if (!reply.GetBool("ok", false))
		return false;

	const char* password = reply.GetString("password", "");
	return run_helper(req.identityUser.String(), req.cookie.String(), password);
}


struct AuthJob {
	AgentApp*		agent;
	AuthRequest		req;
	sd_bus_message*	msg;
};


// Runs the dialog + PAM helper off the bus loop. Never touches the bus
// connection: the verdict goes through queue_auth_reply().
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
		if (kind != NULL && strcmp(kind, "unix-user") == 0
				&& req.identityUser.Length() == 0) {
			sd_bus_message_enter_container(m, 'a', "{sv}");
			while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
				const char* key = NULL;
				sd_bus_message_read(m, "s", &key);
				if (key != NULL && strcmp(key, "uid") == 0) {
					uint32_t uid = 0;
					sd_bus_message_enter_container(m, 'v', "u");
					sd_bus_message_read(m, "u", &uid);
					sd_bus_message_exit_container(m);
					struct passwd* pw = getpwuid((uid_t)uid);
					if (pw != NULL)
						req.identityUser = pw->pw_name;
				} else {
					sd_bus_message_skip(m, "v");
				}
				sd_bus_message_exit_container(m);
			}
			sd_bus_message_exit_container(m);
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
	if (req.identityUser.Length() == 0) {
		struct passwd* pw = getpwuid(getuid());
		if (pw != NULL)
			req.identityUser = pw->pw_name;
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

	while (fRunning) {
		flush_auth_replies();
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

	AuthDialog* dlg = new AuthDialog(r, BMessenger(fwd, replyLooper));
	dlg->Show();

	if (attach_pending_dialog(r.cookie.String(), BMessenger(dlg)))
		dlg->PostMessage(B_QUIT_REQUESTED);
}


int
main(int, char**)
{
	prctl(PR_SET_PDEATHSIG, SIGTERM);
	AgentApp app;
	app.Run();
	return 0;
}
