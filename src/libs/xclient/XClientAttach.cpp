/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

// dlopen()ed by libX11 so X clients register with the roster; kept out of
// libX11's link line since libroot opens /dev/nexus at load.

#include <Application.h>
#include <Entry.h>
#include <Path.h>
#include <Roster.h>

#include <OS.h>

#include <xcb/xcb.h>

#include <ctype.h>
#include <dirent.h>
#include <algorithm>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <unistd.h>


#define XCB_FUNCTIONS(F) \
	F(xcb_connect) F(xcb_connection_has_error) F(xcb_disconnect) \
	F(xcb_flush) F(xcb_get_setup) F(xcb_setup_roots_iterator) \
	F(xcb_intern_atom) F(xcb_intern_atom_reply) F(xcb_query_tree) \
	F(xcb_query_tree_reply) F(xcb_query_tree_children) \
	F(xcb_query_tree_children_length) F(xcb_get_property) \
	F(xcb_get_property_reply) F(xcb_get_property_value) \
	F(xcb_get_property_value_length) F(xcb_get_window_attributes) \
	F(xcb_get_window_attributes_reply) F(xcb_send_event) \
	F(xcb_get_input_focus) F(xcb_get_input_focus_reply)

struct XcbApi {
#define DECLARE(name) __typeof__(&name) p_##name;
	XCB_FUNCTIONS(DECLARE)
#undef DECLARE
};


static bool
load_xcb(XcbApi& api)
{
#define RESOLVE(name) \
	api.p_##name = (__typeof__(&name))dlsym(RTLD_DEFAULT, #name); \
	if (api.p_##name == NULL) \
		return false;
	XCB_FUNCTIONS(RESOLVE)
#undef RESOLVE
	return true;
}


static xcb_atom_t
intern(XcbApi& x, xcb_connection_t* c, const char* name)
{
	xcb_intern_atom_reply_t* r = x.p_xcb_intern_atom_reply(c,
		x.p_xcb_intern_atom(c, 0, strlen(name), name), NULL);
	xcb_atom_t atom = r != NULL ? r->atom : XCB_ATOM_NONE;
	free(r);
	return atom;
}


// Asks this process' mapped toplevels to close, over a private connection.
static void
close_windows()
{
	XcbApi x;
	if (!load_xcb(x))
		return;
	xcb_connection_t* c = x.p_xcb_connect(NULL, NULL);
	if (c == NULL || x.p_xcb_connection_has_error(c) != 0) {
		if (c != NULL)
			x.p_xcb_disconnect(c);
		return;
	}

	xcb_atom_t pidAtom = intern(x, c, "_NET_WM_PID");
	xcb_atom_t protocols = intern(x, c, "WM_PROTOCOLS");
	xcb_atom_t del = intern(x, c, "WM_DELETE_WINDOW");
	xcb_window_t root = x.p_xcb_setup_roots_iterator(x.p_xcb_get_setup(c)).data->root;

	// Pass 0 dismisses open popups (their grab makes GTK ignore the delete),
	// pass 1 asks the toplevels to close.
	for (int pass = 0; pass < 2; pass++) {
	xcb_query_tree_reply_t* tree = x.p_xcb_query_tree_reply(c,
		x.p_xcb_query_tree(c, root), NULL);
	bool dismissed = false;
	if (tree != NULL && pidAtom != XCB_ATOM_NONE && protocols != XCB_ATOM_NONE
		&& del != XCB_ATOM_NONE) {
		xcb_window_t* kids = x.p_xcb_query_tree_children(tree);
		int count = x.p_xcb_query_tree_children_length(tree);
		for (int i = 0; i < count; i++) {
			xcb_get_window_attributes_reply_t* attr
				= x.p_xcb_get_window_attributes_reply(c,
					x.p_xcb_get_window_attributes(c, kids[i]), NULL);
			bool mapped = attr != NULL
				&& attr->map_state == XCB_MAP_STATE_VIEWABLE;
			bool popup = attr != NULL && attr->override_redirect;
			free(attr);
			if (!mapped)
				continue;

			xcb_get_property_reply_t* pid = x.p_xcb_get_property_reply(c,
				x.p_xcb_get_property(c, 0, kids[i], pidAtom, XCB_ATOM_CARDINAL,
					0, 1), NULL);
			bool ours = pid != NULL && x.p_xcb_get_property_value_length(pid)
					== (int)sizeof(uint32_t)
				&& *(uint32_t*)x.p_xcb_get_property_value(pid) == (uint32_t)getpid();
			free(pid);
			if (!ours)
				continue;
			if (pass == 0 || popup) {
				if (pass == 0 && popup) {
					for (int type = XCB_KEY_PRESS; type <= XCB_KEY_RELEASE;
							type++) {
						xcb_key_press_event_t key;
						memset(&key, 0, sizeof(key));
						key.response_type = type;
						key.detail = 9;	// Escape
						key.time = XCB_CURRENT_TIME;
						key.root = root;
						key.event = kids[i];
						key.same_screen = 1;
						x.p_xcb_send_event(c, 0, kids[i],
							XCB_EVENT_MASK_NO_EVENT, (const char*)&key);
					}
					dismissed = true;
				}
				continue;
			}

			xcb_get_property_reply_t* list = x.p_xcb_get_property_reply(c,
				x.p_xcb_get_property(c, 0, kids[i], protocols, XCB_ATOM_ATOM,
					0, 64), NULL);
			bool polite = false;
			if (list != NULL) {
				xcb_atom_t* atoms = (xcb_atom_t*)x.p_xcb_get_property_value(list);
				int n = x.p_xcb_get_property_value_length(list)
					/ (int)sizeof(xcb_atom_t);
				for (int j = 0; j < n; j++)
					polite = polite || atoms[j] == del;
			}
			free(list);
			if (!polite)
				continue;

			xcb_client_message_event_t event;
			memset(&event, 0, sizeof(event));
			event.response_type = XCB_CLIENT_MESSAGE;
			event.format = 32;
			event.window = kids[i];
			event.type = protocols;
			event.data.data32[0] = del;
			event.data.data32[1] = XCB_CURRENT_TIME;
			x.p_xcb_send_event(c, 0, kids[i], XCB_EVENT_MASK_NO_EVENT,
				(const char*)&event);
		}
	}
	free(tree);
	// Round trip: the server must handle the events before we disconnect.
	free(x.p_xcb_get_input_focus_reply(c, x.p_xcb_get_input_focus(c), NULL));
	x.p_xcb_flush(c);
	if (pass == 0 && dismissed)
		usleep(250000);
	}
	x.p_xcb_disconnect(c);
}


static std::string
sanitize(const std::string& in)
{
	std::string out;
	for (size_t i = 0; i < in.size(); i++) {
		unsigned char c = (unsigned char)in[i];
		out += (isalnum(c) || c == '-' || c == '_' || c == '+')
			? (char)tolower(c) : '_';
	}
	return out;
}


static std::string
real_path(const std::string& path)
{
	char buf[PATH_MAX];
	return realpath(path.c_str(), buf) != NULL ? std::string(buf)
		: std::string();
}


// Executable named by an Exec= line (env prefix skipped), resolved.
static std::string
exec_path(const std::string& line)
{
	std::vector<std::string> words;
	size_t i = 0;
	while (i < line.size()) {
		while (i < line.size() && isspace((unsigned char)line[i]))
			i++;
		if (i >= line.size())
			break;
		std::string w;
		if (line[i] == '"') {
			for (i++; i < line.size() && line[i] != '"'; i++) {
				if (line[i] == '\\' && i + 1 < line.size())
					i++;
				w += line[i];
			}
			i++;
		} else {
			for (; i < line.size() && !isspace((unsigned char)line[i]); i++)
				w += line[i];
		}
		words.push_back(w);
	}

	size_t at = 0;
	if (!words.empty()) {
		size_t slash = words[0].rfind('/');
		if ((slash == std::string::npos ? words[0] : words[0].substr(slash + 1))
				== "env") {
			for (at = 1; at < words.size(); at++) {
				if (words[at].find('=') == std::string::npos
					&& words[at][0] != '-') {
					break;
				}
			}
		}
	}
	if (at >= words.size() || words[at].empty())
		return std::string();

	const std::string& first = words[at];
	if (first.find('/') != std::string::npos)
		return real_path(first);
	const char* path = getenv("PATH");
	std::string dirs = path != NULL ? path : "/usr/local/bin:/usr/bin:/bin";
	for (size_t p = 0; p <= dirs.size();) {
		size_t e = dirs.find(':', p);
		if (e == std::string::npos)
			e = dirs.size();
		std::string candidate = dirs.substr(p, e - p) + "/" + first;
		if (access(candidate.c_str(), X_OK) == 0) {
			std::string r = real_path(candidate);
			if (!r.empty())
				return r;
		}
		p = e + 1;
	}
	return std::string();
}


// Fields of the desktop file that matter; false when the entry is hidden
// the way vos-desktop-sync hides it.
static bool
read_desktop(const std::string& file, std::string& exec)
{
	FILE* f = fopen(file.c_str(), "r");
	if (f == NULL)
		return false;
	bool inEntry = false, skip = false, isApp = false;
	std::string only, notIn;
	char* line = NULL;
	size_t cap = 0;
	exec.clear();
	while (getline(&line, &cap, f) > 0) {
		std::string l(line);
		while (!l.empty() && (l[l.size() - 1] == '\n' || l[l.size() - 1] == '\r'))
			l.erase(l.size() - 1);
		if (!l.empty() && l[0] == '[') {
			inEntry = l == "[Desktop Entry]";
			continue;
		}
		size_t eq = l.find('=');
		if (!inEntry || eq == std::string::npos)
			continue;
		std::string k = l.substr(0, eq), v = l.substr(eq + 1);
		if (k == "Exec")
			exec = v;
		else if (k == "Type")
			isApp = v == "Application";
		else if (k == "NoDisplay" || k == "Hidden" || k == "Terminal")
			skip = skip || v == "true";
		else if (k == "OnlyShowIn")
			only = v;
		else if (k == "NotShowIn")
			notIn = v;
	}
	free(line);
	fclose(f);

	std::string v = ";Vitruvian;";
	if (skip || !isApp || exec.empty())
		return false;
	if (!only.empty() && (";" + only + ";").find(v) == std::string::npos)
		return false;
	if ((";" + notIn + ";").find(v) != std::string::npos)
		return false;
	return true;
}


// Signature of the X/Qt client running exe: from the .desktop entry that
// launches it, else from the executable's name.
extern "C" int
vos_x_client_signature(const char* exe, char* out, size_t len)
{
	if (exe == NULL || out == NULL || len == 0)
		return -1;
	std::string self = real_path(exe);
	if (self.empty())
		self = exe;

	static const char* kDirs[] = {"/usr/share/applications",
		"/usr/local/share/applications"};
	// The plainest entry wins (no arguments beyond field codes), then the
	// earliest directory and name.
	std::string id;
	size_t bestArgs = 0;
	for (size_t d = 0; d < 2; d++) {
		std::vector<std::string> names;
		DIR* dir = opendir(kDirs[d]);
		if (dir == NULL)
			continue;
		while (struct dirent* e = readdir(dir)) {
			size_t n = strlen(e->d_name);
			if (n > 8 && strcmp(e->d_name + n - 8, ".desktop") == 0)
				names.push_back(e->d_name);
		}
		closedir(dir);
		std::sort(names.begin(), names.end());
		for (size_t i = 0; i < names.size(); i++) {
			std::string exec;
			if (!read_desktop(std::string(kDirs[d]) + "/" + names[i], exec))
				continue;
			if (exec_path(exec) != self)
				continue;
			size_t args = 0;
			for (size_t c = 0; c < exec.size(); c++) {
				if (isspace((unsigned char)exec[c]) && c + 1 < exec.size()
					&& !isspace((unsigned char)exec[c + 1])
					&& exec[c + 1] != '%') {
					args++;
				}
			}
			if (id.empty() || args < bestArgs) {
				id = names[i].substr(0, names[i].size() - 8);
				bestArgs = args;
			}
		}
	}

	if (id.empty()) {
		size_t slash = self.rfind('/');
		id = slash == std::string::npos ? self : self.substr(slash + 1);
	}
	std::string name = sanitize(id);
	if (name.empty())
		name = "client";
	std::string sig = "application/x-vnd.vos-x-" + name;
	if (sig.size() >= len)
		return -1;
	memcpy(out, sig.c_str(), sig.size() + 1);
	return 0;
}


class XClientApp : public BApplication {
public:
	XClientApp(const char* signature, status_t* error)
		:
		BApplication(signature, error, B_SINGLE_LAUNCH)
	{
	}

	virtual void ReadyToRun()
	{
		fReady = true;
	}

	virtual void ArgvReceived(int32 argc, char** argv)
	{
		if (fReady && argc > 1)
			Forward(argv + 1);
	}

	virtual void RefsReceived(BMessage* message)
	{
		if (!fReady)
			return;
		std::vector<std::string> paths;
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
			BPath path(&ref);
			if (path.InitCheck() == B_OK)
				paths.push_back(path.Path());
		}
		if (paths.empty())
			return;
		std::vector<char*> args;
		for (size_t i = 0; i < paths.size(); i++)
			args.push_back((char*)paths[i].c_str());
		args.push_back(NULL);
		Forward(args.data());
	}

	virtual bool QuitRequested()
	{
		close_windows();
		// Never block a logout.
		bool shutdown = false;
		BMessage* message = CurrentMessage();
		return message != NULL
			&& message->FindBool("_shutdown_", &shutdown) == B_OK && shutdown;
	}

private:
	// A second launch: let the binary's own single-instance logic take over.
	static void Forward(char** args)
	{
		char exe[PATH_MAX];
		ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
		if (n <= 0)
			return;
		exe[n] = '\0';
		std::vector<char*> argv;
		argv.push_back(exe);
		for (; *args != NULL; args++)
			argv.push_back(*args);
		argv.push_back(NULL);
		pid_t pid = fork();
		if (pid == 0) {
			if (fork() == 0) {
				setsid();
				execv(exe, argv.data());
			}
			_exit(0);
		}
		if (pid > 0)
			waitpid(pid, NULL, 0);
	}

	bool fReady = false;
};

// A forked child must not keep the parent's be_app; libbe poisons it and
// exit() would then drop into the debugger.
static void
forked_child(void)
{
	if (be_app == (BApplication*)-1)
		be_app = NULL;
}


static int32
app_thread(void* data)
{
	XClientApp* app = static_cast<XClientApp*>(data);
	app->LockLooper();
	app->Run();
	return 0;
}


extern "C" void
vos_x_client_attach(void)
{
	static bool attempted = false;
	if (attempted || be_app != NULL || access("/dev/nexus", F_OK) != 0)
		return;
	attempted = true;

	// A helper process of an app already listed (browser subprocesses).
	char self[PATH_MAX], parent[PATH_MAX], link[64];
	ssize_t selfLength = readlink("/proc/self/exe", self, sizeof(self));
	snprintf(link, sizeof(link), "/proc/%d/exe", (int)getppid());
	ssize_t parentLength = readlink(link, parent, sizeof(parent));
	if (selfLength > 0 && selfLength == parentLength
		&& memcmp(self, parent, selfLength) == 0) {
		return;
	}

	// Services live in libexec directories; they are not applications.
	if (selfLength > 0) {
		std::string exe(self, selfLength);
		if (exe.find("/libexec/") != std::string::npos)
			return;
	}

	char signature[B_MIME_TYPE_LENGTH];
	char exe[PATH_MAX];
	ssize_t exeLength = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (exeLength <= 0)
		return;
	exe[exeLength] = '\0';
	if (vos_x_client_signature(exe, signature, sizeof(signature)) != 0)
		return;

	// Run() loops on its own thread; the registrar drops the team on exit.
	status_t error = B_OK;
	XClientApp* app = new XClientApp(signature, &error);
	if (error != B_OK)
		return;
	app->Unlock();
	static bool forkHooked = false;
	if (!forkHooked) {
		forkHooked = true;
		pthread_atfork(NULL, NULL, forked_child);
	}
	thread_id thread = spawn_thread(app_thread, "x app", B_NORMAL_PRIORITY,
		app);
	if (thread >= B_OK)
		resume_thread(thread);
}
