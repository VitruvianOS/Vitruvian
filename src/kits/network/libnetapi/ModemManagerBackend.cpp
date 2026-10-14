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
static const char* kMMVoiceInterface
	= "org.freedesktop.ModemManager1.Modem.Voice";
static const char* kMMMessagingInterface
	= "org.freedesktop.ModemManager1.Modem.Messaging";
static const char* kMMCallInterface = "org.freedesktop.ModemManager1.Call";
static const char* kMMSmsInterface = "org.freedesktop.ModemManager1.Sms";


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
static const int32 kMMStateFailed = -1;
static const int32 kMMStateUnknown = 0;
static const int32 kMMStateInitializing = 1;
static const int32 kMMStateLocked = 2;
static const int32 kMMStateDisabled = 3;
static const int32 kMMStateDisabling = 4;
static const int32 kMMStateEnabling = 5;
static const int32 kMMStateEnabled = 6;
static const int32 kMMStateSearching = 7;
static const int32 kMMStateRegistered = 8;
static const int32 kMMStateDisconnecting = 9;
static const int32 kMMStateConnecting = 10;
static const int32 kMMStateConnected = 11;

// MMModemLock values used for SIM unlock state.
static const uint32 kMMLockUnknown = 0;
static const uint32 kMMLockNone = 1;
static const uint32 kMMLockSimPin = 2;
static const uint32 kMMLockSimPin2 = 3;
static const uint32 kMMLockSimPuk = 4;
static const uint32 kMMLockSimPuk2 = 5;

// MMModem3gppRegistrationState values.
static const uint32 kMMRegIdle = 0;
static const uint32 kMMRegHome = 1;
static const uint32 kMMRegSearching = 2;
static const uint32 kMMRegDenied = 3;
static const uint32 kMMRegUnknown = 4;
static const uint32 kMMRegRoaming = 5;
static const uint32 kMMRegHomeSmsOnly = 6;
static const uint32 kMMRegRoamingSmsOnly = 7;
static const uint32 kMMRegEmergencyOnly = 8;
static const uint32 kMMRegHomeCsfbNotPreferred = 9;
static const uint32 kMMRegRoamingCsfbNotPreferred = 10;
static const uint32 kMMRegAttachedRlos = 11;

// MMModemAccessTechnology bits (Modem.AccessTechnologies).
static const uint32 kMMAccessGsm = 1 << 0;
static const uint32 kMMAccessGprs = 1 << 1;
static const uint32 kMMAccessEdge = 1 << 2;
static const uint32 kMMAccessUmts = 1 << 3;
static const uint32 kMMAccessHsdpa = 1 << 4;
static const uint32 kMMAccessHsupa = 1 << 5;
static const uint32 kMMAccessHspa = 1 << 6;
static const uint32 kMMAccessHspaPlus = 1 << 7;
static const uint32 kMMAccessEhrpd = 1 << 8;
static const uint32 kMMAccessLte = 1 << 9;
static const uint32 kMMAccessNr5g = 1 << 10;
static const uint32 kMMAccessLteCa = 1 << 11;

// MMCallState values (Call.State).
static const uint32 kMMCallStateUnknown = 0;
static const uint32 kMMCallStateDialing = 1;
static const uint32 kMMCallStateRingingOut = 2;
static const uint32 kMMCallStateRingingIn = 3;
static const uint32 kMMCallStateActive = 4;
static const uint32 kMMCallStateHeld = 5;
static const uint32 kMMCallStateWaiting = 6;
static const uint32 kMMCallStateTerminated = 7;

// MMCallDirection values (Call.Direction).
static const uint32 kMMCallDirectionUnknown = 0;
static const uint32 kMMCallDirectionIncoming = 1;
static const uint32 kMMCallDirectionOutgoing = 2;

// MM_SMS_STATE values (Sms.State).
static const uint32 kMMSmsStateUnknown = 0;
static const uint32 kMMSmsStateStored = 1;
static const uint32 kMMSmsStateReceiving = 2;
static const uint32 kMMSmsStateReceived = 3;
static const uint32 kMMSmsStateSending = 4;
static const uint32 kMMSmsStateSent = 5;

// MM_SMS_PDU_TYPE_DELIVER: a message that arrived from the network.
static const uint32 kMMSmsPduTypeDeliver = 1;


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
		backend->fCallSnapshot.clear();
		backend->fSmsSnapshot.clear();
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
_ModemStateString(int32 state)
{
	switch (state) {
		case kMMStateFailed:
			return "failed";
		case kMMStateUnknown:
			return "unknown";
		case kMMStateInitializing:
			return "initializing";
		case kMMStateLocked:
			return "locked";
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


static const char*
_RegistrationName(uint32 state)
{
	switch (state) {
		case kMMRegIdle:
			return "idle";
		case kMMRegHome:
			return "home";
		case kMMRegSearching:
			return "searching";
		case kMMRegDenied:
			return "denied";
		case kMMRegRoaming:
			return "roaming";
		case kMMRegHomeSmsOnly:
			return "home-sms-only";
		case kMMRegRoamingSmsOnly:
			return "roaming-sms-only";
		case kMMRegEmergencyOnly:
			return "emergency-only";
		case kMMRegHomeCsfbNotPreferred:
			return "home-csfb-not-preferred";
		case kMMRegRoamingCsfbNotPreferred:
			return "roaming-csfb-not-preferred";
		case kMMRegAttachedRlos:
			return "attached-rlos";
		case kMMRegUnknown:
		default:
			return "unknown";
	}
}


// Coarse technology class from the MMModemAccessTechnology bit mask.
static const char*
_AccessTypeName(uint32 mask)
{
	if ((mask & kMMAccessNr5g) != 0)
		return "5G";
	if ((mask & (kMMAccessLte | kMMAccessLteCa | kMMAccessEhrpd)) != 0)
		return "4G";
	if ((mask & (kMMAccessUmts | kMMAccessHsdpa | kMMAccessHsupa
			| kMMAccessHspa | kMMAccessHspaPlus)) != 0)
		return "3G";
	if ((mask & (kMMAccessGsm | kMMAccessGprs | kMMAccessEdge)) != 0)
		return "2G";
	return "unknown";
}


static bool
_IsRoamingRegistration(uint32 state)
{
	return state == kMMRegRoaming || state == kMMRegRoamingSmsOnly
		|| state == kMMRegRoamingCsfbNotPreferred;
}


static const char*
_CallStateName(uint32 state)
{
	switch (state) {
		case kMMCallStateDialing:
			return "dialing";
		case kMMCallStateRingingOut:
			return "ringing-out";
		case kMMCallStateRingingIn:
			return "ringing-in";
		case kMMCallStateActive:
			return "active";
		case kMMCallStateHeld:
			return "held";
		case kMMCallStateWaiting:
			return "waiting";
		case kMMCallStateTerminated:
			return "terminated";
		case kMMCallStateUnknown:
		default:
			return "unknown";
	}
}


static const char*
_CallDirectionName(uint32 direction)
{
	switch (direction) {
		case kMMCallDirectionIncoming:
			return "incoming";
		case kMMCallDirectionOutgoing:
			return "outgoing";
		case kMMCallDirectionUnknown:
		default:
			return "unknown";
	}
}


static const char*
_SmsStateName(uint32 state)
{
	switch (state) {
		case kMMSmsStateStored:
			return "stored";
		case kMMSmsStateReceiving:
			return "receiving";
		case kMMSmsStateReceived:
			return "received";
		case kMMSmsStateSending:
			return "sending";
		case kMMSmsStateSent:
			return "sent";
		case kMMSmsStateUnknown:
		default:
			return "unknown";
	}
}


// Reads properties from the Modem, Modem3gpp and Sim interface dicts of
// one ObjectManager entry. SignalQuality is a Modem property; OperatorName
// and RegistrationState live on Modem3gpp.
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
			int32 state = g_variant_get_int32(val);
			outInfo->AddInt32(kMMFieldModemState, state);
			outInfo->AddString(kMMFieldModemState,
				_ModemStateString(state));
			outInfo->AddBool(kMMFieldModemEnabled,
				state >= kMMStateEnabled);
			g_variant_unref(val);
		} else {
			outInfo->AddInt32(kMMFieldModemState, kMMStateUnknown);
			outInfo->AddString(kMMFieldModemState, "unknown");
			outInfo->AddBool(kMMFieldModemEnabled, false);
		}

		val = g_variant_lookup_value(modemProps, "Manufacturer",
			G_VARIANT_TYPE_STRING);
		if (val != NULL) {
			outInfo->AddString(kMMFieldModemManufacturer,
				g_variant_get_string(val, NULL));
			g_variant_unref(val);
		}

		val = g_variant_lookup_value(modemProps, "Model",
			G_VARIANT_TYPE_STRING);
		if (val != NULL) {
			outInfo->AddString(kMMFieldModemModel,
				g_variant_get_string(val, NULL));
			g_variant_unref(val);
		}

		val = g_variant_lookup_value(modemProps, "AccessTechnologies",
			G_VARIANT_TYPE_UINT32);
		if (val != NULL) {
			uint32 mask = g_variant_get_uint32(val);
			outInfo->AddUInt32(kMMFieldModemAccessTechMask, mask);
			outInfo->AddString(kMMFieldModemAccessType,
				_AccessTypeName(mask));
			g_variant_unref(val);
		} else {
			outInfo->AddUInt32(kMMFieldModemAccessTechMask, 0);
			outInfo->AddString(kMMFieldModemAccessType, "unknown");
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
		outInfo->AddInt32(kMMFieldModemState, kMMStateUnknown);
		outInfo->AddString(kMMFieldModemState, "unknown");
		outInfo->AddBool(kMMFieldModemEnabled, false);
		outInfo->AddUInt32(kMMFieldSimUnlockRequired, kMMLockUnknown);
		outInfo->AddBool(kMMFieldSimLocked, false);
		outInfo->AddBool(kMMFieldSimPinRequired, false);
		outInfo->AddBool(kMMFieldSimPukRequired, false);
		outInfo->AddUInt32(kMMFieldSimUnlockRetries, 0);
		outInfo->AddUInt32(kMMFieldSimStatus, kMMLockUnknown);
		outInfo->AddUInt32(kMMFieldModemRegistrationState, kMMRegUnknown);
		outInfo->AddString(kMMFieldModemRegistrationName, "unknown");
		outInfo->AddBool(kMMFieldModemIsRoaming, false);
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

		val = g_variant_lookup_value(gppProps, "OperatorCode",
			G_VARIANT_TYPE_STRING);
		if (val != NULL) {
			outInfo->AddString(kMMFieldModemOperatorCode,
				g_variant_get_string(val, NULL));
			g_variant_unref(val);
		}

		val = g_variant_lookup_value(gppProps, "RegistrationState",
			G_VARIANT_TYPE_UINT32);
		if (val != NULL) {
			uint32 state = g_variant_get_uint32(val);
			outInfo->AddUInt32(kMMFieldModemRegistrationState, state);
			outInfo->AddString(kMMFieldModemRegistrationName,
				_RegistrationName(state));
			outInfo->AddBool(kMMFieldModemIsRoaming,
				_IsRoamingRegistration(state));
			g_variant_unref(val);
		} else {
			outInfo->AddUInt32(kMMFieldModemRegistrationState,
				kMMRegUnknown);
			outInfo->AddString(kMMFieldModemRegistrationName,
				"unknown");
			outInfo->AddBool(kMMFieldModemIsRoaming, false);
		}
		g_variant_unref(gppProps);
	} else {
		outInfo->AddUInt32(kMMFieldModemRegistrationState, kMMRegUnknown);
		outInfo->AddString(kMMFieldModemRegistrationName, "unknown");
		outInfo->AddBool(kMMFieldModemIsRoaming, false);
	}
}


// Fills one Call object entry: state, direction and remote number.
static void
_FillCallInfo(GVariant* interfaces, const char* objectPath,
	const char* modemPath, BMessage* outInfo)
{
	outInfo->MakeEmpty();
	outInfo->AddString(kMMFieldCallPath,
		objectPath != NULL ? objectPath : "");
	outInfo->AddString(kMMFieldCallModemPath,
		modemPath != NULL ? modemPath : "");

	GVariant* callProps = g_variant_lookup_value(interfaces,
		kMMCallInterface, G_VARIANT_TYPE("a{sv}"));
	if (callProps == NULL)
		return;

	GVariant* val = g_variant_lookup_value(callProps, "State",
		G_VARIANT_TYPE_INT32);
	if (val != NULL) {
		uint32 state = (uint32)g_variant_get_int32(val);
		outInfo->AddUInt32(kMMFieldCallState, state);
		outInfo->AddString(kMMFieldCallStateName, _CallStateName(state));
		g_variant_unref(val);
	} else {
		outInfo->AddUInt32(kMMFieldCallState, kMMCallStateUnknown);
		outInfo->AddString(kMMFieldCallStateName, "unknown");
	}

	val = g_variant_lookup_value(callProps, "StateReason",
		G_VARIANT_TYPE_INT32);
	if (val != NULL) {
		outInfo->AddUInt32(kMMFieldCallReason,
			(uint32)g_variant_get_int32(val));
		g_variant_unref(val);
	}

	val = g_variant_lookup_value(callProps, "Direction",
		G_VARIANT_TYPE_INT32);
	if (val != NULL) {
		uint32 direction = (uint32)g_variant_get_int32(val);
		outInfo->AddUInt32(kMMFieldCallDirection, direction);
		outInfo->AddString(kMMFieldCallDirectionName,
			_CallDirectionName(direction));
		g_variant_unref(val);
	} else {
		outInfo->AddUInt32(kMMFieldCallDirection, kMMCallDirectionUnknown);
		outInfo->AddString(kMMFieldCallDirectionName, "unknown");
	}

	val = g_variant_lookup_value(callProps, "Number",
		G_VARIANT_TYPE_STRING);
	if (val != NULL) {
		outInfo->AddString(kMMFieldCallNumber,
			g_variant_get_string(val, NULL));
		g_variant_unref(val);
	}

	g_variant_unref(callProps);
}


// Fills one SMS object entry. Text is the assembled text once the
// modem finished reassembling a multipart message.
static void
_FillSmsInfo(GVariant* interfaces, const char* objectPath,
	const char* modemPath, BMessage* outInfo)
{
	outInfo->MakeEmpty();
	outInfo->AddString(kMMFieldSmsPath,
		objectPath != NULL ? objectPath : "");
	outInfo->AddString(kMMFieldSmsModemPath,
		modemPath != NULL ? modemPath : "");

	GVariant* smsProps = g_variant_lookup_value(interfaces,
		kMMSmsInterface, G_VARIANT_TYPE("a{sv}"));
	if (smsProps == NULL)
		return;

	GVariant* val = g_variant_lookup_value(smsProps, "State",
		G_VARIANT_TYPE_UINT32);
	if (val != NULL) {
		uint32 state = g_variant_get_uint32(val);
		outInfo->AddUInt32(kMMFieldSmsState, state);
		outInfo->AddString(kMMFieldSmsStateName, _SmsStateName(state));
		g_variant_unref(val);
	} else {
		outInfo->AddUInt32(kMMFieldSmsState, kMMSmsStateUnknown);
		outInfo->AddString(kMMFieldSmsStateName, "unknown");
	}

	val = g_variant_lookup_value(smsProps, "PduType",
		G_VARIANT_TYPE_UINT32);
	if (val != NULL) {
		guint32 pduType = g_variant_get_uint32(val);
		outInfo->AddUInt32(kMMFieldSmsPduType, pduType);
		outInfo->AddBool(kMMFieldSmsReceived,
			pduType == kMMSmsPduTypeDeliver);
		g_variant_unref(val);
	}

	val = g_variant_lookup_value(smsProps, "Number",
		G_VARIANT_TYPE_STRING);
	if (val != NULL) {
		outInfo->AddString(kMMFieldSmsNumber,
			g_variant_get_string(val, NULL));
		g_variant_unref(val);
	}

	val = g_variant_lookup_value(smsProps, "Text",
		G_VARIANT_TYPE_STRING);
	if (val != NULL) {
		outInfo->AddString(kMMFieldSmsText,
			g_variant_get_string(val, NULL));
		g_variant_unref(val);
	}

	val = g_variant_lookup_value(smsProps, "Timestamp",
		G_VARIANT_TYPE_STRING);
	if (val != NULL) {
		outInfo->AddString(kMMFieldSmsTimestamp,
			g_variant_get_string(val, NULL));
		g_variant_unref(val);
	}

	val = g_variant_lookup_value(smsProps, "DeliveryState",
		G_VARIANT_TYPE_UINT32);
	if (val != NULL) {
		outInfo->AddUInt32(kMMFieldSmsDeliveryState,
			g_variant_get_uint32(val));
		g_variant_unref(val);
	}

	// Multipart: the Parts array lists the segments already collected;
	// Text stays empty until the last one lands.
	val = g_variant_lookup_value(smsProps, "Parts",
		G_VARIANT_TYPE_OBJECT_PATH_ARRAY);
	if (val != NULL) {
		outInfo->AddInt32(kMMFieldSmsPartCount,
			(int32)g_variant_n_children(val));
		g_variant_unref(val);
	}

	g_variant_unref(smsProps);
}


// Walks the ModemManager ObjectManager reply and populates the snapshot.
static void
_FillModemsFromManagedObjects(GVariant* managedObjects,
	std::map<BString, BMessage>& modemSnapshot,
	std::map<BString, BMessage>& callSnapshot,
	std::map<BString, BMessage>& smsSnapshot,
	BMessage* outList)
{
	outList->MakeEmpty();

	if (managedObjects == NULL) {
		outList->AddBool(kMMFieldMMAvailable, true);
		outList->AddInt32(kMMFieldModemCount, 0);
		return;
	}

	outList->AddBool(kMMFieldMMAvailable, true);

	// Pass one: keep every object's interface dict around so a modem's
	// Voice.Calls / Messaging.Messages arrays can resolve their paths.
	std::map<BString, GVariant*> objects;
	GVariantIter* iter = g_variant_iter_new(managedObjects);
	gchar* objectPath = NULL;
	GVariant* interfaces = NULL;
	while (g_variant_iter_next(iter, "{o@a{sa{sv}}}", &objectPath,
			&interfaces)) {
		objects[objectPath] = interfaces;
		g_free(objectPath);
	}
	g_variant_iter_free(iter);

	// Pass two: modems, then their calls and messages.
	int32 count = 0;
	for (std::map<BString, GVariant*>::iterator it = objects.begin();
			it != objects.end(); it++) {
		GVariant* modemProps = g_variant_lookup_value(it->second,
			kMMModemInterface, G_VARIANT_TYPE("a{sv}"));
		if (modemProps == NULL)
			continue;

		BMessage modemInfo;
		_FillModemInfoFromInterfaces(it->second, it->first.String(),
			&modemInfo);
		modemSnapshot[it->first] = modemInfo;

		char modName[32];
		snprintf(modName, sizeof(modName), "modem_%" B_PRId32, count);
		outList->AddMessage(modName, &modemInfo);
		count++;

		g_variant_unref(modemProps);

		GVariant* voiceProps = g_variant_lookup_value(it->second,
			kMMVoiceInterface, G_VARIANT_TYPE("a{sv}"));
		if (voiceProps != NULL) {
			GVariant* calls = g_variant_lookup_value(voiceProps,
				"Calls", G_VARIANT_TYPE_OBJECT_PATH_ARRAY);
			if (calls != NULL) {
				GVariantIter* callIter = g_variant_iter_new(calls);
				gchar* callPath = NULL;
				while (g_variant_iter_next(callIter, "o", &callPath)) {
					std::map<BString, GVariant*>::iterator callIt
						= objects.find(callPath);
					if (callIt != objects.end()) {
						BMessage callInfo;
						_FillCallInfo(callIt->second, callPath,
							it->first.String(), &callInfo);
						callSnapshot[callPath] = callInfo;
					}
					g_free(callPath);
				}
				g_variant_iter_free(callIter);
				g_variant_unref(calls);
			}
			g_variant_unref(voiceProps);
		}

		GVariant* messagingProps = g_variant_lookup_value(it->second,
			kMMMessagingInterface, G_VARIANT_TYPE("a{sv}"));
		if (messagingProps != NULL) {
			GVariant* messages = g_variant_lookup_value(
				messagingProps, "Messages",
				G_VARIANT_TYPE_OBJECT_PATH_ARRAY);
			if (messages != NULL) {
				GVariantIter* smsIter = g_variant_iter_new(messages);
				gchar* smsPath = NULL;
				while (g_variant_iter_next(smsIter, "o", &smsPath)) {
					std::map<BString, GVariant*>::iterator smsIt
						= objects.find(smsPath);
					if (smsIt != objects.end()) {
						BMessage smsInfo;
						_FillSmsInfo(smsIt->second, smsPath,
							it->first.String(), &smsInfo);
						smsSnapshot[smsPath] = smsInfo;
					}
					g_free(smsPath);
				}
				g_variant_iter_free(smsIter);
				g_variant_unref(messages);
			}
			g_variant_unref(messagingProps);
		}
	}

	for (std::map<BString, GVariant*>::iterator it = objects.begin();
			it != objects.end(); it++)
		g_variant_unref(it->second);

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
		fCallSnapshot.clear();
		fSmsSnapshot.clear();
		fModemListSnapshot.MakeEmpty();
		fModemListSnapshot.AddBool(kMMFieldMMAvailable, true);
		fModemListSnapshot.AddInt32(kMMFieldModemCount, 0);
		fSnapshotPopulated = true;
		return;
	}

	GVariant* managedObjects = g_variant_get_child_value(result, 0);

	std::map<BString, BMessage> newSnapshot;
	std::map<BString, BMessage> newCalls;
	std::map<BString, BMessage> newMessages;
	BMessage newList;
	_FillModemsFromManagedObjects(managedObjects, newSnapshot, newCalls,
		newMessages, &newList);

	g_variant_unref(managedObjects);
	g_variant_unref(result);

	{
		BAutolock lock(fLock);
		fModemSnapshot = newSnapshot;
		fCallSnapshot = newCalls;
		fSmsSnapshot = newMessages;
		fModemListSnapshot = newList;
		fSnapshotPopulated = true;
	}
}


// #pragma mark - D-Bus signal callbacks


static void
_CopyStringField(const BMessage& from, BMessage& to, const char* key)
{
	BString value;
	if (from.FindString(key, &value) == B_OK)
		to.AddString(key, value.String());
}


static void
_CopyUInt32Field(const BMessage& from, BMessage& to, const char* key)
{
	uint32 value = 0;
	if (from.FindUInt32(key, &value) == B_OK)
		to.AddUInt32(key, value);
}


static void
_CopyBoolField(const BMessage& from, BMessage& to, const char* key)
{
	bool value = false;
	if (from.FindBool(key, &value) == B_OK)
		to.AddBool(key, value);
}


void
ModemManagerBackend::_PropertiesChangedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName,
	GVariant* parameters, void* userData)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)userData;

	if (interfaceName == NULL)
		return;

	// Call and SMS objects change far more often than modems and carry
	// their own notification types; update one snapshot entry in place.
	if (strcmp(interfaceName, kMMCallInterface) == 0
			|| strcmp(interfaceName, kMMSmsInterface) == 0) {
		backend->_RefreshSnapshot();

		bool isCall = strcmp(interfaceName, kMMCallInterface) == 0;
		BMessage message((uint32)(isCall
			? NOTIFICATION_CALL_STATE_CHANGED
			: NOTIFICATION_SMS_STATE_CHANGED));
		message.AddString(kMMFieldCallPath,
			objectPath != NULL ? objectPath : "");
		message.AddString(kMMFieldSmsPath,
			objectPath != NULL ? objectPath : "");

		BAutolock lock(backend->fLock);
		std::map<BString, BMessage>::iterator it = isCall
			? backend->fCallSnapshot.find(
				objectPath != NULL ? objectPath : "")
			: backend->fSmsSnapshot.find(
				objectPath != NULL ? objectPath : "");
		if (isCall && it != backend->fCallSnapshot.end()) {
			_CopyStringField(it->second, message,
				kMMFieldCallModemPath);
			_CopyStringField(it->second, message, kMMFieldCallNumber);
			_CopyUInt32Field(it->second, message, kMMFieldCallState);
			_CopyUInt32Field(it->second, message, kMMFieldCallDirection);
			_CopyStringField(it->second, message,
				kMMFieldCallStateName);
		} else if (!isCall && it != backend->fSmsSnapshot.end()) {
			_CopyStringField(it->second, message,
				kMMFieldSmsModemPath);
			_CopyStringField(it->second, message, kMMFieldSmsNumber);
			_CopyStringField(it->second, message, kMMFieldSmsText);
			_CopyUInt32Field(it->second, message, kMMFieldSmsState);
			_CopyStringField(it->second, message,
				kMMFieldSmsStateName);
			_CopyUInt32Field(it->second, message,
				kMMFieldSmsDeliveryState);
			_CopyBoolField(it->second, message, kMMFieldSmsReceived);
		}

		if (!backend->fWatchers.empty())
			backend->_NotifyWatchers(isCall
				? NOTIFICATION_CALL_STATE_CHANGED
				: NOTIFICATION_SMS_STATE_CHANGED, message);
		return;
	}

	// Only handle interfaces that carry modem state/signal/SIM data.
	if (strcmp(interfaceName, kMMModemInterface) != 0
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

	{
		BAutolock lock(backend->fLock);
		std::map<BString, BMessage>::iterator it
			= backend->fModemSnapshot.find(
				objectPath != NULL ? objectPath : "");
		if (it != backend->fModemSnapshot.end()) {
			_CopyStringField(it->second, message, kMMFieldModemState);
			_CopyStringField(it->second, message,
				kMMFieldModemOperatorName);
			_CopyStringField(it->second, message,
				kMMFieldModemOperatorCode);
			_CopyStringField(it->second, message,
				kMMFieldModemAccessType);
			_CopyStringField(it->second, message,
				kMMFieldModemRegistrationName);
			_CopyUInt32Field(it->second, message,
				kMMFieldModemSignalQuality);
			_CopyUInt32Field(it->second, message,
				kMMFieldModemSignalBars);
			_CopyUInt32Field(it->second, message,
				kMMFieldModemRegistrationState);
			_CopyUInt32Field(it->second, message,
				kMMFieldModemAccessTechMask);
			_CopyBoolField(it->second, message, kMMFieldSimPinRequired);
			_CopyBoolField(it->second, message, kMMFieldSimPukRequired);
			_CopyBoolField(it->second, message, kMMFieldSimLocked);
		}
	}

	BAutolock lock(backend->fLock);
	if (!backend->fWatchers.empty()) {
		backend->_NotifyWatchers(NOTIFICATION_MODEM_STATE_CHANGED, message);
		backend->_NotifyWatchers(NOTIFICATION_MODEM_SIGNAL_CHANGED, message);
		if (strcmp(interfaceName, kMMModem3gppInterface) == 0)
			backend->_NotifyWatchers(
				NOTIFICATION_REGISTRATION_CHANGED, message);
	}
}


void
ModemManagerBackend::_InterfacesAddedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName,
	GVariant* parameters, void* userData)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)userData;

	// What kind of object showed up? The second argument of
	// InterfacesAdded is its full interface dictionary.
	bool isModem = false;
	bool isCall = false;
	bool isSms = false;
	if (parameters != NULL) {
		GVariant* interfaces = NULL;
		g_variant_get(parameters, "(&o@a{sa{sv}})", NULL, &interfaces);
		if (interfaces != NULL) {
			GVariant* val = g_variant_lookup_value(interfaces,
				kMMModemInterface, G_VARIANT_TYPE("a{sv}"));
			isModem = val != NULL;
			if (val != NULL)
				g_variant_unref(val);
			val = g_variant_lookup_value(interfaces,
				kMMCallInterface, G_VARIANT_TYPE("a{sv}"));
			isCall = val != NULL;
			if (val != NULL)
				g_variant_unref(val);
			val = g_variant_lookup_value(interfaces,
				kMMSmsInterface, G_VARIANT_TYPE("a{sv}"));
			isSms = val != NULL;
			if (val != NULL)
				g_variant_unref(val);
			g_variant_unref(interfaces);
		}
	}

	backend->_RefreshSnapshot();

	if (!isModem && !isCall && !isSms)
		return;

	uint32 type = NOTIFICATION_MODEM_ADDED;
	if (isCall)
		type = NOTIFICATION_CALL_ADDED;
	else if (isSms)
		type = NOTIFICATION_SMS_ADDED;

	BMessage message(type);
	if (objectPath != NULL) {
		message.AddString(kMMFieldCallPath, objectPath);
		message.AddString(kMMFieldSmsPath, objectPath);
		if (isModem)
			message.AddString(kMMFieldModemPath, objectPath);
	}

	BAutolock lock(backend->fLock);
	std::map<BString, BMessage>::iterator it = backend->fCallSnapshot.find(
		objectPath != NULL ? objectPath : "");
	if (isCall && it != backend->fCallSnapshot.end())
		_CopyStringField(it->second, message, kMMFieldCallModemPath);

	it = backend->fSmsSnapshot.find(objectPath != NULL ? objectPath : "");
	if (isSms && it != backend->fSmsSnapshot.end()) {
		_CopyStringField(it->second, message, kMMFieldSmsModemPath);
		_CopyBoolField(it->second, message, kMMFieldSmsReceived);
	}

	if (!backend->fWatchers.empty())
		backend->_NotifyWatchers(type, message);
}


void
ModemManagerBackend::_InterfacesRemovedCallback(GDBusConnection* connection,
	const char* senderName, const char* objectPath,
	const char* interfaceName, const char* signalName,
	GVariant* parameters, void* userData)
{
	ModemManagerBackend* backend = (ModemManagerBackend*)userData;

	// Classify by what we knew about the path: the snapshot still holds
	// the entry until the refresh below drops it.
	uint32 type = NOTIFICATION_MODEM_REMOVED;
	{
		BAutolock lock(backend->fLock);
		BString path(objectPath != NULL ? objectPath : "");
		if (backend->fCallSnapshot.find(path) != backend->fCallSnapshot.end())
			type = NOTIFICATION_CALL_REMOVED;
		else if (backend->fSmsSnapshot.find(path)
				!= backend->fSmsSnapshot.end())
			type = NOTIFICATION_SMS_REMOVED;
	}

	backend->_RefreshSnapshot();

	BMessage message(type);
	if (objectPath != NULL) {
		message.AddString(kMMFieldModemPath, objectPath);
		message.AddString(kMMFieldCallPath, objectPath);
		message.AddString(kMMFieldSmsPath, objectPath);
	}

	BAutolock lock(backend->fLock);
	if (!backend->fWatchers.empty())
		backend->_NotifyWatchers(type, message);
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


// #pragma mark - Calls / messages: snapshot accessors


status_t
ModemManagerBackend::GetCalls(const char* modemPath, BMessage* outCalls)
{
	if (modemPath == NULL || outCalls == NULL)
		return B_BAD_VALUE;

	outCalls->MakeEmpty();
	outCalls->AddInt32(kMMFieldCallCount, 0);

	BAutolock lock(fLock);
	int32 count = 0;
	for (std::map<BString, BMessage>::iterator it = fCallSnapshot.begin();
			it != fCallSnapshot.end(); it++) {
		BString owner;
		if (it->second.FindString(kMMFieldCallModemPath, &owner) != B_OK
				|| owner != modemPath)
			continue;
		char name[32];
		snprintf(name, sizeof(name), "call_%" B_PRId32, count);
		outCalls->AddMessage(name, &it->second);
		count++;
	}
	outCalls->AddInt32(kMMFieldCallCount, count);
	return B_OK;
}


status_t
ModemManagerBackend::GetSmsMessages(const char* modemPath,
	BMessage* outMessages)
{
	if (modemPath == NULL || outMessages == NULL)
		return B_BAD_VALUE;

	outMessages->MakeEmpty();
	outMessages->AddInt32(kMMFieldSmsCount, 0);

	BAutolock lock(fLock);
	int32 count = 0;
	for (std::map<BString, BMessage>::iterator it = fSmsSnapshot.begin();
			it != fSmsSnapshot.end(); it++) {
		BString owner;
		if (it->second.FindString(kMMFieldSmsModemPath, &owner) != B_OK
				|| owner != modemPath)
			continue;
		char name[32];
		snprintf(name, sizeof(name), "sms_%" B_PRId32, count);
		outMessages->AddMessage(name, &it->second);
		count++;
	}
	outMessages->AddInt32(kMMFieldSmsCount, count);
	return B_OK;
}


// #pragma mark - Voice / SMS / PUK jobs


// One job struct for every one-shot D-Bus operation the telephony side
// performs; all of them reply with "status", plus "reason" on failure.
struct _TelephonyJob {
	ModemManagerBackend* backend;
	GDBusConnection* connection;
	BString modemPath;
	BString targetPath;
	BString arg;
	BString arg2;
	int32 operation;
	BMessenger replyTo;
	uint32 replyWhat;
};


enum {
	kJobDial = 1,
	kJobAnswer,
	kJobHangupCall,
	kJobHangupAll,
	kJobHold,
	kJobDtmf,
	kJobSmsSend,
	kJobSmsDelete,
	kJobSignalRate,
	kJobSimPuk
};


static GVariant*
_TelephonyCall(_TelephonyJob* job, const char* path, const char* iface,
	const char* method, GVariant* params, GError** error)
{
	return g_dbus_connection_call_sync(job->connection, kMMBusName, path,
		iface, method, params, NULL, G_DBUS_CALL_FLAGS_NONE,
		kMMCallTimeoutMs, NULL, error);
}


static gboolean
_RunTelephonyJob(gpointer data)
{
	_TelephonyJob* job = (_TelephonyJob*)data;

	GError* error = NULL;
	BMessage reply(job->replyWhat);
	BString callPath;
	BString smsPath;

	switch (job->operation) {
		case kJobDial:
		{
			// Voice.CreateCall({"number": s}) then Call.Start.
			GVariantBuilder props;
			g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
			g_variant_builder_add(&props, "{sv}", "number",
				g_variant_new_string(job->arg.String()));
			GVariant* result = _TelephonyCall(job,
				job->modemPath.String(), kMMVoiceInterface,
				"CreateCall",
				g_variant_new("(a{sv})", &props), &error);
			if (result != NULL) {
				const gchar* path = NULL;
				g_variant_get(result, "(&o)", &path);
				if (path != NULL)
					callPath = path;
				g_variant_unref(result);
			}
			if (result != NULL && !callPath.IsEmpty()) {
				GError* startError = NULL;
				GVariant* started = _TelephonyCall(job,
					callPath.String(), kMMCallInterface, "Start",
					NULL, &startError);
				if (started != NULL)
					g_variant_unref(started);
				else {
					// Start is optional on modems that dial on
					// CreateCall; only fail if create itself broke.
					if (startError != NULL)
						g_error_free(startError);
				}
			}
			break;
		}
		case kJobAnswer:
		{
			GVariant* result = _TelephonyCall(job,
				job->targetPath.String(), kMMCallInterface,
				"Accept", NULL, &error);
			if (result != NULL)
				g_variant_unref(result);
			break;
		}
		case kJobHangupCall:
		{
			GVariant* result = _TelephonyCall(job,
				job->targetPath.String(), kMMCallInterface,
				"Hangup", NULL, &error);
			if (result != NULL)
				g_variant_unref(result);
			break;
		}
		case kJobHangupAll:
		{
			GVariant* result = _TelephonyCall(job,
				job->modemPath.String(), kMMVoiceInterface,
				"HangupAll", NULL, &error);
			if (result != NULL)
				g_variant_unref(result);
			break;
		}
		case kJobHold:
		{
			// MM 1.24 holds the active call by accepting the next
			// one; with no waiting call this is a plain hold.
			GVariant* result = _TelephonyCall(job,
				job->modemPath.String(), kMMVoiceInterface,
				"HoldAndAccept", NULL, &error);
			if (result != NULL)
				g_variant_unref(result);
			break;
		}
		case kJobDtmf:
		{
			GVariant* result = _TelephonyCall(job,
				job->targetPath.String(), kMMCallInterface,
				"SendDtmf",
				g_variant_new("(s)", job->arg.String()), &error);
			if (result != NULL)
				g_variant_unref(result);
			break;
		}
		case kJobSmsSend:
		{
			// Messaging.Create({"number","text"}) then Sms.Send.
			GVariantBuilder props;
			g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
			g_variant_builder_add(&props, "{sv}", "number",
				g_variant_new_string(job->arg.String()));
			g_variant_builder_add(&props, "{sv}", "text",
				g_variant_new_string(job->arg2.String()));
			GVariant* result = _TelephonyCall(job,
				job->modemPath.String(), kMMMessagingInterface,
				"Create",
				g_variant_new("(a{sv})", &props), &error);
			if (result != NULL) {
				const gchar* path = NULL;
				g_variant_get(result, "(&o)", &path);
				if (path != NULL)
					smsPath = path;
				g_variant_unref(result);
			}
			if (!smsPath.IsEmpty()) {
				GError* sendError = NULL;
				GVariant* sent = _TelephonyCall(job,
					smsPath.String(), kMMSmsInterface, "Send",
					NULL, &sendError);
				if (sent != NULL)
					g_variant_unref(sent);
				else if (sendError != NULL) {
					if (error == NULL)
						error = sendError;
					else
						g_error_free(sendError);
				}
			}
			break;
		}
		case kJobSmsDelete:
		{
			GVariant* result = _TelephonyCall(job,
				job->modemPath.String(), kMMMessagingInterface,
				"Delete",
				g_variant_new("(o)", job->targetPath.String()),
				&error);
			if (result != NULL)
				g_variant_unref(result);
			break;
		}
		case kJobSignalRate:
		{
			error = g_error_new(G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
				"use SetupSignalRateAsync");
			break;
		}
		case kJobSimPuk:
		{
			BString simPath;
			if (_ResolveSimPath(job->connection,
					job->modemPath.String(), simPath)) {
				GVariant* result = _TelephonyCall(job,
					simPath.String(), kMMSimInterface, "SendPuk",
					g_variant_new("(ss)", job->arg.String(),
						job->arg2.String()),
					&error);
				if (result != NULL)
					g_variant_unref(result);
			} else {
				error = g_error_new(G_DBUS_ERROR,
					G_DBUS_ERROR_UNKNOWN_OBJECT,
					"no SIM object on modem");
			}
			break;
		}
		default:
			error = g_error_new(G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
				"unknown telephony job");
			break;
	}

	if (error == NULL) {
		reply.AddInt32("status", (int32)B_OK);
		if (!callPath.IsEmpty())
			reply.AddString(kMMFieldCallPath, callPath);
		if (!smsPath.IsEmpty())
			reply.AddString(kMMFieldSmsPath, smsPath);
	} else {
		reply.AddInt32("status", (int32)B_ERROR);
		reply.AddString("reason", error->message != NULL
			? error->message : "unknown error");
		g_error_free(error);
	}

	job->replyTo.SendMessage(&reply);
	job->backend->_RefreshSnapshot();
	delete job;
	return G_SOURCE_REMOVE;
}


static status_t
_QueueTelephonyJob(ModemManagerBackend* backend, GMainContext* context,
	GDBusConnection* connection, int32 operation, const char* modemPath,
	const char* targetPath, const char* arg, const char* arg2,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (connection == NULL || context == NULL)
		return B_ERROR;

	_TelephonyJob* job = new _TelephonyJob;
	job->backend = backend;
	job->connection = connection;
	if (modemPath != NULL)
		job->modemPath = modemPath;
	if (targetPath != NULL)
		job->targetPath = targetPath;
	if (arg != NULL)
		job->arg = arg;
	if (arg2 != NULL)
		job->arg2 = arg2;
	job->operation = operation;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke(context, _RunTelephonyJob, job);
	return B_OK;
}


status_t
ModemManagerBackend::DialAsync(const char* modemPath, const char* number,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL || number == NULL || number[0] == '\0')
		return B_BAD_VALUE;
	return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
		(GDBusConnection*)fDBusConnection,
		kJobDial, modemPath, NULL, number, NULL, replyTo, replyWhat);
}


status_t
ModemManagerBackend::AnswerAsync(const char* callPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (callPath == NULL)
		return B_BAD_VALUE;
	return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
		(GDBusConnection*)fDBusConnection,
		kJobAnswer, NULL, callPath, NULL, NULL, replyTo, replyWhat);
}


status_t
ModemManagerBackend::HangupAsync(const char* modemPath, const char* callPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (callPath != NULL) {
		return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
			(GDBusConnection*)fDBusConnection,
			kJobHangupCall, NULL, callPath, NULL, NULL, replyTo,
			replyWhat);
	}
	if (modemPath == NULL)
		return B_BAD_VALUE;
	return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
		(GDBusConnection*)fDBusConnection,
		kJobHangupAll, modemPath, NULL, NULL, NULL, replyTo, replyWhat);
}


status_t
ModemManagerBackend::HoldAsync(const char* modemPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL)
		return B_BAD_VALUE;
	return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
		(GDBusConnection*)fDBusConnection,
		kJobHold, modemPath, NULL, NULL, NULL, replyTo, replyWhat);
}


status_t
ModemManagerBackend::SendDtmfAsync(const char* callPath, const char* digits,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (callPath == NULL || digits == NULL)
		return B_BAD_VALUE;
	return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
		(GDBusConnection*)fDBusConnection,
		kJobDtmf, NULL, callPath, digits, NULL, replyTo, replyWhat);
}


status_t
ModemManagerBackend::SendMessageAsync(const char* modemPath,
	const char* number, const char* text, const BMessenger& replyTo,
	uint32 replyWhat)
{
	if (modemPath == NULL || number == NULL || text == NULL)
		return B_BAD_VALUE;
	return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
		(GDBusConnection*)fDBusConnection,
		kJobSmsSend, modemPath, NULL, number, text, replyTo, replyWhat);
}


status_t
ModemManagerBackend::DeleteSmsAsync(const char* modemPath,
	const char* smsPath, const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL || smsPath == NULL)
		return B_BAD_VALUE;
	return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
		(GDBusConnection*)fDBusConnection,
		kJobSmsDelete, modemPath, smsPath, NULL, NULL, replyTo,
		replyWhat);
}


struct _SignalRateJob {
	ModemManagerBackend* backend;
	GDBusConnection* connection;
	BString modemPath;
	uint32 rateSeconds;
	BMessenger replyTo;
	uint32 replyWhat;
};


static gboolean
_RunSetupSignalRate(gpointer data)
{
	_SignalRateJob* job = (_SignalRateJob*)data;

	GError* error = NULL;
	// Signal.Setup(u): 0 disables periodic sampling, N samples every
	// N seconds.
	GVariant* result = g_dbus_connection_call_sync(job->connection,
		kMMBusName, job->modemPath.String(),
		kMMModemSignalInterface, "Setup",
		g_variant_new("(u)", (guint32)job->rateSeconds),
		NULL, G_DBUS_CALL_FLAGS_NONE,
		kMMCallTimeoutMs, NULL, &error);

	BMessage reply(job->replyWhat);
	if (result != NULL) {
		reply.AddInt32("status", (int32)B_OK);
		g_variant_unref(result);
	} else {
		reply.AddInt32("status", (int32)B_ERROR);
		reply.AddString("reason", error != NULL ? error->message
			: "unknown error");
		if (error != NULL)
			g_error_free(error);
	}

	job->replyTo.SendMessage(&reply);
	delete job;
	return G_SOURCE_REMOVE;
}


status_t
ModemManagerBackend::SetupSignalRateAsync(const char* modemPath,
	uint32 rateSeconds, const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL)
		return B_BAD_VALUE;

	if (fDBusConnection == NULL || fMainContext == NULL)
		return B_ERROR;

	_SignalRateJob* job = new _SignalRateJob;
	job->backend = this;
	job->connection = (GDBusConnection*)fDBusConnection;
	job->modemPath = modemPath;
	job->rateSeconds = rateSeconds;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext,
		_RunSetupSignalRate, job);
	return B_OK;
}


status_t
ModemManagerBackend::UnlockSimPukAsync(const char* modemPath, const char* puk,
	const char* newPin, const BMessenger& replyTo, uint32 replyWhat)
{
	if (modemPath == NULL || puk == NULL || puk[0] == '\0'
			|| newPin == NULL || newPin[0] == '\0')
		return B_BAD_VALUE;
	return _QueueTelephonyJob(this, (GMainContext*)fMainContext,
		(GDBusConnection*)fDBusConnection,
		kJobSimPuk, modemPath, NULL, puk, newPin, replyTo, replyWhat);
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
