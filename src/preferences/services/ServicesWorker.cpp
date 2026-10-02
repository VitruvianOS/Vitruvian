/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ServicesWorker.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-journal.h>

#include <Autolock.h>
#include <Catalog.h>
#include <Locale.h>
#include <Message.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ServicesWorker"


static const char* kSystemdBus		= "org.freedesktop.systemd1";
static const char* kSystemdPath		= "/org/freedesktop/systemd1";
static const char* kSystemdManager	= "org.freedesktop.systemd1.Manager";
static const char* kSystemdUnitIface	= "org.freedesktop.systemd1.Unit";
static const char* kUnitPathPrefix	= "/org/freedesktop/systemd1/unit/";

// Last N journal lines shown in the detail pane.
static const int32 kJournalLines = 50;
// Hard bound so a failed match cannot walk the whole journal.
static const int32 kJournalScanLimit = 4096;
// Units posted per kMsgUnitsBatch so the window stays responsive.
static const int32 kUnitBatchSize = 48;

enum unit_action {
	kActionStart = 1,
	kActionStop,
	kActionRestart,
	kActionEnable,
	kActionDisable
};


static char
_hexdigit(unsigned int v)
{
	return v < 10 ? (char)('0' + v) : (char)('a' + v - 10);
}


// systemd escapes '-' '.' '@' '_' '/' in unit object paths as \xHH.
static BString
_escape_unit_path(const BString& unit)
{
	BString out(kUnitPathPrefix);
	for (int32 i = 0; i < unit.Length(); i++) {
		unsigned char c = (unsigned char)unit.ByteAt(i);
		if (c == '-' || c == '.' || c == '@' || c == '_' || c == '/'
				|| c >= 0x80) {
			out << "\\x" << _hexdigit(c >> 4) << _hexdigit(c & 0x0f);
		} else {
			out << (char)c;
		}
	}
	return out;
}


// Reverse of _escape_unit_path, for PropertiesChanged signal paths.
static BString
_unescape_unit_path(const char* path)
{
	if (path == NULL)
		return BString();

	size_t prefixLen = strlen(kUnitPathPrefix);
	if (strncmp(path, kUnitPathPrefix, prefixLen) != 0)
		return BString();

	const char* rest = path + prefixLen;
	BString name;
	for (size_t i = 0; rest[i] != '\0'; i++) {
		if (rest[i] == '\\' && rest[i + 1] == 'x' && rest[i + 2] != '\0'
				&& rest[i + 3] != '\0') {
			char hex[3] = { rest[i + 2], rest[i + 3], '\0' };
			name << (char)strtol(hex, NULL, 16);
			i += 3;
		} else {
			name << rest[i];
		}
	}
	return name;
}


static void
_log_sd(const char* what, sd_bus_error* err, int r)
{
	fprintf(stderr, "ServicesWorker: %s failed: %s (%s)\n", what,
		err != NULL && err->message != NULL ? err->message : "no message",
		strerror(-r));
}


int32
ServicesWorker::_BusThreadEntry(void* self)
{
	return ((ServicesWorker*)self)->_BusLoop();
}


ServicesWorker::ServicesWorker(const BMessenger& owner)
	:
	Inherited("services worker", B_NORMAL_PRIORITY),
	fOwner(owner),
	fBus(NULL),
	fThread(-1),
	fBusRunning(false),
	fUsesUser(false),
	fBusLock("services bus")
{
	fWakePipe[0] = -1;
	fWakePipe[1] = -1;
}


ServicesWorker::~ServicesWorker()
{
	// Synchronous stop so the destructor does not race the bus thread.
	_Disconnect();
}


void
ServicesWorker::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSetManager:
		{
			const char* manager = "system";
			message->FindString("manager", &manager);
			_Connect(manager != NULL ? manager : "system");
			break;
		}

		case kMsgReload:
			_DoReload();
			break;

		case kMsgStartUnit:
		case kMsgStopUnit:
		case kMsgRestartUnit:
		case kMsgEnableUnit:
		case kMsgDisableUnit:
			_DoUnitAction(message);
			break;

		case kMsgLoadJournal:
			_DoLoadJournal(message);
			break;

		case kMsgShutdown:
			_Disconnect();
			Quit();
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


bool
ServicesWorker::_Connect(const char* manager)
{
	_Disconnect();

	bool user = manager != NULL && strcmp(manager, "user") == 0;

	sd_bus* bus = NULL;
	int r = user ? sd_bus_open_user(&bus) : sd_bus_open_system(&bus);
	if (r < 0) {
		char detail[256];
		snprintf(detail, sizeof(detail), "cannot open %s bus: %s",
			user ? "user" : "system", strerror(-r));
		_PostError("connect", detail);
		return false;
	}

	if (!user) {
		// Without this the system manager rejects unit actions from a
		// normal user; polkit then prompts through vos-polkit-agent.
		r = sd_bus_set_allow_interactive_authorization(bus, 1);
		if (r < 0)
			_log_sd("set allow interactive authorization", NULL, r);
	}

	if (pipe(fWakePipe) < 0) {
		char detail[128];
		snprintf(detail, sizeof(detail), "cannot create wake pipe: %s",
			strerror(errno));
		sd_bus_unref(bus);
		_PostError("connect", detail);
		return false;
	}

	{
		BAutolock lock(fBusLock);
		fBus = bus;
		fUsesUser = user;
	}

	// NULL callbacks: _BusLoop dispatches what sd_bus_process returns, so
	// nothing re-enters under fBusLock.
	{
		BAutolock lock(fBusLock);
		r = sd_bus_match_signal(bus, NULL, kSystemdBus, kSystemdPath,
			kSystemdManager, "UnitNew", NULL, NULL);
		if (r < 0)
			_log_sd("match UnitNew", NULL, r);
		r = sd_bus_match_signal(bus, NULL, kSystemdBus, kSystemdPath,
			kSystemdManager, "UnitRemoved", NULL, NULL);
		if (r < 0)
			_log_sd("match UnitRemoved", NULL, r);
		r = sd_bus_add_match(bus, NULL,
			"type='signal',"
			"interface='org.freedesktop.DBus.Properties',"
			"member='PropertiesChanged',"
			"arg0='org.freedesktop.systemd1.Unit'",
			NULL, NULL);
		if (r < 0)
			_log_sd("match PropertiesChanged", NULL, r);
	}

	fBusRunning = true;
	fThread = spawn_thread(_BusThreadEntry, "services_bus",
		B_NORMAL_PRIORITY, this);
	if (fThread < 0) {
		fBusRunning = false;
		BAutolock lock(fBusLock);
		sd_bus_unref(bus);
		fBus = NULL;
		fThread = -1;
		close(fWakePipe[0]);
		close(fWakePipe[1]);
		fWakePipe[0] = -1;
		fWakePipe[1] = -1;
		_PostError("connect", "cannot start bus thread");
		return false;
	}
	resume_thread(fThread);

	_DoReload();
	return true;
}


void
ServicesWorker::_Disconnect()
{
	if (fBus == NULL && fThread < 0)
		return;

	fBusRunning = false;

	// Wake the poll in _BusLoop; the bus is unref'd only after the join
	// so the poll never holds a dangling fd.
	if (fWakePipe[1] >= 0) {
		char c = 'q';
		write(fWakePipe[1], &c, 1);
	}

	if (fThread >= 0) {
		wait_for_thread(fThread, NULL);
		fThread = -1;
		BAutolock lock(fBusLock);
		if (fBus != NULL) {
			sd_bus_unref((sd_bus*)fBus);
			fBus = NULL;
		}
	}

	if (fWakePipe[0] >= 0) {
		close(fWakePipe[0]);
		fWakePipe[0] = -1;
	}
	if (fWakePipe[1] >= 0) {
		close(fWakePipe[1]);
		fWakePipe[1] = -1;
	}

	fUnits.MakeEmpty();
	fUsesUser = false;
}


void
ServicesWorker::_DoReload()
{
	if (!_IsConnected())
		return;

	// Units first so ListUnitFiles has a model to fill, then batch post.
	_DoListUnits();
	_DoListUnitFiles();
	_PostUnitBatches();
}


void
ServicesWorker::_DoListUnits()
{
	fUnits.MakeEmpty();

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	int r;

	{
		BAutolock lock(fBusLock);
		if (fBus == NULL)
			return;
		r = sd_bus_call_method((sd_bus*)fBus, kSystemdBus, kSystemdPath,
			kSystemdManager, "ListUnits", &err, &reply, NULL);
	}
	if (r < 0) {
		_log_sd("ListUnits", &err, r);
		_PostError("list", err.message != NULL ? err.message
			: strerror(-r));
		sd_bus_error_free(&err);
		sd_bus_message_unref(reply);
		return;
	}

	r = sd_bus_message_enter_container(reply, 'a', "(sssssouso)");
	while (r > 0) {
		const char* name = NULL;
		const char* description = NULL;
		const char* loadState = NULL;
		const char* activeState = NULL;
		const char* subState = NULL;
		const char* objectPath = NULL;
		uint32_t jobId = 0;
		const char* jobType = NULL;
		const char* jobPath = NULL;

		r = sd_bus_message_read(reply, "(sssssouso)", &name, &description,
			&loadState, &activeState, &subState, &objectPath, &jobId,
			&jobType, &jobPath);
		if (r < 0)
			break;

		ServicesUnit* unit = new ServicesUnit();
		unit->SetFromListTuple(name, description, loadState, activeState,
			subState);
		fUnits.AddItem(unit);

		r = sd_bus_message_enter_container(reply, 'a', "(sssssouso)");
	}
	sd_bus_message_exit_container(reply);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
}


void
ServicesWorker::_DoListUnitFiles()
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* reply = NULL;
	int r;

	{
		BAutolock lock(fBusLock);
		if (fBus == NULL)
			return;
		r = sd_bus_call_method((sd_bus*)fBus, kSystemdBus, kSystemdPath,
			kSystemdManager, "ListUnitFiles", &err, &reply, NULL);
	}
	if (r < 0) {
		// Non-fatal: the enabled column stays empty until this works.
		_log_sd("ListUnitFiles", &err, r);
		sd_bus_error_free(&err);
		sd_bus_message_unref(reply);
		return;
	}

	r = sd_bus_message_enter_container(reply, 'a', "(ss)");
	while (r > 0) {
		const char* path = NULL;
		const char* state = NULL;
		r = sd_bus_message_read(reply, "(ss)", &path, &state);
		if (r < 0)
			break;

		if (path != NULL && state != NULL) {
			const char* slash = strrchr(path, '/');
			const char* unitName = slash != NULL ? slash + 1 : path;
			for (int32 i = 0; i < fUnits.CountItems(); i++) {
				ServicesUnit* unit = fUnits.ItemAt(i);
				if (unit != NULL && unit->fName == unitName) {
					unit->fUnitFileState = state;
					break;
				}
			}
		}

		r = sd_bus_message_enter_container(reply, 'a', "(ss)");
	}
	sd_bus_message_exit_container(reply);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
}


void
ServicesWorker::_PostUnitBatches()
{
	int32 total = fUnits.CountItems();
	for (int32 start = 0; start < total; start += kUnitBatchSize) {
		int32 end = start + kUnitBatchSize;
		if (end > total)
			end = total;

		BObjectList<ServicesUnit, true>* batch
			= new BObjectList<ServicesUnit, true>();
		for (int32 i = start; i < end; i++) {
			ServicesUnit* unit = fUnits.ItemAt(i);
			if (unit != NULL)
				batch->AddItem(new ServicesUnit(*unit));
		}

		BMessage ready(kMsgUnitsBatch);
		ready.AddPointer("units", batch);
		ready.AddInt32("first", start);
		ready.AddInt32("total", total);
		ready.AddBool("user", _UsesUserManager());
		fOwner.SendMessage(&ready);
	}

	BMessage done(kMsgUnitsReady);
	done.AddInt32("count", total);
	done.AddBool("user", _UsesUserManager());
	fOwner.SendMessage(&done);
}


void
ServicesWorker::_DoUnitAction(BMessage* message)
{
	if (!_IsConnected())
		return;

	const char* name = "";
	if (message->FindString("unit", &name) != B_OK || name == NULL
			|| name[0] == '\0') {
		_PostError("action", "no unit selected");
		return;
	}

	const char* method = NULL;
	bool fileOp = false;
	const char* verb = "";
	switch (message->what) {
		case kMsgStartUnit:		method = "StartUnit";	verb = "start"; break;
		case kMsgStopUnit:		method = "StopUnit";	verb = "stop"; break;
		case kMsgRestartUnit:	method = "RestartUnit";	verb = "restart"; break;
		case kMsgEnableUnit:		method = "EnableUnitFiles"; verb = "enable";
			fileOp = true;
			break;
		case kMsgDisableUnit:	method = "DisableUnitFiles"; verb = "disable";
			fileOp = true;
			break;
		default:
			return;
	}

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message* call = NULL;
	sd_bus_message* callReply = NULL;
	int r;

	{
		BAutolock lock(fBusLock);
		if (fBus == NULL)
			return;
		sd_bus* bus = (sd_bus*)fBus;

		if (fileOp) {
			// EnableUnitFiles(as files, b runtime, b force);
			// DisableUnitFiles(as files, b runtime).
			r = sd_bus_message_new_method_call(bus, &call, kSystemdBus,
				kSystemdPath, kSystemdManager, method);
			if (r >= 0) {
				r = sd_bus_message_append(call, "as", 1, name);
				if (r >= 0) {
					if (message->what == kMsgEnableUnit)
						r = sd_bus_message_append(call, "bb", 0, 0);
					else
						r = sd_bus_message_append(call, "b", 0);
				}
				if (r >= 0)
					r = sd_bus_call(bus, call, 0, &err, &callReply);
			}
		} else {
			// Start/Stop/Restart: (s unit, s mode) -> o job
			r = sd_bus_call_method(bus, kSystemdBus, kSystemdPath,
				kSystemdManager, method, &err, &callReply, "ss", name,
				"replace");
		}
	}

	BMessage done(kMsgActionDone);
	done.AddString("unit", name);
	done.AddString("verb", verb);

	if (r < 0) {
		if (sd_bus_error_has_name(&err, SD_BUS_ERROR_ACCESS_DENIED)
			|| sd_bus_error_has_name(&err,
				SD_BUS_ERROR_INTERACTIVE_AUTHORIZATION_REQUIRED)) {
			// Prompt dismissed: show the real state, no alert.
			BMessage cancelled(kMsgError);
			cancelled.AddBool("cancelled", true);
			fOwner.SendMessage(&cancelled);
			_RefreshUnit(name);
		} else {
			_PostError("action", err.message != NULL ? err.message
				: strerror(-r));
		}
	} else {
		fOwner.SendMessage(&done);
		// Refresh UnitFileState so the Enabled column updates.
		_RefreshUnit(name);
	}

	sd_bus_error_free(&err);
	sd_bus_message_unref(call);
	sd_bus_message_unref(callReply);
}


void
ServicesWorker::_DoLoadJournal(BMessage* message)
{
	const char* name = "";
	if (message->FindString("unit", &name) != B_OK || name == NULL)
		name = "";

	bool user = false;
	message->FindBool("user", &user);

	BMessage reply(kMsgJournalReady);
	reply.AddString("unit", name);

	if (name[0] == '\0') {
		reply.AddString("text", "");
		fOwner.SendMessage(&reply);
		return;
	}

	int flags = user ? SD_JOURNAL_CURRENT_USER : SD_JOURNAL_LOCAL_ONLY;

	sd_journal* journal = NULL;
	int r = sd_journal_open(&journal, flags);
	if (r < 0) {
		char detail[128];
		snprintf(detail, sizeof(detail), "sd_journal_open: %s",
			strerror(-r));
		_PostError("journal", detail);
		return;
	}

	char match[256];
	snprintf(match, sizeof(match), "_SYSTEMD_UNIT=%s", name);
	sd_journal_add_match(journal, match, 0);

	sd_journal_seek_tail(journal);

	BString text;
	int32 count = 0;
	int32 scanned = 0;
	while (count < kJournalLines && scanned < kJournalScanLimit) {
		r = sd_journal_previous(journal);
		if (r <= 0)
			break;
		scanned++;

		const void* data = NULL;
		size_t length = 0;
		BString line;
		if (sd_journal_get_data(journal, "MESSAGE", &data, &length) >= 0
				&& data != NULL && length > 7) {
			// "MESSAGE=" is 8 bytes... 7 would drop one char.
			line.SetTo((const char*)data + 8, length - 8);
		} else {
			line = B_TRANSLATE("(no message)");
		}

		data = NULL;
		length = 0;
		if (sd_journal_get_data(journal, "__REALTIME_TIMESTAMP",
				&data, &length) >= 0 && data != NULL && length > 21) {
			BString stamp;
			stamp.SetTo((const char*)data + 21, length - 21);
			BString prefixed;
			prefixed << "[" << stamp << "] " << line;
			line = prefixed;
		}

		if (text.Length() > 0)
			text << "\n";
		text << line;
		count++;
	}

	sd_journal_close(journal);

	reply.AddString("text", text.String());
	fOwner.SendMessage(&reply);
}


void
ServicesWorker::_RefreshUnit(const BString& name)
{
	if (!_IsConnected() || name.Length() == 0)
		return;

	BString path = _UnitPath(name);
	BString unitPath = path;
	BString active;
	BString sub;
	BString description;
	BString loadState;
	BString fileState;

	{
		BAutolock lock(fBusLock);
		if (fBus == NULL)
			return;

		sd_bus* bus = (sd_bus*)fBus;
		sd_bus_error err = SD_BUS_ERROR_NULL;
		sd_bus_message* reply = NULL;
		int r = sd_bus_call_method(bus, kSystemdBus, kSystemdPath,
			kSystemdManager, "GetUnit", &err, &reply, "s", name.String());
		if (r >= 0) {
			const char* canonical = NULL;
			if (sd_bus_message_read(reply, "o", &canonical) >= 0
					&& canonical != NULL)
				unitPath = canonical;
		}
		sd_bus_message_unref(reply);
		sd_bus_error_free(&err);

		const char* props[] = { "ActiveState", "SubState", "Description",
			"LoadState" };
		BString* outs[] = { &active, &sub, &description, &loadState };
		for (int32 i = 0; i < 4; i++) {
			sd_bus_message* propReply = NULL;
			sd_bus_error propErr = SD_BUS_ERROR_NULL;
			r = sd_bus_get_property(bus, kSystemdBus, unitPath.String(),
				kSystemdUnitIface, props[i], &propErr, &propReply, "s");
			if (r >= 0) {
				const char* value = NULL;
				if (sd_bus_message_read(propReply, "s", &value) >= 0
						&& value != NULL)
					*outs[i] = value;
			}
			sd_bus_message_unref(propReply);
			sd_bus_error_free(&propErr);
		}

		// UnitFileState lives on the Manager, not the Unit interface.
		sd_bus_message* fileReply = NULL;
		sd_bus_error fileErr = SD_BUS_ERROR_NULL;
		r = sd_bus_call_method(bus, kSystemdBus, kSystemdPath,
			kSystemdManager, "GetUnitFileState", &fileErr, &fileReply,
			"s", name.String());
		if (r >= 0) {
			const char* state = NULL;
			if (sd_bus_message_read(fileReply, "s", &state) >= 0
					&& state != NULL)
				fileState = state;
		}
		sd_bus_message_unref(fileReply);
		sd_bus_error_free(&fileErr);
	}

	ServicesUnit unit;
	unit.SetFromListTuple(name.String(), description.String(),
		loadState.String(), active.String(), sub.String());
	unit.fUnitFileState = fileState;

	{
		BAutolock lock(fBusLock);
		bool replaced = false;
		for (int32 i = 0; i < fUnits.CountItems(); i++) {
			ServicesUnit* existing = fUnits.ItemAt(i);
			if (existing != NULL && existing->fName == name) {
				*existing = unit;
				replaced = true;
				break;
			}
		}
		if (!replaced)
			fUnits.AddItem(new ServicesUnit(unit));
	}

	_PostUnit(unit);
}


void
ServicesWorker::_ApplyUnitProperty(const BString& name, const char* key,
	const char* value)
{
	if (value == NULL)
		return;

	ServicesUnit* target = NULL;
	for (int32 i = 0; i < fUnits.CountItems(); i++) {
		ServicesUnit* existing = fUnits.ItemAt(i);
		if (existing != NULL && existing->fName == name) {
			target = existing;
			break;
		}
	}

	bool created = false;
	if (target == NULL) {
		target = new ServicesUnit();
		target->SetFromListTuple(name.String(), NULL, NULL, NULL, NULL);
		fUnits.AddItem(target);
		created = true;
	}

	if (strcmp(key, "ActiveState") == 0)
		target->fActiveState = value;
	else if (strcmp(key, "SubState") == 0)
		target->fSubState = value;
	else if (strcmp(key, "Description") == 0)
		target->fDescription = value;
	else if (strcmp(key, "LoadState") == 0)
		target->fLoadState = value;
	else if (!created)
		return;

	_PostUnit(*target);
}


void
ServicesWorker::_PostUnit(const ServicesUnit& unit)
{
	BMessage message(kMsgUnitUpdated);
	unit.AddTo(&message, "unit");
	message.AddBool("user", _UsesUserManager());
	fOwner.SendMessage(&message);
}


void
ServicesWorker::_PostError(const char* operation, const char* detail)
{
	BMessage message(kMsgError);
	message.AddString("operation", operation != NULL ? operation : "");
	message.AddString("detail", detail != NULL ? detail : "");
	fOwner.SendMessage(&message);
}


int32
ServicesWorker::_BusLoop()
{
	while (fBusRunning) {
		sd_bus* bus = NULL;
		int fd = -1;
		uint32_t events = POLLIN;
		uint64_t timeout = (uint64_t)-1;

		{
			BAutolock lock(fBusLock);
			bus = (sd_bus*)fBus;
			if (bus == NULL)
				break;
			fd = sd_bus_get_fd(bus);
			events = sd_bus_get_events(bus);
			sd_bus_get_timeout(bus, &timeout);
		}

		struct pollfd fds[2];
		fds[0].fd = fd;
		fds[0].events = (short)(events != 0 ? events : POLLIN);
		fds[0].revents = 0;
		fds[1].fd = fWakePipe[0];
		fds[1].events = POLLIN;
		fds[1].revents = 0;

		int waitMs = 200;
		if (timeout != (uint64_t)-1) {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			uint64_t nowUs = (uint64_t)now.tv_sec * 1000000
				+ (uint64_t)now.tv_nsec / 1000;
			if (timeout <= nowUs)
				waitMs = 0;
			else {
				uint64_t deltaMs = (timeout - nowUs + 999) / 1000;
				waitMs = deltaMs > 60000 ? 60000 : (int)deltaMs;
			}
		}

		int r = poll(fds, 2, waitMs);
		if (fds[1].revents & POLLIN)
			break;
		if (r < 0 && errno != EINTR)
			break;

		sd_bus_message* m = NULL;
		{
			BAutolock lock(fBusLock);
			if (fBus == NULL)
				break;
			r = sd_bus_process((sd_bus*)fBus, &m);
		}

		if (r < 0)
			break;

		if (m != NULL) {
			_HandleSignal(m);
			sd_bus_message_unref(m);
			continue;
		}
	}
	return 0;
}


void
ServicesWorker::_HandleSignal(void* message)
{
	sd_bus_message* m = (sd_bus_message*)message;
	if (m == NULL)
		return;

	const char* member = sd_bus_message_get_member(m);
	if (member == NULL)
		return;

	const char* path = sd_bus_message_get_path(m);

	if (strcmp(member, "UnitNew") == 0) {
		const char* name = NULL;
		const char* ignored = NULL;
		if (sd_bus_message_read(m, "so", &name, &ignored) >= 0
				&& name != NULL)
			_RefreshUnit(name);
		return;
	}

	if (strcmp(member, "UnitRemoved") == 0) {
		const char* name = NULL;
		const char* ignored = NULL;
		if (sd_bus_message_read(m, "so", &name, &ignored) >= 0
				&& name != NULL) {
			BMessage gone(kMsgUnitRemoved);
			gone.AddString("unit", name);
			gone.AddBool("user", _UsesUserManager());
			fOwner.SendMessage(&gone);
		}
		return;
	}

	if (strcmp(member, "PropertiesChanged") == 0) {
		const char* iface = NULL;
		if (sd_bus_message_read(m, "s", &iface) < 0 || iface == NULL)
			return;
		if (strcmp(iface, kSystemdUnitIface) != 0)
			return;

		BString name = _unescape_unit_path(path);
		if (name.Length() == 0)
			return;

		// Apply the signal's own dict; re-fetching per signal froze the
		// window under storms.
		_HandlePropertiesChanged(m, name);
		return;
	}
}


void
ServicesWorker::_HandlePropertiesChanged(void* message, const BString& name)
{
	sd_bus_message* m = (sd_bus_message*)message;
	if (m == NULL)
		return;

	int r = sd_bus_message_enter_container(m, 'a', "{sv}");
	while (r > 0) {
		r = sd_bus_message_enter_container(m, 'e', "sv");
		if (r < 0)
			break;

		const char* key = NULL;
		if (sd_bus_message_read(m, "s", &key) >= 0 && key != NULL
				&& (strcmp(key, "ActiveState") == 0
					|| strcmp(key, "SubState") == 0
					|| strcmp(key, "Description") == 0
					|| strcmp(key, "LoadState") == 0)) {
			if (sd_bus_message_enter_container(m, 'v', "s") >= 0) {
				const char* value = NULL;
				if (sd_bus_message_read(m, "s", &value) >= 0)
					_ApplyUnitProperty(name, key, value);
				sd_bus_message_exit_container(m);
			} else {
				sd_bus_message_skip(m, "v");
			}
		} else {
			sd_bus_message_skip(m, "v");
		}

		sd_bus_message_exit_container(m);
		r = sd_bus_message_enter_container(m, 'a', "{sv}");
	}
	sd_bus_message_exit_container(m);
}


BString
ServicesWorker::_UnitPath(const BString& unit)
{
	return _escape_unit_path(unit);
}
