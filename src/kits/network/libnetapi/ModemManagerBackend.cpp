/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ModemManagerBackend.h"

#include <Messenger.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vector>

#include <gio/gio.h>

#include <Autolock.h>


// #pragma mark - Constants


static const char* kMMBusName = "org.freedesktop.ModemManager1";
static const char* kMMObjectPath = "/org/freedesktop/ModemManager1";
static const char* kMMManagerInterface = "org.freedesktop.ModemManager1";
static const char* kMMModemInterface = "org.freedesktop.ModemManager1.Modem";
static const char* kMMModemSignalInterface
	= "org.freedesktop.ModemManager1.Modem.Signal";
static const char* kMMModem3gppInterface
	= "org.freedesktop.ModemManager1.Modem.Modem3gpp";
static const char* kMMSimInterface = "org.freedesktop.ModemManager1.Sim";
static const char* kMMObjectManagerInterface
	= "org.freedesktop.DBus.ObjectManager";

static const gint kMMCallTimeoutMs = 5000;

static const char* kMMPropertiesInterface = "org.freedesktop.DBus.Properties";


// Properties.Get for one property on one interface of one object. The
// destination must be the ModemManager bus name, not the Properties interface.
static GVariant*
_MMGetProperty(GDBusConnection* connection, const char* objectPath,
	const char* interfaceName, const char* propertyName, GError** error)
{
	GVariant* result = g_dbus_connection_call_sync(connection,
		kMMBusName, objectPath,
		kMMPropertiesInterface, "Get",
		g_variant_new("(ss)", interfaceName, propertyName),
		G_VARIANT_TYPE("(v)"),
		G_DBUS_CALL_FLAGS_NONE,
		kMMCallTimeoutMs, NULL, error);
	if (result == NULL)
		return NULL;

	GVariant* inner = g_variant_get_child_value(result, 0);
	GVariant* unwrapped = g_variant_get_variant(inner);
	g_variant_unref(inner);
	g_variant_unref(result);
	return unwrapped;
}

// ModemManager state values from the specification (MMModemState)
static const uint32 kMMStateUnknown = 0;
static const uint32 kMMStateDisabled = 10;
static const uint32 kMMStateDisabling = 11;
static const uint32 kMMStateEnabling = 12;
static const uint32 kMMStateEnabled = 20;
static const uint32 kMMStateSearching = 21;
static const uint32 kMMStateRegistered = 30;
static const uint32 kMMStateDisconnecting = 40;
static const uint32 kMMStateConnecting = 41;
static const uint32 kMMStateConnected = 50;

// MMModemLock values used for SIM unlock state.
static const uint32 kMMLockUnknown = 0;
static const uint32 kMMLockNone = 1;
static const uint32 kMMLockSimPin = 2;
static const uint32 kMMLockSimPin2 = 3;
static const uint32 kMMLockSimPuk = 4;
static const uint32 kMMLockSimPuk2 = 5;


// Fills SIM lock fields from the Modem UnlockRequired/UnlockRetries pair.
static void
_FillSimLockFromModemProps(GVariant* modemProps, BMessage* outInfo)
{
	uint32 lock = kMMLockUnknown;
	bool pinRequired = false;
	bool pukRequired = false;
	uint32 retries = 0;

	GVariant* val = g_variant_lookup_value(modemProps, "UnlockRequired",
		G_VARIANT_TYPE_UINT32);
	if (val != NULL) {
		lock = g_variant_get_uint32(val);
		g_variant_unref(val);
	}

	pinRequired = lock == kMMLockSimPin || lock == kMMLockSimPin2;
	pukRequired = lock == kMMLockSimPuk || lock == kMMLockSimPuk2;

	val = g_variant_lookup_value(modemProps, "UnlockRetries",
		G_VARIANT_TYPE("a{uu}"));
	if (val != NULL) {
		guint32 retriesKey = pinRequired ? kMMLockSimPin : kMMLockSimPuk;
		if (lock == kMMLockSimPin2)
			retriesKey = kMMLockSimPin2;
		else if (lock == kMMLockSimPuk2)
			retriesKey = kMMLockSimPuk2;

		GVariantIter* iter = g_variant_iter_new(val);
		guint32 entryKey = 0;
		guint32 entryValue = 0;
		while (g_variant_iter_next(iter, "{uu}", &entryKey, &entryValue)) {
			if (entryKey == retriesKey) {
				retries = entryValue;
				break;
			}
		}
		g_variant_iter_free(iter);
		g_variant_unref(val);
	}

	outInfo->AddUInt32(kMMFieldSimUnlockRequired, lock);
	outInfo->AddBool(kMMFieldSimLocked, pinRequired || pukRequired);
	outInfo->AddBool(kMMFieldSimPinRequired, pinRequired);
	outInfo->AddBool(kMMFieldSimPukRequired, pukRequired);
	outInfo->AddUInt32(kMMFieldSimUnlockRetries, retries);
	outInfo->AddUInt32(kMMFieldSimStatus, lock);
}


// #pragma mark - Singleton


ModemManagerBackend*
ModemManagerBackend::Instance()
{
	// Deliberately leaked: a function-local static's destructor runs at
	// exit() on whatever thread called exit(), not the dispatch thread.
	static ModemManagerBackend* instance = new ModemManagerBackend();
	return instance;
}


ModemManagerBackend::ModemManagerBackend()
	: fMMWatcherId(0),
	fDBusConnection(NULL),
	fMainContext(NULL),
	fMainLoop(NULL),
	fDispatchThread(-1),
	fPropertiesChangedSubscriptionId(0),
	fInterfacesAddedSubscriptionId(0),
	fInterfacesRemovedSubscriptionId(0),
	fSnapshotPopulated(false)
{
	_InitModemManager();
}


ModemManagerBackend::~ModemManagerBackend()
{
	_CleanupModemManager();
}


// #pragma mark - Dispatch thread


int32
ModemManagerBackend::_DispatchThreadEntry(void* data)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)data;
	backend->_DispatchThread();
	return 0;
}


void
ModemManagerBackend::_DispatchThread()
{
	if (fMainLoop == NULL)
		return;

	g_main_context_push_thread_default((GMainContext*)fMainContext);
	g_main_loop_run((GMainLoop*)fMainLoop);
	g_main_context_pop_thread_default((GMainContext*)fMainContext);
}


static gboolean
_QuitLoop(gpointer data)
{
	g_main_loop_quit((GMainLoop*)data);
	return G_SOURCE_REMOVE;
}


// #pragma mark - _RunOnDispatchThread


struct _DispatchJob {
	ModemManagerBackend::DispatchFunc func;
	void* cookie;
	BMessenger replyTo;
	uint32 replyWhat;
};


static gboolean
_RunDispatchJob(gpointer data)
{
	_DispatchJob* job = (_DispatchJob*)data;

	BMessage reply(job->replyWhat);
	job->func(job->cookie, &reply);
	job->replyTo.SendMessage(&reply);

	delete job;
	return G_SOURCE_REMOVE;
}


status_t
ModemManagerBackend::_RunOnDispatchThread(DispatchFunc func, void* cookie,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (fMainContext == NULL)
		return B_ERROR;

	_DispatchJob* job = new _DispatchJob;
	job->func = func;
	job->cookie = cookie;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext, _RunDispatchJob, job);
	return B_OK;
}


// #pragma mark - Init / cleanup


bool
ModemManagerBackend::_InitModemManager()
{
	fMainContext = g_main_context_new();
	if (fMainContext == NULL) {
		fprintf(stderr, "ModemManagerBackend: failed to create GMainContext\n");
		return false;
	}

	fMainLoop = g_main_loop_new((GMainContext*)fMainContext, FALSE);
	if (fMainLoop == NULL) {
		fprintf(stderr, "ModemManagerBackend: failed to create GMainLoop\n");
		_CleanupModemManager();
		return false;
	}

	g_main_context_push_thread_default((GMainContext*)fMainContext);

	// Try connecting to the system bus. ModemManager not running is not
	// fatal; the name watcher below will retry when it appears.
	GError* error = NULL;
	fDBusConnection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
	if (fDBusConnection == NULL) {
		fprintf(stderr, "ModemManagerBackend: no system bus: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
	}

	g_main_context_pop_thread_default((GMainContext*)fMainContext);

	fDispatchThread = spawn_thread(_DispatchThreadEntry, "mm_dispatch",
		B_NORMAL_PRIORITY, this);
	if (fDispatchThread < B_OK) {
		fprintf(stderr, "ModemManagerBackend: failed to spawn dispatch thread\n");
		_CleanupModemManager();
		return false;
	}
	resume_thread(fDispatchThread);

	// Set up the name watcher on the dispatch thread.
	g_main_context_invoke((GMainContext*)fMainContext, _SetupMMWatchSource,
		this);

	// If the bus connection succeeded, subscribe to signals immediately.
	if (fDBusConnection != NULL) {
		g_main_context_invoke((GMainContext*)fMainContext,
			[](gpointer data) -> gboolean {
				ModemManagerBackend* backend
					= (ModemManagerBackend*)data;
				backend->_RefreshSnapshot();
				return G_SOURCE_REMOVE;
			}, this);
	}

	return true;
}


void
ModemManagerBackend::_CleanupModemManager()
{
	if (fMMWatcherId != 0) {
		g_bus_unwatch_name(fMMWatcherId);
		fMMWatcherId = 0;
	}

	if (fDispatchThread >= B_OK && fMainLoop != NULL) {
		g_main_context_invoke((GMainContext*)fMainContext, _QuitLoop,
			fMainLoop);
		status_t result;
		wait_for_thread(fDispatchThread, &result);
		fDispatchThread = -1;
	}

	if (fDBusConnection != NULL) {
		g_object_unref(fDBusConnection);
		fDBusConnection = NULL;
	}

	if (fMainLoop != NULL) {
		g_main_loop_unref((GMainLoop*)fMainLoop);
		fMainLoop = NULL;
	}

	if (fMainContext != NULL) {
		g_main_context_unref((GMainContext*)fMainContext);
		fMainContext = NULL;
	}
}


// #pragma mark - Name watcher


gboolean
ModemManagerBackend::_SetupMMWatchSource(gpointer cookie)
{
	((ModemManagerBackend*)cookie)->_SetupMMWatch();
	return G_SOURCE_REMOVE;
}


void
ModemManagerBackend::_SetupMMWatch()
{
	fMMWatcherId = g_bus_watch_name(G_BUS_TYPE_SYSTEM,
		kMMBusName, G_BUS_NAME_WATCHER_FLAGS_NONE,
		_OnMMNameAppeared, _OnMMNameVanished, this, NULL);
}


void
ModemManagerBackend::_OnMMNameAppeared(GDBusConnection* connection,
	const char* name, const char* nameOwner, void* userData)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)userData;
	fprintf(stderr, "ModemManagerBackend: ModemManager appeared\n");

	if (backend->fDBusConnection == NULL) {
		GError* error = NULL;
		backend->fDBusConnection = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
			NULL, &error);
		if (backend->fDBusConnection == NULL) {
			fprintf(stderr, "ModemManagerBackend: bus re-acquire failed: %s\n",
				error ? error->message : "unknown error");
			if (error)
				g_error_free(error);
			return;
		}
	}

	backend->_RefreshSnapshot();
}


void
ModemManagerBackend::_OnMMNameVanished(GDBusConnection* connection,
	const char* name, void* userData)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)userData;
	fprintf(stderr, "ModemManagerBackend: ModemManager vanished\n");

	{
		BAutolock lock(backend->fLock);
		backend->fModemSnapshot.clear();
		backend->fModemListSnapshot.MakeEmpty();
		backend->fModemListSnapshot.AddBool(kMMFieldMMAvailable, false);
		backend->fModemListSnapshot.AddInt32(kMMFieldModemCount, 0);
		backend->fSnapshotPopulated = true;
	}

	BMessage message((uint32)NOTIFICATION_SERVICE_VANISHED);
	BAutolock lock(backend->fLock);
	if (!backend->fWatchers.empty())
		backend->_NotifyWatchers(NOTIFICATION_SERVICE_VANISHED, message);
}


// #pragma mark - Snapshot refresh (dispatch thread only)


static const char*
_ModemStateString(uint32 state)
{
	switch (state) {
		case kMMStateUnknown:
			return "unknown";
		case kMMStateDisabled:
			return "disabled";
		case kMMStateDisabling:
			return "disabling";
		case kMMStateEnabling:
			return "enabling";
		case kMMStateEnabled:
			return "enabled";
		case kMMStateSearching:
			return "searching";
		case kMMStateRegistered:
			return "registered";
		case kMMStateDisconnecting:
			return "disconnecting";
		case kMMStateConnecting:
			return "connecting";
		case kMMStateConnected:
			return "connected";
		default:
			return "unknown";
	}
}


// Reads properties from the Modem, Modem3gpp and Sim interface dicts of
// one ObjectManager entry. SignalQuality is a Modem property; OperatorName
// lives on Modem3gpp.
static void
_FillModemInfoFromInterfaces(GVariant* interfaces, const char* objectPath,
	BMessage* outInfo)
{
	outInfo->MakeEmpty();
	outInfo->AddString(kMMFieldModemPath,
		objectPath != NULL ? objectPath : "");

	GVariant* val = NULL;
	GVariant* modemProps = g_variant_lookup_value(interfaces,
		kMMModemInterface, G_VARIANT_TYPE("a{sv}"));

	if (modemProps != NULL) {
		val = g_variant_lookup_value(modemProps, "State",
			G_VARIANT_TYPE_INT32);
		if (val != NULL) {
			uint32 state = (uint32)g_variant_get_int32(val);
			outInfo->AddUInt32(kMMFieldModemState, state);
			outInfo->AddString(kMMFieldModemState,
				_ModemStateString(state));
			outInfo->AddBool(kMMFieldModemEnabled,
				state >= kMMStateEnabled);
			g_variant_unref(val);
		} else {
			outInfo->AddUInt32(kMMFieldModemState, kMMStateUnknown);
			outInfo->AddString(kMMFieldModemState, "unknown");
			outInfo->AddBool(kMMFieldModemEnabled, false);
		}

		val = g_variant_lookup_value(modemProps, "EquipmentIdentifier",
			G_VARIANT_TYPE_STRING);
		if (val != NULL) {
			outInfo->AddString(kMMFieldModemEquipmentId,
				g_variant_get_string(val, NULL));
			g_variant_unref(val);
		}

		val = g_variant_lookup_value(modemProps, "Sim",
			G_VARIANT_TYPE_OBJECT_PATH);
		if (val != NULL) {
			outInfo->AddString(kMMFieldModemSimPath,
				g_variant_get_string(val, NULL));
			g_variant_unref(val);
		}

		val = g_variant_lookup_value(modemProps, "SignalQuality",
			G_VARIANT_TYPE("(ub)"));
		if (val != NULL) {
			guint32 quality = 0;
			gboolean recent = FALSE;
			g_variant_get(val, "(ub)", &quality, &recent);
			outInfo->AddUInt32(kMMFieldModemSignalQuality, quality);
			uint32 bars = 0;
			if (quality > 0) bars = 1;
			if (quality > 25) bars = 2;
			if (quality > 50) bars = 3;
			if (quality > 75) bars = 4;
			outInfo->AddUInt32(kMMFieldModemSignalBars, bars);
			g_variant_unref(val);
		}

		_FillSimLockFromModemProps(modemProps, outInfo);

		g_variant_unref(modemProps);
	} else {
		outInfo->AddUInt32(kMMFieldModemState, kMMStateUnknown);
		outInfo->AddString(kMMFieldModemState, "unknown");
		outInfo->AddBool(kMMFieldModemEnabled, false);
		outInfo->AddUInt32(kMMFieldSimUnlockRequired, kMMLockUnknown);
		outInfo->AddBool(kMMFieldSimLocked, false);
		outInfo->AddBool(kMMFieldSimPinRequired, false);
		outInfo->AddBool(kMMFieldSimPukRequired, false);
		outInfo->AddUInt32(kMMFieldSimUnlockRetries, 0);
		outInfo->AddUInt32(kMMFieldSimStatus, kMMLockUnknown);
	}

	GVariant* gppProps = g_variant_lookup_value(interfaces,
		kMMModem3gppInterface, G_VARIANT_TYPE("a{sv}"));
	if (gppProps != NULL) {
		val = g_variant_lookup_value(gppProps, "OperatorName",
			G_VARIANT_TYPE_STRING);
		if (val != NULL) {
			outInfo->AddString(kMMFieldModemOperatorName,
				g_variant_get_string(val, NULL));
			g_variant_unref(val);
		}
		g_variant_unref(gppProps);
	}
}


// Walks the ModemManager ObjectManager reply and populates the snapshot.
static void
_FillModemsFromManagedObjects(GVariant* managedObjects,
	std::map<BString, BMessage>& modemSnapshot, BMessage* outList)
{
	outList->MakeEmpty();

	if (managedObjects == NULL) {
		outList->AddBool(kMMFieldMMAvailable, true);
		outList->AddInt32(kMMFieldModemCount, 0);
		return;
	}

	outList->AddBool(kMMFieldMMAvailable, true);
	int32 count = 0;

	GVariantIter* iter = g_variant_iter_new(managedObjects);
	gchar* objectPath = NULL;
	GVariant* interfaces = NULL;

	while (g_variant_iter_next(iter, "{o@a{sa{sv}}}", &objectPath,
			&interfaces)) {
		GVariant* modemProps = g_variant_lookup_value(interfaces,
			kMMModemInterface, G_VARIANT_TYPE("a{sv}"));
		if (modemProps != NULL) {
			BMessage modemInfo;
			_FillModemInfoFromInterfaces(interfaces, objectPath,
				&modemInfo);

			modemSnapshot[objectPath] = modemInfo;

			char modName[32];
			snprintf(modName, sizeof(modName), "modem_%" B_PRId32, count);
			outList->AddMessage(modName, &modemInfo);
			count++;

			g_variant_unref(modemProps);
		}
		g_free(objectPath);
		g_variant_unref(interfaces);
	}

	g_variant_iter_free(iter);
	outList->AddInt32(kMMFieldModemCount, count);
}


// Called from the dispatch thread after a name-appeared or property change.
void
ModemManagerBackend::_RefreshSnapshot()
{
	if (fDBusConnection == NULL)
		return;

	GError* error = NULL;
	GVariant* result = g_dbus_connection_call_sync(
		(GDBusConnection*)fDBusConnection,
		kMMBusName, kMMObjectPath,
		kMMObjectManagerInterface, "GetManagedObjects",
		NULL,
		G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
		G_DBUS_CALL_FLAGS_NONE,
		kMMCallTimeoutMs, NULL, &error);

	if (result == NULL) {
		fprintf(stderr, "ModemManagerBackend: GetManagedObjects failed: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);

		BAutolock lock(fLock);
		fModemSnapshot.clear();
		fModemListSnapshot.MakeEmpty();
		fModemListSnapshot.AddBool(kMMFieldMMAvailable, true);
		fModemListSnapshot.AddInt32(kMMFieldModemCount, 0);
		fSnapshotPopulated = true;
		return;
	}

	GVariant* managedObjects = g_variant_get_child_value(result, 0);

	std::map<BString, BMessage> newSnapshot;
	BMessage newList;
	_FillModemsFromManagedObjects(managedObjects, newSnapshot, &newList);

	g_variant_unref(managedObjects);
	g_variant_unref(result);

	{
		BAutolock lock(fLock);
		fModemSnapshot = newSnapshot;
		fModemListSnapshot = newList;
		fSnapshotPopulated = true;
	}
}


// #pragma mark - D-Bus signal callbacks


void
ModemManagerBackend::_PropertiesChangedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName,
	GVariant* parameters, void* userData)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)userData;

	// Only handle interfaces that carry modem state/signal/SIM data.
	if (interfaceName != NULL
			&& strcmp(interfaceName, kMMModemInterface) != 0
			&& strcmp(interfaceName, kMMModemSignalInterface) != 0
			&& strcmp(interfaceName, kMMModem3gppInterface) != 0
			&& strcmp(interfaceName, kMMSimInterface) != 0) {
		return;
	}

	// Refresh the snapshot and notify.
	backend->_RefreshSnapshot();

	BMessage message((uint32)NOTIFICATION_MODEM_STATE_CHANGED);
	message.AddString(kMMFieldModemPath,
		objectPath != NULL ? objectPath : "");

	BAutolock lock(backend->fLock);
	if (!backend->fWatchers.empty()) {
		backend->_NotifyWatchers(NOTIFICATION_MODEM_STATE_CHANGED, message);
		backend->_NotifyWatchers(NOTIFICATION_MODEM_SIGNAL_CHANGED, message);
	}
}


void
ModemManagerBackend::_InterfacesAddedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName,
	GVariant* parameters, void* userData)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)userData;

	backend->_RefreshSnapshot();

	BMessage message((uint32)NOTIFICATION_MODEM_ADDED);
	// The object path of the newly added modem is in the first argument
	// of the InterfacesAdded signal: (oa{sa{sv}}).
	if (parameters != NULL) {
		const gchar* newPath = NULL;
		g_variant_get(parameters, "(&o*)", &newPath, NULL);
		if (newPath != NULL)
			message.AddString(kMMFieldModemPath, newPath);
	}

	BAutolock lock(backend->fLock);
	if (!backend->fWatchers.empty())
		backend->_NotifyWatchers(NOTIFICATION_MODEM_ADDED, message);
}


void
ModemManagerBackend::_InterfacesRemovedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName,
	GVariant* parameters, void* userData)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)userData;

	backend->_RefreshSnapshot();

	BMessage message((uint32)NOTIFICATION_MODEM_REMOVED);
	if (parameters != NULL) {
		const gchar* removedPath = NULL;
		g_variant_get(parameters, "(&o*)", &removedPath, NULL);
		if (removedPath != NULL)
			message.AddString(kMMFieldModemPath, removedPath);
	}

	BAutolock lock(backend->fLock);
	if (!backend->fWatchers.empty())
		backend->_NotifyWatchers(NOTIFICATION_MODEM_REMOVED, message);
}


// #pragma mark - Notification fan-out


void
ModemManagerBackend::_NotifyWatchers(uint32 type, BMessage& message)
{
	// Caller must hold fLock.
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


// #pragma mark - Public API: IsServiceAvailable


bool
ModemManagerBackend::IsServiceAvailable()
{
	BAutolock lock(fLock);
	return fDBusConnection != NULL;
}


// #pragma mark - Public API: GetModems (synchronous)


status_t
ModemManagerBackend::GetModems(BMessage* outModems)
{
	if (outModems == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fLock);

	if (!fSnapshotPopulated) {
		outModems->MakeEmpty();
		outModems->AddBool(kMMFieldMMAvailable, false);
		outModems->AddInt32(kMMFieldModemCount, 0);
		return B_OK;
	}

	*outModems = fModemListSnapshot;
	return B_OK;
}


// #pragma mark - Public API: GetModemsAsync


struct _GetModemsCookie {
	ModemManagerBackend* backend;
};


static void
_RunGetModemsAsync(void* cookie, BMessage* reply)
{
	_GetModemsCookie* job = (_GetModemsCookie*)cookie;
	job->backend->GetModems(reply);
	delete job;
}


status_t
ModemManagerBackend::GetModemsAsync(const BMessenger& replyTo,
	uint32 replyWhat)
{
	_GetModemsCookie* cookie = new _GetModemsCookie;
	cookie->backend = this;

	return _RunOnDispatchThread(_RunGetModemsAsync, cookie, replyTo,
		replyWhat);
}


// #pragma mark - Public API: GetModemInfo


status_t
ModemManagerBackend::GetModemInfo(const char* modemPath, BMessage* outInfo)
{
	if (modemPath == NULL || outInfo == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fLock);

	std::map<BString, BMessage>::iterator it = fModemSnapshot.find(modemPath);
	if (it != fModemSnapshot.end()) {
		*outInfo = it->second;
		return B_OK;
	}

	outInfo->MakeEmpty();
	return B_ENTRY_NOT_FOUND;
}


struct _GetModemInfoCookie {
	ModemManagerBackend* backend;
	BString modemPath;
};


static void
_RunGetModemInfoAsync(void* cookie, BMessage* reply)
{
	_GetModemInfoCookie* job = (_GetModemInfoCookie*)cookie;
	job->backend->GetModemInfo(job->modemPath.String(), reply);
	delete job;
}


status_t
ModemManagerBackend::GetModemInfoAsync(const char* modemPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL)
		return B_BAD_VALUE;

	_GetModemInfoCookie* cookie = new _GetModemInfoCookie;
	cookie->backend = this;
	cookie->modemPath = modemPath;

	return _RunOnDispatchThread(_RunGetModemInfoAsync, cookie, replyTo,
		replyWhat);
}


// #pragma mark - Public API: EnableModem / EnableModemAsync


struct _EnableModemJob {
	ModemManagerBackend* backend;
	GDBusConnection* connection;
	BString modemPath;
	bool enabled;
	BMessenger replyTo;
	uint32 replyWhat;
};


static gboolean
_RunEnableModem(gpointer data)
{
	_EnableModemJob* job = (_EnableModemJob*)data;

	GError* error = NULL;
	GVariant* result = NULL;

	// org.freedesktop.ModemManager1.Modem.Enable(bool)
	// or org.freedesktop.ModemManager1.Modem.Disable()
	if (job->enabled) {
		result = g_dbus_connection_call_sync(job->connection,
			kMMBusName, job->modemPath.String(),
			kMMModemInterface, "Enable",
			g_variant_new("(b)", TRUE),
			NULL, G_DBUS_CALL_FLAGS_NONE,
			kMMCallTimeoutMs, NULL, &error);
	} else {
		result = g_dbus_connection_call_sync(job->connection,
			kMMBusName, job->modemPath.String(),
			kMMModemInterface, "Disable",
			NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
			kMMCallTimeoutMs, NULL, &error);
	}

	BMessage reply(job->replyWhat);
	if (result != NULL) {
		reply.AddInt32("status", (int32)B_OK);
		g_variant_unref(result);
	} else {
		reply.AddInt32("status", (int32)B_ERROR);
		reply.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error)
			g_error_free(error);
	}

	job->replyTo.SendMessage(&reply);

	// Refresh snapshot after a state change.
	job->backend->_RefreshSnapshot();

	BMessage notification((uint32)ModemManagerBackend::NOTIFICATION_MODEM_STATE_CHANGED);
	notification.AddString(kMMFieldModemPath, job->modemPath);
	{
		BAutolock lock(job->backend->fLock);
		if (!job->backend->fWatchers.empty())
			job->backend->_NotifyWatchers(
				ModemManagerBackend::NOTIFICATION_MODEM_STATE_CHANGED,
				notification);
	}

	delete job;
	return G_SOURCE_REMOVE;
}


status_t
ModemManagerBackend::EnableModem(const char* modemPath, bool enabled)
{
	if (modemPath == NULL)
		return B_BAD_VALUE;

	if (fDBusConnection == NULL)
		return B_ERROR;

	GError* error = NULL;
	GVariant* result = NULL;

	if (enabled) {
		result = g_dbus_connection_call_sync(
			(GDBusConnection*)fDBusConnection,
			kMMBusName, modemPath,
			kMMModemInterface, "Enable",
			g_variant_new("(b)", TRUE),
			NULL, G_DBUS_CALL_FLAGS_NONE,
			kMMCallTimeoutMs, NULL, &error);
	} else {
		result = g_dbus_connection_call_sync(
			(GDBusConnection*)fDBusConnection,
			kMMBusName, modemPath,
			kMMModemInterface, "Disable",
			NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
			kMMCallTimeoutMs, NULL, &error);
	}

	status_t status = B_OK;
	if (result == NULL) {
		fprintf(stderr, "ModemManagerBackend: Enable/Disable failed: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
		status = B_ERROR;
	} else {
		g_variant_unref(result);
	}

	// Refresh the snapshot.
	if (fMainContext != NULL) {
		g_main_context_invoke((GMainContext*)fMainContext,
			[](gpointer data) -> gboolean {
				((ModemManagerBackend*)data)->_RefreshSnapshot();
				return G_SOURCE_REMOVE;
			}, this);
	}

	return status;
}


status_t
ModemManagerBackend::EnableModemAsync(const char* modemPath, bool enabled,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL)
		return B_BAD_VALUE;

	if (fDBusConnection == NULL || fMainContext == NULL)
		return B_ERROR;

	_EnableModemJob* job = new _EnableModemJob;
	job->backend = this;
	job->connection = (GDBusConnection*)fDBusConnection;
	job->modemPath = modemPath;
	job->enabled = enabled;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext, _RunEnableModem, job);
	return B_OK;
}


// #pragma mark - Public API: GetSignalQuality


status_t
ModemManagerBackend::GetSignalQuality(const char* modemPath,
	BMessage* outSignal)
{
	if (modemPath == NULL || outSignal == NULL)
		return B_BAD_VALUE;

	if (fDBusConnection == NULL)
		return B_ERROR;

	// SignalQuality is a property of org.freedesktop.ModemManager1.Modem.
	GError* error = NULL;
	GVariant* unwrapped = _MMGetProperty(
		(GDBusConnection*)fDBusConnection, modemPath,
		kMMModemInterface, "SignalQuality", &error);

	outSignal->MakeEmpty();
	outSignal->AddString(kMMFieldModemPath, modemPath);

	if (unwrapped != NULL) {
		guint32 quality = 0;
		gboolean recent = FALSE;

		if (g_variant_is_of_type(unwrapped, G_VARIANT_TYPE("(ub)"))) {
			g_variant_get(unwrapped, "(ub)", &quality, &recent);
		}
		outSignal->AddUInt32(kMMFieldModemSignalQuality, quality);

		uint32 bars = 0;
		if (quality > 0) bars = 1;
		if (quality > 25) bars = 2;
		if (quality > 50) bars = 3;
		if (quality > 75) bars = 4;
		outSignal->AddUInt32(kMMFieldModemSignalBars, bars);

		g_variant_unref(unwrapped);
		return B_OK;
	} else {
		if (error != NULL)
			g_error_free(error);
		return B_ERROR;
	}
}


struct _GetSignalCookie {
	ModemManagerBackend* backend;
	BString modemPath;
};


static void
_RunGetSignalQualityAsync(void* cookie, BMessage* reply)
{
	_GetSignalCookie* job = (_GetSignalCookie*)cookie;
	job->backend->GetSignalQuality(job->modemPath.String(), reply);
	delete job;
}


status_t
ModemManagerBackend::GetSignalQualityAsync(const char* modemPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL)
		return B_BAD_VALUE;

	if (fDBusConnection == NULL || fMainContext == NULL)
		return B_ERROR;

	_GetSignalCookie* cookie = new _GetSignalCookie;
	cookie->backend = this;
	cookie->modemPath = modemPath;

	return _RunOnDispatchThread(_RunGetSignalQualityAsync, cookie, replyTo,
		replyWhat);
}


// #pragma mark - SIM status / PIN unlock


status_t
ModemManagerBackend::GetSimStatus(const char* modemPath, BMessage* outSim)
{
	if (modemPath == NULL || outSim == NULL)
		return B_BAD_VALUE;

	if (fDBusConnection == NULL)
		return B_ERROR;

	// Sim object path first; fall back to the modem snapshot's stored path.
	BString simPath;
	{
		BAutolock lock(fLock);
		std::map<BString, BMessage>::iterator it
			= fModemSnapshot.find(modemPath);
		if (it != fModemSnapshot.end())
			it->second.FindString(kMMFieldModemSimPath, &simPath);
	}

	if (simPath.IsEmpty()) {
		GError* error = NULL;
		GVariant* unwrapped = _MMGetProperty(
			(GDBusConnection*)fDBusConnection, modemPath,
			kMMModemInterface, "Sim", &error);
		if (unwrapped != NULL
				&& g_variant_is_of_type(unwrapped,
					G_VARIANT_TYPE_OBJECT_PATH))
			simPath = g_variant_get_string(unwrapped, NULL);
		if (unwrapped != NULL)
			g_variant_unref(unwrapped);
		if (error != NULL)
			g_error_free(error);
	}

	outSim->MakeEmpty();
	outSim->AddString(kMMFieldModemPath, modemPath);
	outSim->AddString(kMMFieldModemSimPath, simPath);

	// Lock state lives on the Modem object (UnlockRequired/UnlockRetries),
	// not on the Sim interface.
	GError* error = NULL;
	GVariant* lockVal = _MMGetProperty(
		(GDBusConnection*)fDBusConnection, modemPath,
		kMMModemInterface, "UnlockRequired", &error);
	if (lockVal == NULL) {
		if (error != NULL)
			g_error_free(error);
		error = NULL;

		if (simPath.IsEmpty()) {
			outSim->AddInt32("status", (int32)B_ENTRY_NOT_FOUND);
			return B_ENTRY_NOT_FOUND;
		}
		outSim->AddInt32("status", (int32)B_ERROR);
		outSim->AddString("reason", "UnlockRequired unavailable");
		return B_ERROR;
	}

	uint32 lock = g_variant_get_uint32(lockVal);
	g_variant_unref(lockVal);

	bool pinRequired = lock == kMMLockSimPin || lock == kMMLockSimPin2;
	bool pukRequired = lock == kMMLockSimPuk || lock == kMMLockSimPuk2;
	uint32 retries = 0;

	GVariant* retriesVal = _MMGetProperty(
		(GDBusConnection*)fDBusConnection, modemPath,
		kMMModemInterface, "UnlockRetries", &error);
	if (retriesVal != NULL
			&& g_variant_is_of_type(retriesVal, G_VARIANT_TYPE("a{uu}"))) {
		guint32 retriesKey = pinRequired
			? (lock == kMMLockSimPin2 ? kMMLockSimPin2 : kMMLockSimPin)
			: (lock == kMMLockSimPuk2 ? kMMLockSimPuk2 : kMMLockSimPuk);
		GVariantIter* iter = g_variant_iter_new(retriesVal);
		guint32 entryKey = 0;
		guint32 entryValue = 0;
		while (g_variant_iter_next(iter, "{uu}", &entryKey, &entryValue)) {
			if (entryKey == retriesKey) {
				retries = entryValue;
				break;
			}
		}
		g_variant_iter_free(iter);
	}
	if (retriesVal != NULL)
		g_variant_unref(retriesVal);
	if (error != NULL)
		g_error_free(error);

	outSim->AddUInt32(kMMFieldSimUnlockRequired, lock);
	outSim->AddBool(kMMFieldSimLocked, pinRequired || pukRequired);
	outSim->AddBool(kMMFieldSimPinRequired, pinRequired);
	outSim->AddBool(kMMFieldSimPukRequired, pukRequired);
	outSim->AddUInt32(kMMFieldSimUnlockRetries, retries);
	outSim->AddUInt32(kMMFieldSimStatus, lock);
	outSim->AddInt32("status", (int32)B_OK);
	return B_OK;
}


struct _GetSimCookie {
	ModemManagerBackend* backend;
	BString modemPath;
	BMessenger replyTo;
	uint32 replyWhat;
};


static void
_RunGetSimStatusAsync(void* cookie, BMessage* reply)
{
	_GetSimCookie* job = (_GetSimCookie*)cookie;
	job->backend->GetSimStatus(job->modemPath.String(), reply);
	delete job;
}


status_t
ModemManagerBackend::GetSimStatusAsync(const char* modemPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL)
		return B_BAD_VALUE;

	_GetSimCookie* cookie = new _GetSimCookie;
	cookie->backend = this;
	cookie->modemPath = modemPath;

	return _RunOnDispatchThread(_RunGetSimStatusAsync, cookie, replyTo,
		replyWhat);
}


struct _UnlockSimJob {
	ModemManagerBackend* backend;
	GDBusConnection* connection;
	BString modemPath;
	BString pin;
	BMessenger replyTo;
	uint32 replyWhat;
};


// Resolves the SIM object path from the Modem.Sim property.
static bool
_ResolveSimPath(GDBusConnection* connection, const char* modemPath,
	BString& outSimPath)
{
	GError* error = NULL;
	GVariant* unwrapped = _MMGetProperty(connection, modemPath,
		kMMModemInterface, "Sim", &error);
	if (error != NULL)
		g_error_free(error);
	if (unwrapped == NULL)
		return false;

	bool ok = g_variant_is_of_type(unwrapped, G_VARIANT_TYPE_OBJECT_PATH);
	if (ok)
		outSimPath = g_variant_get_string(unwrapped, NULL);
	g_variant_unref(unwrapped);
	return ok && !outSimPath.IsEmpty();
}


// SendPin is a method of org.freedesktop.ModemManager1.Sim on the SIM
// object, not of Modem3gpp on the modem.
static status_t
_SendPinToSim(GDBusConnection* connection, const char* modemPath,
	const char* pin)
{
	BString simPath;
	if (!_ResolveSimPath(connection, modemPath, simPath))
		return B_ENTRY_NOT_FOUND;

	GError* error = NULL;
	GVariant* result = g_dbus_connection_call_sync(connection,
		kMMBusName, simPath.String(),
		kMMSimInterface, "SendPin",
		g_variant_new("(s)", pin),
		NULL, G_DBUS_CALL_FLAGS_NONE,
		kMMCallTimeoutMs, NULL, &error);

	status_t status = B_OK;
	if (result == NULL) {
		fprintf(stderr, "ModemManagerBackend: SendPin failed: %s\n",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
		status = B_ERROR;
	} else {
		g_variant_unref(result);
	}
	return status;
}


static gboolean
_RunUnlockSim(gpointer data)
{
	_UnlockSimJob* job = (_UnlockSimJob*)data;

	status_t sendStatus = _SendPinToSim(job->connection,
		job->modemPath.String(), job->pin.String());

	BMessage reply(job->replyWhat);
	if (sendStatus == B_OK) {
		reply.AddInt32("status", (int32)B_OK);
	} else {
		reply.AddInt32("status", (int32)sendStatus);
		reply.AddString("reason", sendStatus == B_ENTRY_NOT_FOUND
			? "no SIM object on modem" : "SendPin failed");
	}

	job->replyTo.SendMessage(&reply);

	if (reply.FindInt32("status") == B_OK)
		job->backend->_RefreshSnapshot();

	delete job;
	return G_SOURCE_REMOVE;
}


status_t
ModemManagerBackend::UnlockSim(const char* modemPath, const char* pin)
{
	if (modemPath == NULL || pin == NULL || pin[0] == '\0')
		return B_BAD_VALUE;

	if (fDBusConnection == NULL)
		return B_ERROR;

	status_t status = _SendPinToSim((GDBusConnection*)fDBusConnection,
		modemPath, pin);
	if (status == B_OK && fMainContext != NULL) {
		g_main_context_invoke((GMainContext*)fMainContext,
			[](gpointer data) -> gboolean {
				((ModemManagerBackend*)data)->_RefreshSnapshot();
				return G_SOURCE_REMOVE;
			}, this);
	}

	return status;
}


status_t
ModemManagerBackend::UnlockSimAsync(const char* modemPath, const char* pin,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL || pin == NULL || pin[0] == '\0')
		return B_BAD_VALUE;

	if (fDBusConnection == NULL || fMainContext == NULL)
		return B_ERROR;

	_UnlockSimJob* job = new _UnlockSimJob;
	job->backend = this;
	job->connection = (GDBusConnection*)fDBusConnection;
	job->modemPath = modemPath;
	job->pin = pin;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext, _RunUnlockSim, job);
	return B_OK;
}


// #pragma mark - StartWatching / StopWatching


status_t
ModemManagerBackend::StartWatching(const BMessenger& target,
	uint32 notificationMask)
{
	BAutolock lock(fLock);

	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers[i].mask = notificationMask;
			return B_OK;
		}
	}

	Watcher watcher;
	watcher.messenger = target;
	watcher.mask = notificationMask;
	fWatchers.push_back(watcher);

	// Subscribe to D-Bus signals on first watcher.
	if (fDBusConnection != NULL
			&& fPropertiesChangedSubscriptionId == 0) {
		g_main_context_invoke((GMainContext*)fMainContext,
			[](gpointer data) -> gboolean {
				ModemManagerBackend* backend
					= (ModemManagerBackend*)data;
				if (backend->fDBusConnection == NULL)
					return G_SOURCE_REMOVE;

				GError* error = NULL;
				GDBusConnection* conn
					= (GDBusConnection*)backend->fDBusConnection;

				// Subscribe to PropertiesChanged on the manager path
				// to catch modem state/signal changes.
				backend->fPropertiesChangedSubscriptionId
					= g_dbus_connection_signal_subscribe(conn,
						kMMBusName,
						"org.freedesktop.DBus.Properties",
						"PropertiesChanged",
						NULL,	// any object path
						NULL,
						G_DBUS_SIGNAL_FLAGS_NONE,
						_PropertiesChangedCallback, backend,
						NULL);

				// Subscribe to InterfacesAdded/Removed on the manager
				// path for modem hotplug.
				backend->fInterfacesAddedSubscriptionId
					= g_dbus_connection_signal_subscribe(conn,
						kMMBusName,
						kMMObjectManagerInterface,
						"InterfacesAdded",
						kMMObjectPath,
						NULL,
						G_DBUS_SIGNAL_FLAGS_NONE,
						_InterfacesAddedCallback, backend,
						NULL);

				backend->fInterfacesRemovedSubscriptionId
					= g_dbus_connection_signal_subscribe(conn,
						kMMBusName,
						kMMObjectManagerInterface,
						"InterfacesRemoved",
						kMMObjectPath,
						NULL,
						G_DBUS_SIGNAL_FLAGS_NONE,
						_InterfacesRemovedCallback, backend,
						NULL);

				if (error != NULL) {
					fprintf(stderr, "ModemManagerBackend: "
						"signal subscribe failed: %s\n",
						error->message);
					g_error_free(error);
				}

				return G_SOURCE_REMOVE;
			}, this);
	}

	return B_OK;
}


status_t
ModemManagerBackend::StopWatching(const BMessenger& target)
{
	BAutolock lock(fLock);

	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers.erase(fWatchers.begin() + i);
			break;
		}
	}

	// Unsubscribe D-Bus signals when last watcher leaves.
	if (fWatchers.empty() && fDBusConnection != NULL) {
		GDBusConnection* conn = (GDBusConnection*)fDBusConnection;
		if (fPropertiesChangedSubscriptionId != 0) {
			g_dbus_connection_signal_unsubscribe(conn,
				fPropertiesChangedSubscriptionId);
			fPropertiesChangedSubscriptionId = 0;
		}
		if (fInterfacesAddedSubscriptionId != 0) {
			g_dbus_connection_signal_unsubscribe(conn,
				fInterfacesAddedSubscriptionId);
			fInterfacesAddedSubscriptionId = 0;
		}
		if (fInterfacesRemovedSubscriptionId != 0) {
			g_dbus_connection_signal_unsubscribe(conn,
				fInterfacesRemovedSubscriptionId);
			fInterfacesRemovedSubscriptionId = 0;
		}
	}

	return B_OK;
}
