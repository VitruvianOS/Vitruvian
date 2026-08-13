/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "BlueZBackend.h"

#include <Autolock.h>
#include <Messenger.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>

#include <algorithm>

#include <GLibSyncTimeout.h>


//! Upper bound on the blocking g_bus_get_sync() during singleton construction.
static const bigtime_t kInitTimeout = 10 * 1000000LL;


static gboolean
_QuitLoop(gpointer data)
{
	g_main_loop_quit((GMainLoop*)data);
	return G_SOURCE_REMOVE;
}


BlueZBackend*
BlueZBackend::Instance()
{
	// Deliberately leaked: a function-local static's destructor runs at
	// exit() on whatever thread called exit(), not the dispatch thread --
	// quitting fMainLoop from there races _DispatchThread(). Leaking is the
	// standard fix; the process is exiting anyway.
	static BlueZBackend* instance = new BlueZBackend();
	return instance;
}


BlueZBackend::BlueZBackend()
	: fBluezWatcherId(0),
	fBluezAvailable(false),
	fSnapshotPopulated(false),
	fSnapshotGeneration(0),
	fDaemonHealthy(false),
	fQueryInFlight(false),
	fBackoffUntil(0),
	fCurrentBackoffUs(0),
	fBlueZConnection(NULL),
	fMainContext(NULL),
	fMainLoop(NULL),
	fDispatchThread(-1),
	fInitThread(-1),
	fAgentRegistrationId(0),
	fPropertiesChangedSubscriptionId(0),
	fInterfacesAddedSubscriptionId(0),
	fInterfacesRemovedSubscriptionId(0)
{
	// Instance() is typically first touched from a window thread (the
	// replicant's AttachedToWindow(), the preflet's constructor). _InitBlueZ()
	// blocks on g_bus_get_sync() plus org.bluez auto-activation, bounded by
	// kInitTimeout but still far too slow for a window thread -- so run it on
	// a dedicated one-shot thread instead of inline here. Every public method
	// below already null-checks fBlueZConnection/fMainContext, so callers
	// that land before init completes get a clean "not ready" rather than a
	// block; the next poll succeeds once init finishes.
	fInitThread = spawn_thread(_InitThreadEntry, "bluez_init",
		B_NORMAL_PRIORITY, this);
	if (fInitThread >= B_OK)
		resume_thread(fInitThread);
}


BlueZBackend::~BlueZBackend()
{
	if (fInitThread >= B_OK) {
		status_t result;
		wait_for_thread(fInitThread, &result);
	}
	_CleanupBlueZ();
}


int32
BlueZBackend::_InitThreadEntry(void* data)
{
	((BlueZBackend*)data)->_InitBlueZ();
	return 0;
}


int32
BlueZBackend::_DispatchThreadEntry(void* data)
{
	BlueZBackend* backend = (BlueZBackend*)data;
	backend->_DispatchThread();
	return 0;
}


void
BlueZBackend::_DispatchThread()
{
	if (fMainLoop == NULL)
		return;

	// Kept pushed for the thread's whole life: g_bus_watch_name() (see
	// _SetupBluezWatch) runs here via g_main_context_invoke and binds to
	// whatever context is thread-default *on this thread*, not whichever
	// context g_main_context_invoke happens to be dispatching.
	g_main_context_push_thread_default((GMainContext*)fMainContext);
	g_main_loop_run((GMainLoop*)fMainLoop);
	g_main_context_pop_thread_default((GMainContext*)fMainContext);
}


bool
BlueZBackend::_InitBlueZ()
{
	// Create dedicated GMainContext for BlueZ operations
	{
		// fLock also guards the fMainContext != NULL check in
		// _RunOnDispatchThread()/StartWatching() -- a caller on another
		// thread (typically AttachedToWindow(), racing this init thread)
		// must see either a fully-NULL or fully-valid pointer, never a
		// torn write.
		BAutolock lock(fLock);
		fMainContext = g_main_context_new();
	}
	if (fMainContext == NULL) {
		fprintf(stderr, "Failed to create GMainContext for BlueZ\n");
		// fMainContext will never become non-NULL now (this backend only
		// gets one _InitBlueZ() attempt) -- fail out any caller queued in
		// _RunOnDispatchThread() rather than leaving it waiting forever.
		_FailPendingDispatchJobs();
		return false;
	}

	// Anything that raced Instance() before fMainContext existed is queued
	// in fPendingDispatchJobs; drain it now that g_main_context_invoke() has
	// somewhere to deliver to (see _RunOnDispatchThread's comment -- the
	// invoke is harmless to issue before the dispatch thread starts running
	// this context's loop, it just waits).
	_FlushPendingDispatchJobs();

	// Create GMainLoop for this context
	fMainLoop = g_main_loop_new((GMainContext*)fMainContext, FALSE);
	if (fMainLoop == NULL) {
		fprintf(stderr, "Failed to create GMainLoop for BlueZ\n");
		_CleanupBlueZ();
		return false;
	}

	// The dispatch thread must NOT be running yet: g_main_loop_run() acquires
	// fMainContext, and g_main_context_push_thread_default() acquires it too
	// and g_return_if_fail()s if another thread already owns it -- which would
	// silently bind the connection to the (never-iterated) default context.
	g_main_context_push_thread_default((GMainContext*)fMainContext);

	// g_bus_get_sync() has no timeout; an absent/wedged system bus would
	// otherwise freeze this thread -- Deskbar's window thread included.
	GError* error = NULL;
	BPrivate::GLibSyncTimeout guard(kInitTimeout);
	fBlueZConnection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, guard.Cancellable(),
		&error);
	guard.Stop();

	g_main_context_pop_thread_default((GMainContext*)fMainContext);

	if (fBlueZConnection == NULL) {
		fprintf(stderr, "Failed to connect to system D-Bus: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
		_CleanupBlueZ();
		return false;
	}

	// Only now start pumping fMainContext. Spawning after the connection
	// exists also means the failure path above never has to join a thread that
	// may not have entered g_main_loop_run() yet (see _CleanupBlueZ).
	fDispatchThread = spawn_thread(_DispatchThreadEntry, "bluetooth_dispatch",
		B_NORMAL_PRIORITY, this);
	if (fDispatchThread < B_OK) {
		fprintf(stderr, "Failed to spawn BlueZ dispatch thread\n");
		_CleanupBlueZ();
		return false;
	}
	resume_thread(fDispatchThread);

	g_main_context_invoke((GMainContext*)fMainContext, _SetupBluezWatchSource,
		this);

	// Subscribes signals (also replays any StartWatching() that queued a
	// watcher into fWatchers before fBlueZConnection existed -- without this
	// a replicant that called StartWatching() from AttachedToWindow() got no
	// subscription, ever) strictly before kicking the initial snapshot fill.
	// The fill itself is async now (_StartSnapshotQuery) and returns
	// immediately; any PropertiesChanged/InterfacesAdded/InterfacesRemoved
	// that arrives while it's in flight is caught by the generation counter
	// in _HandleSnapshotQueryReply rather than by blocking on fLock.
	{
		BAutolock lock(fLock);
		_SubscribeSignalsLocked();
	}
	_StartSnapshotQuery();

	return true;
}


gboolean
BlueZBackend::_SetupBluezWatchSource(gpointer cookie)
{
	((BlueZBackend*)cookie)->_SetupBluezWatch();
	return G_SOURCE_REMOVE;
}


void
BlueZBackend::_SetupBluezWatch()
{
	fBluezWatcherId = g_bus_watch_name(G_BUS_TYPE_SYSTEM, "org.bluez",
		G_BUS_NAME_WATCHER_FLAGS_NONE, _OnBluezNameAppeared,
		_OnBluezNameVanished, this, NULL);
}


void
BlueZBackend::_OnBluezNameAppeared(GDBusConnection* connection,
	const char* name, const char* nameOwner, void* userData)
{
	BlueZBackend* backend = (BlueZBackend*)userData;

	{
		BAutolock lock(backend->fLock);
		backend->fBluezAvailable = true;
		// Name ownership reappearing is new information regardless of any
		// backoff accumulated from a previous, differently-owned incarnation
		// of the name -- give it an immediate attempt.
		backend->fBackoffUntil = 0;
		backend->fCurrentBackoffUs = 0;
	}

	// _StartSnapshotQuery() only queues work (g_main_context_invoke) and
	// returns immediately -- safe to call from here even though this
	// callback itself runs on the dispatch thread pumping fMainContext.
	// fQueryInFlight (checked inside it) naturally coalesces a burst of
	// appear/vanish/appear into whatever query is already outstanding.
	backend->_StartSnapshotQuery();
}


void
BlueZBackend::_OnBluezNameVanished(GDBusConnection* connection,
	const char* name, void* userData)
{
	BlueZBackend* backend = (BlueZBackend*)userData;
	BAutolock lock(backend->fLock);
	backend->fBluezAvailable = false;
	backend->fDaemonHealthy = false;

	// bluetoothd is gone -- its whole object tree is gone with it. An empty
	// cache is the honest state, not a guess; _OnBluezNameAppeared refills
	// it from scratch if/when the daemon comes back.
	backend->_ClearSnapshotLocked();
}


bool
BlueZBackend::IsServiceAvailable()
{
	BAutolock lock(fLock);
	return fBluezAvailable;
}


void
BlueZBackend::_CleanupBlueZ()
{
	// Best-effort: leaving a stale Agent1 registration on the bus after this
	// process exits would make BlueZ keep trying to call a dead endpoint.
	_UnregisterAgent();

	// g_bus_unwatch_name() is safe from any thread and guarantees neither
	// callback fires again once it returns.
	if (fBluezWatcherId != 0) {
		g_bus_unwatch_name(fBluezWatcherId);
		fBluezWatcherId = 0;
	}

	if (fBlueZConnection != NULL && fPropertiesChangedSubscriptionId != 0) {
		g_dbus_connection_signal_unsubscribe((GDBusConnection*)fBlueZConnection,
			fPropertiesChangedSubscriptionId);
		fPropertiesChangedSubscriptionId = 0;
	}
	if (fBlueZConnection != NULL && fInterfacesAddedSubscriptionId != 0) {
		g_dbus_connection_signal_unsubscribe((GDBusConnection*)fBlueZConnection,
			fInterfacesAddedSubscriptionId);
		fInterfacesAddedSubscriptionId = 0;
	}
	if (fBlueZConnection != NULL && fInterfacesRemovedSubscriptionId != 0) {
		g_dbus_connection_signal_unsubscribe((GDBusConnection*)fBlueZConnection,
			fInterfacesRemovedSubscriptionId);
		fInterfacesRemovedSubscriptionId = 0;
	}

	// Quit from inside the loop. g_main_loop_run() unconditionally sets
	// is_running=TRUE on entry (glib gmain.c:4514), so a quit issued before
	// the dispatch thread gets there is lost and wait_for_thread() below
	// would block forever.
	if (fDispatchThread >= B_OK && fMainLoop != NULL) {
		g_main_context_invoke((GMainContext*)fMainContext, _QuitLoop,
			fMainLoop);
		status_t result;
		wait_for_thread(fDispatchThread, &result);
		fDispatchThread = -1;
	}

	// Cleanup D-Bus connection
	if (fBlueZConnection != NULL) {
		g_object_unref(fBlueZConnection);
		fBlueZConnection = NULL;
	}

	// Cleanup main loop and context
	if (fMainLoop != NULL) {
		g_main_loop_unref((GMainLoop*)fMainLoop);
		fMainLoop = NULL;
	}

	if (fMainContext != NULL) {
		g_main_context_unref((GMainContext*)fMainContext);
		fMainContext = NULL;
	}
}


// #pragma mark - _RunOnDispatchThread: async request/reply, no blocking


struct _BlueZDispatchJob {
	BlueZBackend::DispatchFunc func;
	void* cookie;
	BMessenger replyTo;
	uint32 replyWhat;
};


static gboolean
_RunBlueZDispatchJob(gpointer data)
{
	_BlueZDispatchJob* job = (_BlueZDispatchJob*)data;

	BMessage reply(job->replyWhat);
	job->func(job->cookie, &reply);	// func frees cookie itself
	job->replyTo.SendMessage(&reply);

	delete job;
	return G_SOURCE_REMOVE;
}


status_t
BlueZBackend::_RunOnDispatchThread(DispatchFunc func, void* cookie,
	const BMessenger& replyTo, uint32 replyWhat)
{
	_BlueZDispatchJob* job = new _BlueZDispatchJob;
	job->func = func;
	job->cookie = cookie;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	BAutolock lock(fLock);

	if (fMainContext == NULL) {
		// bluez_init (the one-shot thread spawned by the constructor)
		// hasn't created fMainContext yet -- queue instead of failing
		// immediately. AttachedToWindow() commonly calls
		// RegisterAgentAsync()/GetDevicesAsync() etc. within this window;
		// _FlushPendingDispatchJobs() drains this the moment the context
		// exists (the queued g_main_context_invoke() is harmless to issue
		// before the dispatch thread starts running that context's loop --
		// it just waits). _FailPendingDispatchJobs() replies these out if
		// init ultimately fails outright (system bus unreachable).
		fPendingDispatchJobs.push_back(job);
		return B_OK;
	}

	g_main_context_invoke((GMainContext*)fMainContext, _RunBlueZDispatchJob,
		job);
	return B_OK;
}


void
BlueZBackend::_FlushPendingDispatchJobs()
{
	std::vector<void*> jobs;
	{
		BAutolock lock(fLock);
		jobs.swap(fPendingDispatchJobs);
	}
	for (size_t i = 0; i < jobs.size(); i++) {
		g_main_context_invoke((GMainContext*)fMainContext,
			_RunBlueZDispatchJob, (_BlueZDispatchJob*)jobs[i]);
	}
}


void
BlueZBackend::_FailPendingDispatchJobs()
{
	std::vector<void*> jobs;
	{
		BAutolock lock(fLock);
		jobs.swap(fPendingDispatchJobs);
	}
	for (size_t i = 0; i < jobs.size(); i++) {
		_BlueZDispatchJob* job = (_BlueZDispatchJob*)jobs[i];
		BMessage reply(job->replyWhat);
		reply.AddInt32("status", (int32)B_ERROR);
		reply.AddString("reason", "the Bluetooth service is not available");
		job->replyTo.SendMessage(&reply);
		delete job;
	}
}


// #pragma mark - GDBus marshalling helpers (run directly on caller's thread)


// Maps a BlueZ property name to the BMessage field name this backend
// exposes it under, or NULL if it's not one we track. Single source of
// truth for both _AddVariantProperty() (below) and invalidated-properties
// handling (_RemovePropertyFieldLocked), so the two can never drift apart.
static const char*
_FieldNameForProperty(const char* propName)
{
	if (strcmp(propName, "Address") == 0)
		return "address";
	if (strcmp(propName, "Name") == 0)
		return "name";
	if (strcmp(propName, "Alias") == 0)
		return "alias";
	if (strcmp(propName, "Adapter") == 0)
		return "adapter";
	if (strcmp(propName, "Powered") == 0)
		return "powered";
	if (strcmp(propName, "Discovering") == 0)
		return "discovering";
	if (strcmp(propName, "Discoverable") == 0)
		return "discoverable";
	if (strcmp(propName, "Pairable") == 0)
		return "pairable";
	if (strcmp(propName, "Class") == 0)
		return "class";
	if (strcmp(propName, "Icon") == 0)
		return "icon";
	if (strcmp(propName, "Connected") == 0)
		return "connected";
	if (strcmp(propName, "Paired") == 0)
		return "paired";
	if (strcmp(propName, "Trusted") == 0)
		return "trusted";
	if (strcmp(propName, "Blocked") == 0)
		return "blocked";
	return NULL;
}


// Replace semantics (RemoveName() before adding): safe and equivalent for a
// full-object parse (each property appears at most once there anyway), and
// required for merging a PropertiesChanged update into a cached entry --
// BMessage::AddXXX() on an existing name appends instead of overwriting,
// which would otherwise turn a changed property into a two-element array.
static void
_AddVariantProperty(BMessage* info, const char* propName, GVariant* propValue)
{
	const char* field = _FieldNameForProperty(propName);
	if (field == NULL)
		return;
	info->RemoveName(field);

	if (strcmp(propName, "Address") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_STRING)) {
		info->AddString(field, g_variant_get_string(propValue, NULL));
	} else if (strcmp(propName, "Name") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_STRING)) {
		info->AddString(field, g_variant_get_string(propValue, NULL));
	} else if (strcmp(propName, "Alias") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_STRING)) {
		info->AddString(field, g_variant_get_string(propValue, NULL));
	} else if (strcmp(propName, "Adapter") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_OBJECT_PATH)) {
		info->AddString(field, g_variant_get_string(propValue, NULL));
	} else if (strcmp(propName, "Powered") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_BOOLEAN)) {
		info->AddBool(field, g_variant_get_boolean(propValue));
	} else if (strcmp(propName, "Discovering") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_BOOLEAN)) {
		info->AddBool(field, g_variant_get_boolean(propValue));
	} else if (strcmp(propName, "Discoverable") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_BOOLEAN)) {
		info->AddBool(field, g_variant_get_boolean(propValue));
	} else if (strcmp(propName, "Pairable") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_BOOLEAN)) {
		info->AddBool(field, g_variant_get_boolean(propValue));
	} else if (strcmp(propName, "Class") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_UINT32)) {
		info->AddUInt32(field, g_variant_get_uint32(propValue));
	} else if (strcmp(propName, "Icon") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_STRING)) {
		info->AddString(field, g_variant_get_string(propValue, NULL));
	} else if (strcmp(propName, "Connected") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_BOOLEAN)) {
		info->AddBool(field, g_variant_get_boolean(propValue));
	} else if (strcmp(propName, "Paired") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_BOOLEAN)) {
		info->AddBool(field, g_variant_get_boolean(propValue));
	} else if (strcmp(propName, "Trusted") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_BOOLEAN)) {
		info->AddBool(field, g_variant_get_boolean(propValue));
	} else if (strcmp(propName, "Blocked") == 0
			&& g_variant_is_of_type(propValue, G_VARIANT_TYPE_BOOLEAN)) {
		info->AddBool(field, g_variant_get_boolean(propValue));
	}
}


//! Bound every call: org.bluez is D-Bus auto-activated, so the GIO default
//! (-1, i.e. 25s) becomes a 25s UI stall whenever bluetoothd is slow or absent.
//! Shortened from the original 5000ms now that a failed/slow query is never
//! fatal (GetAdapters()/GetDevices() always serve the cache) -- a short bound
//! just means daemon-unresponsive is discovered, and the backoff/health state
//! updated, sooner. 2s is comfortably above a live daemon's normal round trip
//! (sub-100ms even under load) while cutting the worst case by more than half.
static const gint kBlueZCallTimeoutMs = 2000;

// How many times a GetManagedObjects reply may be discarded and re-issued
// because a signal mutated the snapshot while it was in flight, before just
// accepting the result -- see _HandleSnapshotQueryReply. A real churn storm
// that outlasts this is vanishingly unlikely and self-heals on the next
// signal regardless.
static const int kMaxGenerationRetries = 3;

static const bigtime_t kBaseBackoff = 2 * 1000000LL;
static const bigtime_t kMaxBackoff = 30 * 1000000LL;


// Pure GVariant parsing, no shared state -- safe to run unlocked and off the
// dispatch thread, which is exactly what _HandleSnapshotQueryReply does.
static void
_ParseManagedObjectsReply(GVariant* reply, std::map<BString, BMessage>& adapters,
	std::map<BString, BMessage>& devices)
{
	GVariantIter* objects = NULL;
	g_variant_get(reply, "(a{oa{sa{sv}}})", &objects);
	if (objects == NULL)
		return;

	const char* objectPath;
	GVariantIter* interfaces;

	while (g_variant_iter_loop(objects, "{&oa{sa{sv}}}", &objectPath,
			&interfaces)) {
		const char* interfaceName;
		GVariantIter* properties;

		while (g_variant_iter_loop(interfaces, "{&sa{sv}}", &interfaceName,
				&properties)) {
			bool isAdapter = strcmp(interfaceName, "org.bluez.Adapter1") == 0;
			bool isDevice = strcmp(interfaceName, "org.bluez.Device1") == 0;
			if (!isAdapter && !isDevice)
				continue;

			BMessage itemInfo;
			itemInfo.AddString("path", objectPath);

			const char* propName;
			GVariant* propValue;
			while (g_variant_iter_loop(properties, "{&sv}", &propName,
					&propValue)) {
				_AddVariantProperty(&itemInfo, propName, propValue);
			}

			(isAdapter ? adapters : devices)[BString(objectPath)] = itemInfo;
		}
	}

	g_variant_iter_free(objects);
}


// File-local: only ever seen by _StartSnapshotQuery/_IssueSnapshotQuery/
// _OnGetManagedObjectsReply/_HandleSnapshotQueryReply, carried as an opaque
// void* everywhere else so the header doesn't need to name it.
struct _SnapshotQueryCookie {
	BlueZBackend* backend;
	uint32 generation;
	int retriesLeft;
};


status_t
BlueZBackend::_MessageFromObjectsLocked(
	const std::map<BString, BMessage>& objects, const char* countField,
	const char* prefix, BMessage* outMessage)
{
	outMessage->MakeEmpty();

	int32 count = 0;
	for (std::map<BString, BMessage>::const_iterator it = objects.begin();
			it != objects.end(); ++it) {
		char itemName[48];
		snprintf(itemName, sizeof(itemName), "%s%" B_PRId32, prefix, count);
		outMessage->AddMessage(itemName, &it->second);
		count++;
	}
	outMessage->AddInt32(countField, count);

	return B_OK;
}


void
BlueZBackend::_ClearSnapshotLocked()
{
	fAdapterSnapshot.clear();
	fDeviceSnapshot.clear();
	fSnapshotPopulated = true;
	fSnapshotGeneration++;
}


// Never blocks: checks/sets state under fLock only long enough to decide
// whether to proceed, then hops onto the dispatch thread to actually issue
// the call. Safe to call from any thread, including from inside a signal
// callback already running on the dispatch thread (_OnBluezNameAppeared) --
// g_main_context_invoke() only queues a source, it never re-enters a loop.
void
BlueZBackend::_StartSnapshotQuery()
{
	uint32 generation;
	{
		BAutolock lock(fLock);
		if (fQueryInFlight || fBlueZConnection == NULL
				|| fMainContext == NULL) {
			return;
		}
		if (system_time() < fBackoffUntil)
			return;
		fQueryInFlight = true;
		generation = fSnapshotGeneration;
	}

	_SnapshotQueryCookie* cookie = new _SnapshotQueryCookie;
	cookie->backend = this;
	cookie->generation = generation;
	cookie->retriesLeft = kMaxGenerationRetries;

	g_main_context_invoke((GMainContext*)fMainContext,
		_IssueSnapshotQuerySource, cookie);
}


gboolean
BlueZBackend::_IssueSnapshotQuerySource(gpointer data)
{
	_SnapshotQueryCookie* cookie = (_SnapshotQueryCookie*)data;
	cookie->backend->_IssueSnapshotQuery(cookie);
	return G_SOURCE_REMOVE;
}


void
BlueZBackend::_IssueSnapshotQuery(void* cookie)
{
	if (fBlueZConnection == NULL) {
		BAutolock lock(fLock);
		fQueryInFlight = false;
		delete (_SnapshotQueryCookie*)cookie;
		return;
	}

	g_dbus_connection_call((GDBusConnection*)fBlueZConnection, "org.bluez",
		"/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", NULL,
		G_VARIANT_TYPE("(a{oa{sa{sv}}})"), G_DBUS_CALL_FLAGS_NONE,
		kBlueZCallTimeoutMs, NULL, _OnGetManagedObjectsReply, cookie);
}


void
BlueZBackend::_OnGetManagedObjectsReply(GObject* source, GAsyncResult* res,
	gpointer userData)
{
	_SnapshotQueryCookie* cookie = (_SnapshotQueryCookie*)userData;

	GError* error = NULL;
	GVariant* reply = g_dbus_connection_call_finish((GDBusConnection*)source,
		res, &error);

	cookie->backend->_HandleSnapshotQueryReply(reply, error, cookie);
}


void
BlueZBackend::_HandleSnapshotQueryReply(GVariant* reply, GError* error,
	void* rawCookie)
{
	_SnapshotQueryCookie* cookie = (_SnapshotQueryCookie*)rawCookie;

	if (reply == NULL) {
		fprintf(stderr, "BlueZBackend: GetManagedObjects failed: %s\n",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);

		BAutolock lock(fLock);
		fQueryInFlight = false;
		fDaemonHealthy = false;
		fCurrentBackoffUs = fCurrentBackoffUs == 0 ? kBaseBackoff
			: std::min(fCurrentBackoffUs * 2, kMaxBackoff);
		fBackoffUntil = system_time() + fCurrentBackoffUs;
		delete cookie;
		return;
	}

	// Parsing touches no shared state -- done unlocked, off whatever thread
	// GDBus delivered the reply on (the dispatch thread), before taking
	// fLock only for the swap below. This is the fix for the original
	// defect: the 2s-capped round trip above never holds fLock, so nothing
	// else (StartWatching(), status polls, the Agent1 trust check) can be
	// stuck behind it.
	std::map<BString, BMessage> adapters, devices;
	_ParseManagedObjectsReply(reply, adapters, devices);
	g_variant_unref(reply);

	BAutolock lock(fLock);
	fQueryInFlight = false;

	if (cookie->generation != fSnapshotGeneration && cookie->retriesLeft > 0) {
		// A signal (PropertiesChanged/InterfacesAdded/InterfacesRemoved)
		// mutated the cache while this query was in flight. That signal is
		// strictly newer than the point-in-time view just parsed above --
		// swapping it in now would silently revert a newer write with older
		// data (the exact lost-update race this generation counter exists
		// to catch). Discard and re-issue instead of swapping.
		cookie->retriesLeft--;
		cookie->generation = fSnapshotGeneration;
		fQueryInFlight = true;
		lock.Unlock();
		g_main_context_invoke((GMainContext*)fMainContext,
			_IssueSnapshotQuerySource, cookie);
		return;
	}

	// Generation matched (nothing raced this query), or the retry budget
	// above is spent and the tree is churning too fast to ever see a quiet
	// window -- accept this snapshot either way. In the retry-exhausted
	// case any single object this reply missed or resurrected relative to
	// the racing signal is corrected by the very next signal that touches
	// it, same as any other snapshot entry.
	fAdapterSnapshot.swap(adapters);
	fDeviceSnapshot.swap(devices);
	fSnapshotPopulated = true;
	fDaemonHealthy = true;
	fCurrentBackoffUs = 0;
	fBackoffUntil = 0;
	delete cookie;
}


// Caller must hold fLock. Returns the cached entry for `path`, creating an
// empty one (with "path" set) if this is the first anything has heard about
// it -- should only happen if a signal is somehow reordered ahead of the
// InterfacesAdded that introduced the path.
static BMessage&
_SnapshotEntryLocked(std::map<BString, BMessage>& snapshot, const char* path)
{
	BMessage& entry = snapshot[BString(path)];
	if (!entry.HasString("path"))
		entry.AddString("path", path);
	return entry;
}


void
BlueZBackend::_RemovePropertyFieldLocked(BMessage& entry,
	const char* propName)
{
	const char* field = _FieldNameForProperty(propName);
	if (field != NULL)
		entry.RemoveName(field);
}


status_t
BlueZBackend::_DoCallMethod(const char* path, const char* iface,
	const char* method, const char* paramObjectPath)
{
	if (fBlueZConnection == NULL)
		return B_ERROR;

	GVariant* parameters = NULL;
	if (paramObjectPath != NULL && paramObjectPath[0] != '\0')
		parameters = g_variant_new("(o)", paramObjectPath);

	GError* error = NULL;
	GVariant* reply = g_dbus_connection_call_sync(
		(GDBusConnection*)fBlueZConnection, "org.bluez", path, iface, method,
		parameters, NULL, G_DBUS_CALL_FLAGS_NONE, kBlueZCallTimeoutMs, NULL, &error);

	if (reply == NULL) {
		fprintf(stderr, "BlueZBackend: %s.%s on %s failed: %s\n", iface,
			method, path, error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
		return B_ERROR;
	}

	g_variant_unref(reply);
	return B_OK;
}


status_t
BlueZBackend::_DoSetProperty(const char* path, const char* iface,
	const char* property, bool value)
{
	if (fBlueZConnection == NULL)
		return B_ERROR;

	GVariant* propValue = g_variant_new_boolean(value);
	GVariant* parameters = g_variant_new("(ssv)", iface, property, propValue);

	GError* error = NULL;
	GVariant* reply = g_dbus_connection_call_sync(
		(GDBusConnection*)fBlueZConnection, "org.bluez", path,
		"org.freedesktop.DBus.Properties", "Set", parameters, NULL,
		G_DBUS_CALL_FLAGS_NONE, kBlueZCallTimeoutMs, NULL, &error);

	if (reply == NULL) {
		fprintf(stderr, "BlueZBackend: Set %s.%s on %s failed: %s\n", iface,
			property, path, error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
		return B_ERROR;
	}

	g_variant_unref(reply);
	return B_OK;
}


// Fans a single notification out to every registered watcher whose mask
// includes `type`, pruning messengers that have gone invalid (target team
// exited) as it goes. Caller must hold fLock.
void
BlueZBackend::_NotifyWatchers(uint32 type, BMessage& message)
{
	for (size_t i = 0; i < fWatchers.size();) {
		Watcher& watcher = fWatchers[i];
		if (!watcher.messenger.IsValid()) {
			fWatchers.erase(fWatchers.begin() + i);
			continue;
		}
		if ((watcher.mask & type) != 0)
			watcher.messenger.SendMessage(&message);
		i++;
	}
}


void
BlueZBackend::_PropertiesChangedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName, GVariant* parameters,
	void* userData)
{
	BlueZBackend* backend = (BlueZBackend*)userData;

	// parameters: (s a{sv} as) -> (interface, changed properties, invalidated)
	const char* changedInterface = NULL;
	GVariantIter* changedProps = NULL;
	GVariantIter* invalidatedProps = NULL;
	g_variant_get(parameters, "(&sa{sv}as)", &changedInterface, &changedProps,
		&invalidatedProps);

	bool isAdapter = strcmp(changedInterface, "org.bluez.Adapter1") == 0;
	bool isDevice = strcmp(changedInterface, "org.bluez.Device1") == 0;

	if (isAdapter || isDevice) {
		uint32 type = isAdapter
			? (uint32)NOTIFICATION_ADAPTER_PROPERTY_CHANGED
			: (uint32)NOTIFICATION_DEVICE_PROPERTY_CHANGED;

		BMessage message(type);
		message.AddString("path", objectPath);

		BAutolock lock(backend->fLock);

		std::map<BString, BMessage>& snapshot = isAdapter
			? backend->fAdapterSnapshot : backend->fDeviceSnapshot;
		BMessage& entry = _SnapshotEntryLocked(snapshot, objectPath);

		// One pass over changedProps drives both the outgoing delta
		// (message, unchanged contract for existing watchers) and the
		// cached entry (merged in-place -- a partial update must never
		// drop fields it didn't touch, which is why this isn't a
		// MakeEmpty()+refill).
		if (changedProps != NULL) {
			const char* propName;
			GVariant* propValue;
			while (g_variant_iter_loop(changedProps, "{&sv}", &propName,
					&propValue)) {
				_AddVariantProperty(&message, propName, propValue);
				_AddVariantProperty(&entry, propName, propValue);
			}
		}

		// Invalidated properties: BlueZ knows the interface still has these
		// but chose not to inline a value -- treat as "now unknown" and
		// drop from the cache rather than leaving a stale value in place.
		if (invalidatedProps != NULL) {
			const char* propName;
			while (g_variant_iter_loop(invalidatedProps, "&s", &propName)) {
				_RemovePropertyFieldLocked(entry, propName);
				message.AddString("invalidated", propName);
			}
		}

		backend->fSnapshotGeneration++;

		if (!backend->fWatchers.empty())
			backend->_NotifyWatchers(type, message);
	}

	if (changedProps != NULL)
		g_variant_iter_free(changedProps);
	if (invalidatedProps != NULL)
		g_variant_iter_free(invalidatedProps);
}


void
BlueZBackend::_InterfacesAddedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName, GVariant* parameters,
	void* userData)
{
	BlueZBackend* backend = (BlueZBackend*)userData;

	// parameters: (o a{sa{sv}}) -> (object path, {interface: {property: value}})
	const char* newObjectPath = NULL;
	GVariantIter* interfaces = NULL;
	g_variant_get(parameters, "(&oa{sa{sv}})", &newObjectPath, &interfaces);

	const char* ifaceName;
	GVariantIter* properties;
	while (g_variant_iter_loop(interfaces, "{&sa{sv}}", &ifaceName,
			&properties)) {
		bool isAdapter = strcmp(ifaceName, "org.bluez.Adapter1") == 0;
		bool isDevice = strcmp(ifaceName, "org.bluez.Device1") == 0;
		if (!isAdapter && !isDevice)
			continue;

		uint32 type = isAdapter
			? (uint32)NOTIFICATION_ADAPTER_ADDED
			: (uint32)NOTIFICATION_DEVICE_FOUND;

		BMessage message(type);
		message.AddString("path", newObjectPath);

		const char* propName;
		GVariant* propValue;
		while (g_variant_iter_loop(properties, "{&sv}", &propName,
				&propValue)) {
			_AddVariantProperty(&message, propName, propValue);
		}

		BAutolock lock(backend->fLock);

		// New object: the snapshot entry is whatever InterfacesAdded just
		// delivered, in full -- unlike PropertiesChanged this isn't a
		// partial update, so a plain overwrite is correct (and also
		// resolves the case where a placeholder entry was created early by
		// a reordered PropertiesChanged -- see _SnapshotEntryLocked).
		std::map<BString, BMessage>& snapshot = isAdapter
			? backend->fAdapterSnapshot : backend->fDeviceSnapshot;
		snapshot[BString(newObjectPath)] = message;
		backend->fSnapshotGeneration++;

		if (!backend->fWatchers.empty())
			backend->_NotifyWatchers(type, message);
	}

	if (interfaces != NULL)
		g_variant_iter_free(interfaces);
}


void
BlueZBackend::_InterfacesRemovedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName, GVariant* parameters,
	void* userData)
{
	BlueZBackend* backend = (BlueZBackend*)userData;

	BAutolock lock(backend->fLock);

	// parameters: (o as) -> (object path, removed interfaces)
	const char* removedObjectPath = NULL;
	GVariantIter* interfaces = NULL;
	g_variant_get(parameters, "(&oas)", &removedObjectPath, &interfaces);

	const char* ifaceName;
	while (g_variant_iter_loop(interfaces, "&s", &ifaceName)) {
		// Removing an interface removes only that interface's presence on
		// the object, not necessarily the whole object (BlueZ objects can
		// carry other interfaces, e.g. org.bluez.Battery1) -- but this
		// backend only ever tracked Adapter1/Device1 per path as a whole
		// entry, so losing the interface we cared about means dropping the
		// entry from that snapshot specifically, leaving the other
		// snapshot (if the same path is somehow tracked there too, which
		// doesn't happen in practice -- adapters and devices are disjoint
		// object paths) untouched.
		if (strcmp(ifaceName, "org.bluez.Device1") == 0) {
			// A forgotten/expired device also can't be "already trusted" any
			// more -- removing the cached entry handles that too, there's no
			// separate trusted-path list left to clear.
			backend->fDeviceSnapshot.erase(BString(removedObjectPath));
			backend->fSnapshotGeneration++;

			if (!backend->fWatchers.empty()) {
				BMessage message(NOTIFICATION_DEVICE_REMOVED);
				message.AddString("path", removedObjectPath);
				backend->_NotifyWatchers(NOTIFICATION_DEVICE_REMOVED, message);
			}
			continue;
		}

		if (strcmp(ifaceName, "org.bluez.Adapter1") != 0)
			continue;

		backend->fAdapterSnapshot.erase(BString(removedObjectPath));
		backend->fSnapshotGeneration++;

		if (backend->fWatchers.empty())
			continue;

		BMessage message(NOTIFICATION_ADAPTER_REMOVED);
		message.AddString("path", removedObjectPath);
		backend->_NotifyWatchers(NOTIFICATION_ADAPTER_REMOVED, message);
	}

	if (interfaces != NULL)
		g_variant_iter_free(interfaces);
}


// #pragma mark - Public API


// Serves from the snapshot cache -- a locked memory read, never IPC, never
// blocking -- once it has been populated at least once. Before the first
// successful populate, or whenever the daemon last looked unresponsive, this
// also kicks an async background refresh (rate-limited by backoff, see
// _StartSnapshotQuery); it does not wait for it. An unpopulated snapshot
// returns an honest B_ERROR rather than an empty-looking success. Shared by
// GetAdapters()/GetDevices() below.
status_t
BlueZBackend::_GetObjects(const char* filterInterface,
	const char* countField, const char* prefix, BMessage* outMessage)
{
	if (outMessage == NULL)
		return B_BAD_VALUE;

	status_t status;
	bool needsQuery;
	{
		BAutolock lock(fLock);

		if (fSnapshotPopulated) {
			const std::map<BString, BMessage>& snapshot =
				strcmp(filterInterface, "org.bluez.Adapter1") == 0
					? fAdapterSnapshot : fDeviceSnapshot;
			status = _MessageFromObjectsLocked(snapshot, countField, prefix,
				outMessage);
		} else {
			status = B_ERROR;
		}

		needsQuery = !fSnapshotPopulated || !fDaemonHealthy;
	}

	if (needsQuery)
		_StartSnapshotQuery();

	return status;
}


status_t
BlueZBackend::GetAdapters(BMessage* outAdapters)
{
	return _GetObjects("org.bluez.Adapter1", "adapter_count", "adapter_",
		outAdapters);
}


status_t
BlueZBackend::GetAdapterInfo(const char* adapterPath, BMessage* outInfo)
{
	if (adapterPath == NULL || outInfo == NULL)
		return B_BAD_VALUE;

	// Reuse the object-manager scan and pick out the matching entry; a
	// single-object Properties.GetAll would be marginally cheaper but this
	// keeps the parsing logic in exactly one place.
	BMessage adapters;
	status_t status = GetAdapters(&adapters);
	if (status != B_OK)
		return status;

	int32 count = 0;
	adapters.FindInt32("adapter_count", &count);
	for (int32 i = 0; i < count; i++) {
		BString name;
		name << "adapter_" << i;
		BMessage info;
		if (adapters.FindMessage(name.String(), &info) != B_OK)
			continue;
		const char* path;
		if (info.FindString("path", &path) == B_OK
				&& strcmp(path, adapterPath) == 0) {
			*outInfo = info;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


static void
_RunGetAdaptersAsync(void* cookie, BMessage* reply)
{
	BlueZBackend* backend = (BlueZBackend*)cookie;
	backend->GetAdapters(reply);
}


status_t
BlueZBackend::GetAdaptersAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	return _RunOnDispatchThread(_RunGetAdaptersAsync, this, replyTo,
		replyWhat);
}


status_t
BlueZBackend::_SetAdapterProperty(const char* adapterPath,
	const char* property, bool value)
{
	if (adapterPath == NULL || property == NULL)
		return B_BAD_VALUE;

	if (fBlueZConnection == NULL)
		return B_ERROR;

	uint32 generation;
	{
		BAutolock lock(fLock);
		generation = fSnapshotGeneration;
	}

	// Issued unlocked: g_dbus_connection_call_sync() here blocks up to
	// kBlueZCallTimeoutMs on a dead/slow daemon, and fLock also guards
	// StartWatching(), status polls and the Agent1 trust check -- none of
	// those may wait behind a Powered/Discoverable/Pairable toggle.
	status_t status = _DoSetProperty(adapterPath, "org.bluez.Adapter1",
		property, value);
	if (status != B_OK)
		return status;

	const char* field = _FieldNameForProperty(property);
	if (field == NULL)
		return status;

	BAutolock lock(fLock);
	if (fSnapshotGeneration != generation) {
		// A signal landed while the call was in flight and is strictly newer
		// than our guess about what our own write did -- leave it alone
		// rather than clobber authoritative daemon state with a stale value.
		return status;
	}

	std::map<BString, BMessage>::iterator it =
		fAdapterSnapshot.find(BString(adapterPath));
	if (it != fAdapterSnapshot.end()) {
		it->second.RemoveName(field);
		it->second.AddBool(field, value);
		fSnapshotGeneration++;
	}
	return status;
}


bool
BlueZBackend::_IsDeviceCachedTrusted(const char* devicePath)
{
	if (devicePath == NULL)
		return false;

	std::map<BString, BMessage>::const_iterator it =
		fDeviceSnapshot.find(BString(devicePath));
	if (it == fDeviceSnapshot.end())
		return false;

	bool trusted = false;
	it->second.FindBool("trusted", &trusted);
	return trusted;
}


status_t
BlueZBackend::SetAdapterPowered(const char* adapterPath, bool powered)
{
	return _SetAdapterProperty(adapterPath, "Powered", powered);
}


struct _SetAdapterPoweredCookie {
	BlueZBackend* backend;
	BString adapterPath;
	bool powered;
};


static void
_RunSetAdapterPoweredAsync(void* cookie, BMessage* reply)
{
	_SetAdapterPoweredCookie* job = (_SetAdapterPoweredCookie*)cookie;
	status_t status = job->backend->SetAdapterPowered(
		job->adapterPath.String(), job->powered);
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::SetAdapterPoweredAsync(const char* adapterPath, bool powered,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (adapterPath == NULL)
		return B_BAD_VALUE;

	_SetAdapterPoweredCookie* cookie = new _SetAdapterPoweredCookie;
	cookie->backend = this;
	cookie->adapterPath = adapterPath;
	cookie->powered = powered;

	return _RunOnDispatchThread(_RunSetAdapterPoweredAsync, cookie, replyTo,
		replyWhat);
}


status_t
BlueZBackend::SetAdapterDiscoverable(const char* adapterPath, bool discoverable)
{
	return _SetAdapterProperty(adapterPath, "Discoverable", discoverable);
}


struct _SetAdapterDiscoverableCookie {
	BlueZBackend* backend;
	BString adapterPath;
	bool discoverable;
};


static void
_RunSetAdapterDiscoverableAsync(void* cookie, BMessage* reply)
{
	_SetAdapterDiscoverableCookie* job = (_SetAdapterDiscoverableCookie*)cookie;
	status_t status = job->backend->SetAdapterDiscoverable(
		job->adapterPath.String(), job->discoverable);
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::SetAdapterDiscoverableAsync(const char* adapterPath,
	bool discoverable, const BMessenger& replyTo, uint32 replyWhat)
{
	if (adapterPath == NULL)
		return B_BAD_VALUE;

	_SetAdapterDiscoverableCookie* cookie = new _SetAdapterDiscoverableCookie;
	cookie->backend = this;
	cookie->adapterPath = adapterPath;
	cookie->discoverable = discoverable;

	return _RunOnDispatchThread(_RunSetAdapterDiscoverableAsync, cookie,
		replyTo, replyWhat);
}


status_t
BlueZBackend::SetAdapterPairable(const char* adapterPath, bool pairable)
{
	return _SetAdapterProperty(adapterPath, "Pairable", pairable);
}


struct _SetAdapterPairableCookie {
	BlueZBackend* backend;
	BString adapterPath;
	bool pairable;
};


static void
_RunSetAdapterPairableAsync(void* cookie, BMessage* reply)
{
	_SetAdapterPairableCookie* job = (_SetAdapterPairableCookie*)cookie;
	status_t status = job->backend->SetAdapterPairable(
		job->adapterPath.String(), job->pairable);
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::SetAdapterPairableAsync(const char* adapterPath, bool pairable,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (adapterPath == NULL)
		return B_BAD_VALUE;

	_SetAdapterPairableCookie* cookie = new _SetAdapterPairableCookie;
	cookie->backend = this;
	cookie->adapterPath = adapterPath;
	cookie->pairable = pairable;

	return _RunOnDispatchThread(_RunSetAdapterPairableAsync, cookie,
		replyTo, replyWhat);
}


status_t
BlueZBackend::_CallAdapterMethod(const char* adapterPath, const char* method)
{
	if (adapterPath == NULL || method == NULL)
		return B_BAD_VALUE;

	if (fBlueZConnection == NULL)
		return B_ERROR;

	uint32 generation;
	{
		BAutolock lock(fLock);
		generation = fSnapshotGeneration;
	}

	status_t status = _DoCallMethod(adapterPath, "org.bluez.Adapter1", method);
	if (status != B_OK)
		return status;

	// StartDiscovery/StopDiscovery are the only adapter methods with a
	// predictable property outcome; anything else (there are none today)
	// just leaves the cache alone for the next signal to update.
	const char* field = NULL;
	bool value = false;
	if (strcmp(method, "StartDiscovery") == 0) {
		field = "discovering";
		value = true;
	} else if (strcmp(method, "StopDiscovery") == 0) {
		field = "discovering";
		value = false;
	}
	if (field == NULL)
		return status;

	BAutolock lock(fLock);
	if (fSnapshotGeneration != generation)
		return status;	// a signal raced in and is authoritative -- keep it

	std::map<BString, BMessage>::iterator it =
		fAdapterSnapshot.find(BString(adapterPath));
	if (it != fAdapterSnapshot.end()) {
		it->second.RemoveName(field);
		it->second.AddBool(field, value);
		fSnapshotGeneration++;
	}
	return status;
}


status_t
BlueZBackend::StartDiscovery(const char* adapterPath)
{
	return _CallAdapterMethod(adapterPath, "StartDiscovery");
}


status_t
BlueZBackend::StopDiscovery(const char* adapterPath)
{
	return _CallAdapterMethod(adapterPath, "StopDiscovery");
}


struct _AdapterPathCookie {
	BlueZBackend* backend;
	BString adapterPath;
};


static void
_RunStartDiscoveryAsync(void* cookie, BMessage* reply)
{
	_AdapterPathCookie* job = (_AdapterPathCookie*)cookie;
	status_t status = job->backend->StartDiscovery(job->adapterPath.String());
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::StartDiscoveryAsync(const char* adapterPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (adapterPath == NULL)
		return B_BAD_VALUE;

	_AdapterPathCookie* cookie = new _AdapterPathCookie;
	cookie->backend = this;
	cookie->adapterPath = adapterPath;

	return _RunOnDispatchThread(_RunStartDiscoveryAsync, cookie, replyTo,
		replyWhat);
}


static void
_RunStopDiscoveryAsync(void* cookie, BMessage* reply)
{
	_AdapterPathCookie* job = (_AdapterPathCookie*)cookie;
	status_t status = job->backend->StopDiscovery(job->adapterPath.String());
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::StopDiscoveryAsync(const char* adapterPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (adapterPath == NULL)
		return B_BAD_VALUE;

	_AdapterPathCookie* cookie = new _AdapterPathCookie;
	cookie->backend = this;
	cookie->adapterPath = adapterPath;

	return _RunOnDispatchThread(_RunStopDiscoveryAsync, cookie, replyTo,
		replyWhat);
}


status_t
BlueZBackend::GetDevices(BMessage* outDevices)
{
	return _GetObjects("org.bluez.Device1", "device_count", "device_",
		outDevices);
}


status_t
BlueZBackend::GetDeviceInfo(const char* devicePath, BMessage* outInfo)
{
	if (devicePath == NULL || outInfo == NULL)
		return B_BAD_VALUE;

	BMessage devices;
	status_t status = GetDevices(&devices);
	if (status != B_OK)
		return status;

	int32 count = 0;
	devices.FindInt32("device_count", &count);
	for (int32 i = 0; i < count; i++) {
		BString name;
		name << "device_" << i;
		BMessage info;
		if (devices.FindMessage(name.String(), &info) != B_OK)
			continue;
		const char* path;
		if (info.FindString("path", &path) == B_OK
				&& strcmp(path, devicePath) == 0) {
			*outInfo = info;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


static void
_RunGetDevicesAsync(void* cookie, BMessage* reply)
{
	BlueZBackend* backend = (BlueZBackend*)cookie;
	backend->GetDevices(reply);
}


status_t
BlueZBackend::GetDevicesAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	return _RunOnDispatchThread(_RunGetDevicesAsync, this, replyTo,
		replyWhat);
}


status_t
BlueZBackend::_CallDeviceMethod(const char* devicePath, const char* method)
{
	if (devicePath == NULL || method == NULL)
		return B_BAD_VALUE;

	if (fBlueZConnection == NULL)
		return B_ERROR;

	uint32 generation;
	{
		BAutolock lock(fLock);
		generation = fSnapshotGeneration;
	}

	status_t status = _DoCallMethod(devicePath, "org.bluez.Device1", method);
	if (status != B_OK)
		return status;

	// Connect/Disconnect/Pair returning OK over D-Bus already means the
	// operation completed (these aren't fire-and-forget), so the resulting
	// property value is known, not a guess -- unlike a bare method success.
	const char* field = NULL;
	bool value = false;
	if (strcmp(method, "Connect") == 0) {
		field = "connected";
		value = true;
	} else if (strcmp(method, "Disconnect") == 0) {
		field = "connected";
		value = false;
	} else if (strcmp(method, "Pair") == 0) {
		field = "paired";
		value = true;
	}
	if (field == NULL)
		return status;

	BAutolock lock(fLock);
	if (fSnapshotGeneration != generation)
		return status;	// a signal raced in and is authoritative -- keep it

	std::map<BString, BMessage>::iterator it =
		fDeviceSnapshot.find(BString(devicePath));
	if (it != fDeviceSnapshot.end()) {
		it->second.RemoveName(field);
		it->second.AddBool(field, value);
		fSnapshotGeneration++;
	}
	return status;
}


status_t
BlueZBackend::ConnectDevice(const char* devicePath)
{
	return _CallDeviceMethod(devicePath, "Connect");
}


status_t
BlueZBackend::DisconnectDevice(const char* devicePath)
{
	return _CallDeviceMethod(devicePath, "Disconnect");
}


status_t
BlueZBackend::PairDevice(const char* devicePath, const char* pin)
{
	// BlueZ pairing agents handle the PIN/passkey exchange out-of-band via
	// org.bluez.Agent1; a plain Pair() call is the correct entry point here.
	(void)pin;
	return _CallDeviceMethod(devicePath, "Pair");
}


status_t
BlueZBackend::UnpairDevice(const char* devicePath)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	BMessage info;
	status_t status = GetDeviceInfo(devicePath, &info);
	if (status != B_OK)
		return status;

	BString adapterPath;
	if (info.FindString("adapter", &adapterPath) != B_OK)
		return B_ERROR;

	if (fBlueZConnection == NULL)
		return B_ERROR;

	// Issued unlocked -- see _SetAdapterProperty for why.
	status_t callStatus = _DoCallMethod(adapterPath.String(),
		"org.bluez.Adapter1", "RemoveDevice", devicePath);
	if (callStatus == B_OK) {
		// Optimistic, but unlike a property flip this needs no generation
		// check: RemoveDevice returning OK means the object is genuinely
		// gone from BlueZ, so erasing it is correct regardless of anything
		// that raced in meanwhile (InterfacesRemoved will confirm and is
		// harmless to erase again; a stray PropertiesChanged for the same
		// path racing in is moot -- the object no longer exists).
		BAutolock lock(fLock);
		fDeviceSnapshot.erase(BString(devicePath));
		fSnapshotGeneration++;
	}
	return callStatus;
}


struct _DevicePathCookie {
	BlueZBackend* backend;
	BString devicePath;
};


static void
_RunConnectDeviceAsync(void* cookie, BMessage* reply)
{
	_DevicePathCookie* job = (_DevicePathCookie*)cookie;
	status_t status = job->backend->ConnectDevice(job->devicePath.String());
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::ConnectDeviceAsync(const char* devicePath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	_DevicePathCookie* cookie = new _DevicePathCookie;
	cookie->backend = this;
	cookie->devicePath = devicePath;

	return _RunOnDispatchThread(_RunConnectDeviceAsync, cookie, replyTo,
		replyWhat);
}


static void
_RunDisconnectDeviceAsync(void* cookie, BMessage* reply)
{
	_DevicePathCookie* job = (_DevicePathCookie*)cookie;
	status_t status = job->backend->DisconnectDevice(job->devicePath.String());
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::DisconnectDeviceAsync(const char* devicePath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	_DevicePathCookie* cookie = new _DevicePathCookie;
	cookie->backend = this;
	cookie->devicePath = devicePath;

	return _RunOnDispatchThread(_RunDisconnectDeviceAsync, cookie, replyTo,
		replyWhat);
}


static void
_RunPairDeviceAsync(void* cookie, BMessage* reply)
{
	_DevicePathCookie* job = (_DevicePathCookie*)cookie;
	status_t status = job->backend->PairDevice(job->devicePath.String());
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::PairDeviceAsync(const char* devicePath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	_DevicePathCookie* cookie = new _DevicePathCookie;
	cookie->backend = this;
	cookie->devicePath = devicePath;

	return _RunOnDispatchThread(_RunPairDeviceAsync, cookie, replyTo,
		replyWhat);
}


static void
_RunUnpairDeviceAsync(void* cookie, BMessage* reply)
{
	_DevicePathCookie* job = (_DevicePathCookie*)cookie;
	status_t status = job->backend->UnpairDevice(job->devicePath.String());
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::UnpairDeviceAsync(const char* devicePath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	_DevicePathCookie* cookie = new _DevicePathCookie;
	cookie->backend = this;
	cookie->devicePath = devicePath;

	return _RunOnDispatchThread(_RunUnpairDeviceAsync, cookie, replyTo,
		replyWhat);
}


status_t
BlueZBackend::_SetDeviceProperty(const char* devicePath, const char* property,
	bool value)
{
	if (devicePath == NULL || property == NULL)
		return B_BAD_VALUE;

	if (fBlueZConnection == NULL)
		return B_ERROR;

	uint32 generation;
	{
		BAutolock lock(fLock);
		generation = fSnapshotGeneration;
	}

	// Issued unlocked -- see _SetAdapterProperty for why.
	status_t status = _DoSetProperty(devicePath, "org.bluez.Device1", property,
		value);
	if (status != B_OK)
		return status;

	const char* field = _FieldNameForProperty(property);
	if (field == NULL)
		return status;

	BAutolock lock(fLock);
	if (fSnapshotGeneration != generation) {
		// Same lost-update guard as _SetAdapterProperty: a signal raced in
		// while the call was in flight and is authoritative daemon state,
		// not our guess -- don't clobber it.
		return status;
	}

	std::map<BString, BMessage>::iterator it =
		fDeviceSnapshot.find(BString(devicePath));
	if (it != fDeviceSnapshot.end()) {
		it->second.RemoveName(field);
		it->second.AddBool(field, value);
		fSnapshotGeneration++;
	}
	return status;
}


status_t
BlueZBackend::SetDeviceTrusted(const char* devicePath, bool trusted)
{
	return _SetDeviceProperty(devicePath, "Trusted", trusted);
}


status_t
BlueZBackend::SetDeviceBlocked(const char* devicePath, bool blocked)
{
	return _SetDeviceProperty(devicePath, "Blocked", blocked);
}


struct _SetDevicePropertyCookie {
	BlueZBackend* backend;
	BString devicePath;
	bool value;
};


static void
_RunSetDeviceTrustedAsync(void* cookie, BMessage* reply)
{
	_SetDevicePropertyCookie* job = (_SetDevicePropertyCookie*)cookie;
	status_t status = job->backend->SetDeviceTrusted(job->devicePath.String(),
		job->value);
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::SetDeviceTrustedAsync(const char* devicePath, bool trusted,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	_SetDevicePropertyCookie* cookie = new _SetDevicePropertyCookie;
	cookie->backend = this;
	cookie->devicePath = devicePath;
	cookie->value = trusted;

	return _RunOnDispatchThread(_RunSetDeviceTrustedAsync, cookie, replyTo,
		replyWhat);
}


static void
_RunSetDeviceBlockedAsync(void* cookie, BMessage* reply)
{
	_SetDevicePropertyCookie* job = (_SetDevicePropertyCookie*)cookie;
	status_t status = job->backend->SetDeviceBlocked(job->devicePath.String(),
		job->value);
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::SetDeviceBlockedAsync(const char* devicePath, bool blocked,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	_SetDevicePropertyCookie* cookie = new _SetDevicePropertyCookie;
	cookie->backend = this;
	cookie->devicePath = devicePath;
	cookie->value = blocked;

	return _RunOnDispatchThread(_RunSetDeviceBlockedAsync, cookie, replyTo,
		replyWhat);
}


struct _GetStatusCookie {
	BlueZBackend* backend;
};


static void
_RunGetStatusAsync(void* cookie, BMessage* reply)
{
	_GetStatusCookie* job = (_GetStatusCookie*)cookie;

	reply->AddBool("bluez_available", job->backend->IsServiceAvailable());

	BMessage adapters;
	job->backend->GetAdapters(&adapters);
	reply->AddMessage("adapters", &adapters);

	BMessage devices;
	job->backend->GetDevices(&devices);
	reply->AddMessage("devices", &devices);

	delete job;
}


status_t
BlueZBackend::GetStatusAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	_GetStatusCookie* cookie = new _GetStatusCookie;
	cookie->backend = this;

	return _RunOnDispatchThread(_RunGetStatusAsync, cookie, replyTo,
		replyWhat);
}


// Caller must hold fLock. Subscribes the three org.bluez signals
// unconditionally once the connection exists (not gated on fWatchers --
// the snapshot cache needs them live regardless of whether any UI is
// watching) and each isn't already subscribed. Called from _InitBlueZ()
// once the connection comes up, and again from StartWatching() as a
// harmless no-op safety net for callers that could theoretically race it.
void
BlueZBackend::_SubscribeSignalsLocked()
{
	if (fBlueZConnection == NULL)
		return;

	if (fPropertiesChangedSubscriptionId == 0) {
		fPropertiesChangedSubscriptionId = g_dbus_connection_signal_subscribe(
			(GDBusConnection*)fBlueZConnection, "org.bluez",
			"org.freedesktop.DBus.Properties", "PropertiesChanged", NULL,
			NULL, G_DBUS_SIGNAL_FLAGS_NONE, _PropertiesChangedCallback, this,
			NULL);
	}

	if (fInterfacesAddedSubscriptionId == 0) {
		fInterfacesAddedSubscriptionId = g_dbus_connection_signal_subscribe(
			(GDBusConnection*)fBlueZConnection, "org.bluez",
			"org.freedesktop.DBus.ObjectManager", "InterfacesAdded", "/",
			NULL, G_DBUS_SIGNAL_FLAGS_NONE, _InterfacesAddedCallback, this,
			NULL);
	}

	if (fInterfacesRemovedSubscriptionId == 0) {
		fInterfacesRemovedSubscriptionId = g_dbus_connection_signal_subscribe(
			(GDBusConnection*)fBlueZConnection, "org.bluez",
			"org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", "/",
			NULL, G_DBUS_SIGNAL_FLAGS_NONE, _InterfacesRemovedCallback, this,
			NULL);
	}
}


status_t
BlueZBackend::StartWatching(const BMessenger& target, uint32 notificationMask)
{
	BAutolock lock(fLock);

	// Recorded regardless of whether fBlueZConnection exists yet -- a
	// caller racing bluez_init (typically AttachedToWindow()) used to get
	// an immediate, permanent B_ERROR here and never subscribe at all.
	// _SubscribeSignalsLocked() below is a no-op until the connection is
	// up; _InitBlueZ()'s replay call picks this watcher up once it is.
	bool found = false;
	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers[i].mask = notificationMask;
			found = true;
			break;
		}
	}
	if (!found) {
		Watcher watcher;
		watcher.messenger = target;
		watcher.mask = notificationMask;
		fWatchers.push_back(watcher);
	}

	_SubscribeSignalsLocked();

	return B_OK;
}


status_t
BlueZBackend::StopWatching(const BMessenger& target)
{
	BAutolock lock(fLock);

	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers.erase(fWatchers.begin() + i);
			break;
		}
	}

	// The three org.bluez signal subscriptions stay up regardless of watcher
	// count now -- they feed the snapshot cache, which GetAdapters()/
	// GetDevices() depend on even with zero watchers registered. Only
	// _CleanupBlueZ() (process teardown) unsubscribes them.

	return B_OK;
}


// #pragma mark - org.bluez.Agent1


// KeyboardDisplay is the most capable registered capability -- it lets
// BlueZ pick the strongest mutually supported SSP flow instead of falling
// back to legacy PIN entry.
static const char* kAgentPath = "/org/vitruvian/bluetooth/agent";
static const char* kAgentCapability = "KeyboardDisplay";

static const char* kAgentIntrospectionXML =
	"<node>"
	"  <interface name='org.bluez.Agent1'>"
	"    <method name='Release'/>"
	"    <method name='RequestPinCode'>"
	"      <arg type='o' name='device' direction='in'/>"
	"      <arg type='s' name='pincode' direction='out'/>"
	"    </method>"
	"    <method name='DisplayPinCode'>"
	"      <arg type='o' name='device' direction='in'/>"
	"      <arg type='s' name='pincode' direction='in'/>"
	"    </method>"
	"    <method name='RequestPasskey'>"
	"      <arg type='o' name='device' direction='in'/>"
	"      <arg type='u' name='passkey' direction='out'/>"
	"    </method>"
	"    <method name='DisplayPasskey'>"
	"      <arg type='o' name='device' direction='in'/>"
	"      <arg type='u' name='passkey' direction='in'/>"
	"      <arg type='q' name='entered' direction='in'/>"
	"    </method>"
	"    <method name='RequestConfirmation'>"
	"      <arg type='o' name='device' direction='in'/>"
	"      <arg type='u' name='passkey' direction='in'/>"
	"    </method>"
	"    <method name='RequestAuthorization'>"
	"      <arg type='o' name='device' direction='in'/>"
	"    </method>"
	"    <method name='AuthorizeService'>"
	"      <arg type='o' name='device' direction='in'/>"
	"      <arg type='s' name='uuid' direction='in'/>"
	"    </method>"
	"    <method name='Cancel'/>"
	"  </interface>"
	"</node>";


// Mirrors src/apps/bluetoothstatus/PairingDialogWindow.h's pairing_dialog_kind
// exactly (0..6, same order) -- kept as plain int32 here rather than an
// #include because kit code must not depend on an app header; only the
// numeric convention is shared, carried across in the "kind" BMessage field.
enum {
	kKindRequestConfirmation = 0,
	kKindRequestPasskey,
	kKindDisplayPasskey,
	kKindRequestPinCode,
	kKindDisplayPinCode,
	kKindRequestAuthorization,
	kKindAuthorizeService
};


void
BlueZBackend::_AgentMethodCall(GDBusConnection* connection,
	const char* sender, const char* objectPath, const char* interfaceName,
	const char* methodName, GVariant* parameters,
	GDBusMethodInvocation* invocation, void* userData)
{
	((BlueZBackend*)userData)->_HandleAgentMethodCall(methodName, parameters,
		invocation);
}


void
BlueZBackend::_HandleAgentMethodCall(const char* methodName,
	GVariant* parameters, GDBusMethodInvocation* invocation)
{
	if (strcmp(methodName, "Release") == 0) {
		_CancelPendingAgentRequests();
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}

	if (strcmp(methodName, "Cancel") == 0) {
		// Agent1.Cancel carries no device -- it means "whatever request is
		// outstanding is void now". Must be idempotent: BlueZ may call it
		// after we already replied, or with nothing pending at all.
		_CancelPendingAgentRequests();
		BMessenger uiHandler = fAgentRouter.UIHandler();
		if (uiHandler.IsValid()) {
			BMessage cancel((uint32)AGENT_CANCEL);
			uiHandler.SendMessage(&cancel);
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}

	const char* devicePath = NULL;
	int32 kind;
	BMessage request;
	bool blocking = true;

	if (strcmp(methodName, "RequestConfirmation") == 0) {
		guint32 passkey = 0;
		g_variant_get(parameters, "(&ou)", &devicePath, &passkey);
		kind = kKindRequestConfirmation;
		request.AddUInt32("passkey", (uint32)passkey);
	} else if (strcmp(methodName, "RequestPasskey") == 0) {
		g_variant_get(parameters, "(&o)", &devicePath);
		kind = kKindRequestPasskey;
	} else if (strcmp(methodName, "DisplayPasskey") == 0) {
		guint32 passkey = 0;
		guint16 entered = 0;
		g_variant_get(parameters, "(&ouq)", &devicePath, &passkey, &entered);
		kind = kKindDisplayPasskey;
		request.AddUInt32("passkey", (uint32)passkey);
		blocking = false;
	} else if (strcmp(methodName, "RequestPinCode") == 0) {
		g_variant_get(parameters, "(&o)", &devicePath);
		kind = kKindRequestPinCode;
	} else if (strcmp(methodName, "DisplayPinCode") == 0) {
		const char* pin = NULL;
		g_variant_get(parameters, "(&o&s)", &devicePath, &pin);
		kind = kKindDisplayPinCode;
		request.AddString("pin_code", pin);
		blocking = false;
	} else if (strcmp(methodName, "RequestAuthorization") == 0) {
		g_variant_get(parameters, "(&o)", &devicePath);
		kind = kKindRequestAuthorization;
	} else if (strcmp(methodName, "AuthorizeService") == 0) {
		const char* uuid = NULL;
		g_variant_get(parameters, "(&o&s)", &devicePath, &uuid);
		kind = kKindAuthorizeService;
		request.AddString("service_name", uuid);
	} else {
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
			G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown Agent1 method %s",
			methodName);
		return;
	}

	// Auto-answer AuthorizeService for a device the user has already
	// explicitly trusted (Device1.Trusted, set from the preflet's Trust
	// button or by BlueZ itself after a successful pairing). AuthorizeService
	// only grants use of a Bluetooth profile/service on a device that is
	// already bonded -- it does not create or modify the pairing itself, so
	// this cannot be used to sneak through a new bond. RequestConfirmation,
	// RequestPasskey/PinCode and RequestAuthorization all establish or
	// re-establish the pairing (or, for RequestAuthorization, decide whether
	// an already-paired device's incoming reconnection is legitimate) and
	// must keep prompting regardless of trust.
	if (kind == kKindAuthorizeService) {
		BAutolock lock(fLock);
		bool trusted = _IsDeviceCachedTrusted(devicePath);
		lock.Unlock();
		if (trusted) {
			g_dbus_method_invocation_return_value(invocation, NULL);
			return;
		}
	}

	// Do NOT resolve a friendly name here: that would mean a synchronous
	// GetDeviceInfo() -> GetDevices() -> g_dbus_connection_call_sync() call
	// from inside this very method-call handler, i.e. a nested D-Bus round
	// trip on the thread that is dispatching bluetoothd's incoming call --
	// bluetoothd waits on our reply while we call back into it, stalling the
	// dispatch thread for up to kBlueZCallTimeoutMs with no notifications
	// flowing. The UI already has (or can cheaply get) a device list from
	// its own status snapshot; it resolves alias/name from that, falling
	// back to the path/address when nothing is cached.
	request.AddInt32("kind", kind);
	request.AddString("device_path", devicePath);

	if (!blocking) {
		// DisplayPasskey/DisplayPinCode are notify-only: BlueZ does not wait
		// for the user, only for this call to return. Still route through
		// the router as best-effort UI feedback, but reply right away --
		// holding the invocation open here would just stall bluetoothd for
		// nothing on every incoming digit.
		BMessenger uiHandler = fAgentRouter.UIHandler();
		if (uiHandler.IsValid()) {
			request.what = AGENT_REQUEST;
			request.AddUInt32("request_id", 0);
			uiHandler.SendMessage(&request);
		}
		g_dbus_method_invocation_return_value(invocation, NULL);
		return;
	}

	request.what = AGENT_REQUEST;
	uint32 requestId = 0;
	status_t status = fAgentRouter.BeginRequest(request, invocation,
		&requestId);
	if (status != B_OK) {
		// No UI registered (or it just died) -- fail loud and fast rather
		// than leave bluetoothd's own agent timeout (~60s) as the only
		// thing standing between this and looking like dead hardware.
		g_dbus_method_invocation_return_dbus_error(invocation,
			"org.bluez.Error.Canceled", "No pairing UI is registered");
	}
}


void
BlueZBackend::_CompleteAgentRequest(uint32 requestId, bool accepted,
	const BString& value)
{
	void* cookie = fAgentRouter.Take(requestId);
	if (cookie == NULL)
		return;	// already answered, cancelled, or stale -- idempotent

	GDBusMethodInvocation* invocation = (GDBusMethodInvocation*)cookie;

	if (!accepted) {
		g_dbus_method_invocation_return_dbus_error(invocation,
			"org.bluez.Error.Rejected", "Rejected by user");
		return;
	}

	const char* method = g_dbus_method_invocation_get_method_name(invocation);
	if (strcmp(method, "RequestPasskey") == 0) {
		guint32 passkey = (guint32)strtoul(value.String(), NULL, 10);
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(u)", passkey));
	} else if (strcmp(method, "RequestPinCode") == 0) {
		g_dbus_method_invocation_return_value(invocation,
			g_variant_new("(s)", value.String()));
	} else {
		// RequestConfirmation / RequestAuthorization / AuthorizeService all
		// reply with an empty tuple on acceptance.
		g_dbus_method_invocation_return_value(invocation, NULL);
	}
}


static gboolean
_RunCompleteAgentRequest(gpointer data);


struct _CompleteAgentCookie {
	BlueZBackend* backend;
	uint32 requestId;
	bool accepted;
	BString value;
};


static gboolean
_RunCompleteAgentRequest(gpointer data)
{
	_CompleteAgentCookie* cookie = (_CompleteAgentCookie*)data;
	cookie->backend->_CompleteAgentRequest(cookie->requestId,
		cookie->accepted, cookie->value);
	delete cookie;
	return G_SOURCE_REMOVE;
}


void
BlueZBackend::CompleteAgentRequest(uint32 requestId, bool accepted,
	const BString& value)
{
	if (fMainContext == NULL || requestId == 0)
		return;

	_CompleteAgentCookie* cookie = new _CompleteAgentCookie;
	cookie->backend = this;
	cookie->requestId = requestId;
	cookie->accepted = accepted;
	cookie->value = value;

	// GDBusMethodInvocation completion is documented thread-safe from any
	// thread, but routing through the dispatch thread keeps a single
	// synchronization idiom for all Agent1/router state instead of a
	// second one just for this call.
	g_main_context_invoke((GMainContext*)fMainContext,
		_RunCompleteAgentRequest, cookie);
}


void
BlueZBackend::_CancelPendingAgentRequests()
{
	std::vector<void*> cookies;
	fAgentRouter.TakeAll(cookies);
	for (size_t i = 0; i < cookies.size(); i++) {
		g_dbus_method_invocation_return_dbus_error(
			(GDBusMethodInvocation*)cookies[i], "org.bluez.Error.Canceled",
			"Canceled");
	}
}


status_t
BlueZBackend::_RegisterAgent(const BMessenger& uiHandler)
{
	if (fBlueZConnection == NULL)
		return B_ERROR;

	{
		BAutolock lock(fLock);
		fAgentRouter.SetUIHandler(uiHandler);

		if (fAgentRegistrationId != 0) {
			// Already exported (e.g. the replicant detached and reattached
			// without a full teardown in between) -- the UI handler above is
			// all that needed refreshing.
			return B_OK;
		}
	}

	// Everything below issues up to two synchronous D-Bus round trips
	// (kBlueZCallTimeoutMs each) -- unlocked, like every other write path
	// here, so a dead/slow daemon during agent registration doesn't stall
	// status polls, StartWatching() or the Agent1 trust check behind it.
	GError* error = NULL;
	GDBusNodeInfo* nodeInfo = g_dbus_node_info_new_for_xml(
		kAgentIntrospectionXML, &error);
	if (nodeInfo == NULL) {
		fprintf(stderr, "BlueZBackend: bad Agent1 introspection XML: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
		return B_ERROR;
	}

	static const GDBusInterfaceVTable vtable = {
		_AgentMethodCall, NULL, NULL
	};

	guint id = g_dbus_connection_register_object(
		(GDBusConnection*)fBlueZConnection, kAgentPath,
		nodeInfo->interfaces[0], &vtable, this, NULL, &error);
	g_dbus_node_info_unref(nodeInfo);

	if (id == 0) {
		fprintf(stderr, "BlueZBackend: failed to export Agent1: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
		return B_ERROR;
	}

	{
		BAutolock lock(fLock);
		// A second RegisterAgentAsync() could have raced this one through the
		// unlocked window above and already won -- unregister our redundant
		// export rather than leaking it or clobbering fAgentRegistrationId.
		if (fAgentRegistrationId != 0) {
			g_dbus_connection_unregister_object(
				(GDBusConnection*)fBlueZConnection, id);
			return B_OK;
		}
		fAgentRegistrationId = id;
	}

	GVariant* result = g_dbus_connection_call_sync(
		(GDBusConnection*)fBlueZConnection, "org.bluez", "/org/bluez",
		"org.bluez.AgentManager1", "RegisterAgent",
		g_variant_new("(os)", kAgentPath, kAgentCapability), NULL,
		G_DBUS_CALL_FLAGS_NONE, kBlueZCallTimeoutMs, NULL, &error);
	if (result == NULL) {
		fprintf(stderr, "BlueZBackend: AgentManager1.RegisterAgent failed: "
			"%s\n", error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
		BAutolock lock(fLock);
		g_dbus_connection_unregister_object(
			(GDBusConnection*)fBlueZConnection, fAgentRegistrationId);
		fAgentRegistrationId = 0;
		return B_ERROR;
	}
	g_variant_unref(result);

	// Non-fatal if this fails (e.g. another agent already claimed default) --
	// our agent is still registered and will be used for devices that
	// target it explicitly.
	result = g_dbus_connection_call_sync((GDBusConnection*)fBlueZConnection,
		"org.bluez", "/org/bluez", "org.bluez.AgentManager1",
		"RequestDefaultAgent", g_variant_new("(o)", kAgentPath), NULL,
		G_DBUS_CALL_FLAGS_NONE, kBlueZCallTimeoutMs, NULL, &error);
	if (result == NULL) {
		fprintf(stderr, "BlueZBackend: RequestDefaultAgent failed: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
	} else {
		g_variant_unref(result);
	}

	return B_OK;
}


status_t
BlueZBackend::_UnregisterAgent()
{
	// AuthPromptRouter is internally synchronized (see its own header) --
	// safe to call unlocked, and must run regardless of whether an agent
	// is currently exported.
	_CancelPendingAgentRequests();

	guint registrationId;
	{
		BAutolock lock(fLock);
		fAgentRouter.SetUIHandler(BMessenger());
		registrationId = fAgentRegistrationId;
		if (registrationId == 0)
			return B_OK;
		fAgentRegistrationId = 0;
	}

	// Issued unlocked, like _RegisterAgent -- see its comment for why.
	if (fBlueZConnection != NULL) {
		GError* error = NULL;
		GVariant* result = g_dbus_connection_call_sync(
			(GDBusConnection*)fBlueZConnection, "org.bluez", "/org/bluez",
			"org.bluez.AgentManager1", "UnregisterAgent",
			g_variant_new("(o)", kAgentPath), NULL, G_DBUS_CALL_FLAGS_NONE,
			kBlueZCallTimeoutMs, NULL, &error);
		if (result != NULL)
			g_variant_unref(result);
		else if (error != NULL)
			g_error_free(error);

		g_dbus_connection_unregister_object(
			(GDBusConnection*)fBlueZConnection, registrationId);
	}

	return B_OK;
}


struct _AgentRegisterCookie {
	BlueZBackend* backend;
	BMessenger uiHandler;
};


static void
_RunRegisterAgentAsync(void* cookie, BMessage* reply)
{
	_AgentRegisterCookie* job = (_AgentRegisterCookie*)cookie;
	status_t status = job->backend->_RegisterAgent(job->uiHandler);
	reply->AddInt32("status", status);
	delete job;
}


status_t
BlueZBackend::RegisterAgentAsync(const BMessenger& uiHandler,
	const BMessenger& replyTo, uint32 replyWhat)
{
	_AgentRegisterCookie* cookie = new _AgentRegisterCookie;
	cookie->backend = this;
	cookie->uiHandler = uiHandler;

	return _RunOnDispatchThread(_RunRegisterAgentAsync, cookie, replyTo,
		replyWhat);
}


static void
_RunUnregisterAgentAsync(void* cookie, BMessage* reply)
{
	BlueZBackend* backend = (BlueZBackend*)cookie;
	status_t status = backend->_UnregisterAgent();
	reply->AddInt32("status", status);
}


status_t
BlueZBackend::UnregisterAgentAsync(const BMessenger& replyTo,
	uint32 replyWhat)
{
	return _RunOnDispatchThread(_RunUnregisterAgentAsync, this, replyTo,
		replyWhat);
}
