/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NMBackend.h"

#include <NetworkInterface.h>
#include <NetworkRoute.h>
#include <Messenger.h>
#include <String.h>

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <vector>

#include <gio/gio.h>

#include <NetworkManager.h>
#include <nm-client.h>
#include <nm-device.h>
#include <nm-device-wifi.h>
#include <nm-access-point.h>
#include <nm-remote-connection.h>
#include <nm-setting-connection.h>
#include <nm-setting-wireless.h>
#include <nm-setting-wired.h>
#include <nm-setting-vpn.h>
#include <nm-utils.h>
#include <nm-setting-wireguard.h>
#include <nm-setting-ip-config.h>
#include <nm-setting-ip4-config.h>
#include <nm-setting-ip6-config.h>
#include <nm-ip-config.h>
#include <nm-active-connection.h>
#include <nm-vpn-connection.h>
#include <nm-object.h>

#include <GLibSyncTimeout.h>
#include <Autolock.h>


//! Upper bound on the blocking nm_client_new() during singleton construction.
static const bigtime_t kInitTimeout = 10 * 1000000LL;


static gboolean
_QuitLoop(gpointer data)
{
	g_main_loop_quit((GMainLoop*)data);
	return G_SOURCE_REMOVE;
}


// Forward declaration: _HandleNMVanished() (defined near _InitLibNM, ahead
// of the device-snapshot section below) reuses this to rebuild the
// snapshot with kNMFieldNMAvailable == false, the same "unavailable, not
// empty" shape GetDevices()/_ConnectClientSignals() already produce.
static void _FillDevicesMessage(NMClient* nmClient, BMessage* outMessage);


// #pragma mark - _RunOnDispatchThread: async request/reply, no blocking


struct _DispatchJob {
	NMBackend::DispatchFunc func;
	void* cookie;
	BMessenger replyTo;
	uint32 replyWhat;
};


static gboolean
_RunDispatchJob(gpointer data)
{
	_DispatchJob* job = (_DispatchJob*)data;

	BMessage reply(job->replyWhat);
	job->func(job->cookie, &reply);	// func frees cookie itself
	job->replyTo.SendMessage(&reply);

	delete job;
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::_RunOnDispatchThread(DispatchFunc func, void* cookie,
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


NMBackend*
NMBackend::Instance()
{
	// Deliberately leaked: a function-local static's destructor runs at
	// exit() on whatever thread called exit(), not the dispatch thread --
	// quitting fMainLoop from there races _DispatchThread(). Leaking is the
	// standard fix; the process is exiting anyway.
	static NMBackend* instance = new NMBackend();
	return instance;
}


NMBackend::NMBackend()
	: fNMWatcherId(0),
	fNMClient(NULL),
	fMainContext(NULL),
	fMainLoop(NULL),
	fDispatchThread(-1),
	fDeviceAddedHandlerId(0),
	fDeviceRemovedHandlerId(0),
	fActiveConnectionHandlerId(0),
	fActiveAP(NULL),
	fActiveAPStrengthHandlerId(0),
	fSecretAgentRegistrationId(0),
	fPendingSecretRequestId(0)
{
	_InitLibNM();
}


NMBackend::~NMBackend()
{
	_CleanupLibNM();
}


int32
NMBackend::_DispatchThreadEntry(void* data)
{
	NMBackend* backend = (NMBackend*)data;
	backend->_DispatchThread();
	return 0;
}


void
NMBackend::_DispatchThread()
{
	if (fMainLoop == NULL)
		return;

	// Kept pushed for the thread's whole life: g_bus_watch_name() and the
	// retry nm_client_new() both run here (via g_main_context_invoke) and
	// bind to whatever context is thread-default *on this thread* -- not
	// whichever context g_main_context_invoke happens to be dispatching.
	g_main_context_push_thread_default((GMainContext*)fMainContext);
	g_main_loop_run((GMainLoop*)fMainLoop);
	g_main_context_pop_thread_default((GMainContext*)fMainContext);
}


bool
NMBackend::_InitLibNM()
{
	// Create dedicated GMainContext for NetworkManager operations
	fMainContext = g_main_context_new();
	if (fMainContext == NULL) {
		fprintf(stderr, "Failed to create GMainContext\n");
		return false;
	}
	
	// Create GMainLoop for this context
	fMainLoop = g_main_loop_new((GMainContext*)fMainContext, FALSE);
	if (fMainLoop == NULL) {
		fprintf(stderr, "Failed to create GMainLoop\n");
		_CleanupLibNM();
		return false;
	}
	
	// The dispatch thread must NOT be running yet: g_main_loop_run() acquires
	// fMainContext, and g_main_context_push_thread_default() acquires it too
	// and g_return_if_fail()s if another thread already owns it -- which would
	// silently bind the client to the (never-iterated) default context.
	g_main_context_push_thread_default((GMainContext*)fMainContext);

	// nm_client_new() has no timeout; an absent/wedged system bus would
	// otherwise freeze this thread -- Deskbar's window thread included.
	GError* error = NULL;
	BPrivate::GLibSyncTimeout guard(kInitTimeout);
	fNMClient = nm_client_new(guard.Cancellable(), &error);
	guard.Stop();

	g_main_context_pop_thread_default((GMainContext*)fMainContext);

	// NetworkManager not running (or not yet activated) is not fatal: the
	// name watcher set up below retries nm_client_new() the moment
	// org.freedesktop.NetworkManager appears on the bus. Every public
	// method already null-checks fNMClient and the UI already renders
	// kNMFieldNMAvailable == false as "NetworkManager is not running"
	// rather than an empty list -- this just makes that state recoverable
	// instead of permanent until the app restarts.
	if (fNMClient == NULL) {
		fprintf(stderr, "NM client not available yet: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
	}

	// Start pumping fMainContext regardless of whether the client came up:
	// the watcher below needs a running loop to deliver name-appeared, and
	// a later successful retry needs the dispatch thread to run on.
	fDispatchThread = spawn_thread(_DispatchThreadEntry, "nm_dispatch",
		B_NORMAL_PRIORITY, this);
	if (fDispatchThread < B_OK) {
		fprintf(stderr, "Failed to spawn dispatch thread\n");
		_CleanupLibNM();
		return false;
	}
	resume_thread(fDispatchThread);

	// Must run on the dispatch thread, not here: NMClient was created under
	// push_thread_default(fMainContext), and every g_signal_connect against
	// it or its devices needs to happen where its signals are actually
	// emitted.
	if (fNMClient != NULL) {
		g_main_context_invoke((GMainContext*)fMainContext,
			_ConnectClientSignalsSource, this);
	}

	g_main_context_invoke((GMainContext*)fMainContext, _SetupNMWatchSource,
		this);

	return true;
}


gboolean
NMBackend::_SetupNMWatchSource(gpointer cookie)
{
	((NMBackend*)cookie)->_SetupNMWatch();
	return G_SOURCE_REMOVE;
}


void
NMBackend::_SetupNMWatch()
{
	fNMWatcherId = g_bus_watch_name(G_BUS_TYPE_SYSTEM,
		"org.freedesktop.NetworkManager", G_BUS_NAME_WATCHER_FLAGS_NONE,
		_OnNMNameAppeared, _OnNMNameVanished, this, NULL);
}


void
NMBackend::_OnNMNameAppeared(GDBusConnection* connection, const char* name,
	const char* nameOwner, void* userData)
{
	((NMBackend*)userData)->_TryCreateNMClient();
}


void
NMBackend::_OnNMNameVanished(GDBusConnection* connection, const char* name,
	void* userData)
{
	((NMBackend*)userData)->_HandleNMVanished();
}


// Dispatch thread only (invoked as a g_bus_watch_name callback, which fires
// on whichever context is thread-default there -- fMainContext, see
// _DispatchThread). A no-op if a client already exists, so a redundant
// name-appeared (e.g. right after our own successful init) is harmless.
void
NMBackend::_TryCreateNMClient()
{
	if (fNMClient != NULL)
		return;

	GError* error = NULL;
	NMClient* client = nm_client_new(NULL, &error);
	if (client == NULL) {
		fprintf(stderr, "NM client retry failed: %s\n",
			error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
		return;
	}

	fNMClient = client;
	_ConnectClientSignals();

	// Replay the secret-agent registration if the NetworkStatus replicant
	// had one in place before NM vanished -- see _RegisterSecretAgent()'s
	// comment for why re-running just the AgentManager call (not the GDBus
	// object export) is what this needs.
	if (fSecretRouter.HasUIHandler())
		_RegisterSecretAgent(fSecretRouter.UIHandler());
}


// Dispatch thread only (g_bus_watch_name callback). Mirrors the client
// teardown half of _CleanupLibNM(), but leaves fMainContext/fMainLoop/the
// watcher itself alone -- only NM's own state goes away, not this backend.
void
NMBackend::_HandleNMVanished()
{
	if (fNMClient == NULL)
		return;

	fDeviceStateHandlers.clear();
	fAPAddedHandlers.clear();
	fAPRemovedHandlers.clear();
	fActiveAP = NULL;
	fActiveAPStrengthHandlerId = 0;

	g_object_unref(fNMClient);
	fNMClient = NULL;

	BMessage message((uint32)NOTIFICATION_DEVICE_REMOVED);
	{
		BAutolock lock(fLock);
		fWiFiSnapshot.clear();
		_FillDevicesMessage(NULL, &fDeviceSnapshot);
		fVPNSnapshot.MakeEmpty();
		fVPNSnapshot.AddInt32(kNMFieldVPNCount, 0);
		if (!fWatchers.empty())
			_NotifyWatchers(NOTIFICATION_DEVICE_REMOVED, message);
	}
}


void
NMBackend::_CleanupLibNM()
{
	// Best-effort: leaving a stale SecretAgent registration on the bus
	// after this process exits would make NM keep trying to call a dead
	// endpoint on every future secrets request.
	_UnregisterSecretAgent();

	// g_bus_unwatch_name() is safe from any thread and guarantees neither
	// callback fires again once it returns.
	if (fNMWatcherId != 0) {
		g_bus_unwatch_name(fNMWatcherId);
		fNMWatcherId = 0;
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


	// Cleanup NM client. Unref destroys every NMDevice/NMAccessPoint it owns
	// along with it, which drops their signal handlers too -- no explicit
	// g_signal_handler_disconnect needed. Just drop our own bookkeeping.
	fDeviceStateHandlers.clear();
	fActiveAP = NULL;
	fActiveAPStrengthHandlerId = 0;
	if (fNMClient != NULL) {
		g_object_unref(fNMClient);
		fNMClient = NULL;
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


gboolean
NMBackend::_ConnectClientSignalsSource(gpointer cookie)
{
	((NMBackend*)cookie)->_ConnectClientSignals();
	return G_SOURCE_REMOVE;
}


// Extends the type switch so bridge/bond/tun/loopback/vpn devices get a
// real label instead of the bare "continue" that used to make them
// silently vanish from the list.
static const char*
_DeviceTypeString(NMDevice* device)
{
	switch (nm_device_get_device_type(device)) {
		case NM_DEVICE_TYPE_ETHERNET:
			return "ethernet";
		case NM_DEVICE_TYPE_WIFI:
			return "wifi";
		case NM_DEVICE_TYPE_MODEM:
			return "modem";
		case NM_DEVICE_TYPE_BRIDGE:
			return "bridge";
		case NM_DEVICE_TYPE_BOND:
			return "bond";
		case NM_DEVICE_TYPE_TUN:
			return "tun";
		case NM_DEVICE_TYPE_LOOPBACK:
			return "loopback";
		case NM_DEVICE_TYPE_VLAN:
			return "vlan";
		case NM_DEVICE_TYPE_WIREGUARD:
			return "vpn";
		default:
			return "unknown";
	}
}


// Converts a dotted-quad IPv4 netmask to a CIDR prefix length, or -1 if it is
// missing or not a valid mask.
static int32
_NetmaskToPrefix(const char* netmask)
{
	if (netmask == NULL || netmask[0] == '\0')
		return -1;

	struct in_addr addr;
	if (inet_pton(AF_INET, netmask, &addr) != 1)
		return -1;

	uint32_t hostOrder = ntohl(addr.s_addr);
	int32 prefix = 0;
	while (prefix < 32 && (hostOrder & (0x80000000u >> prefix)) != 0)
		prefix++;

	// A netmask must be contiguous ones followed by zeroes; 255.0.255.0
	// parses fine as an address but is not a mask.
	if (prefix < 32 && (hostOrder << prefix) != 0)
		return -1;
	return prefix;
}


// Converts a CIDR prefix length back to a dotted-quad netmask, for seeding
// StaticIPView's Netmask field from a saved profile.
static void
_PrefixToNetmask(guint32 prefix, BString& outNetmask)
{
	struct in_addr addr;
	addr.s_addr = prefix == 0 ? 0
		: htonl(prefix >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - prefix)) - 1));

	char buffer[INET_ADDRSTRLEN];
	if (inet_ntop(AF_INET, &addr, buffer, sizeof(buffer)) != NULL)
		outNetmask = buffer;
	else
		outNetmask = "";
}


// Reads one connection's own NMSettingIPConfig into the kNMFieldIP4* fields
// -- shared by _FillIP4ConfigFields() (the active connection, for the
// device-info snapshot) and GetConnectionIP4ConfigAsync() (any saved
// profile, for StaticIPView's profile chooser). connection == NULL (no such
// setting, or no connection at all) yields method "unknown" and empty
// fields, same as a device with nothing active always has.
static void
_FillConnectionIP4Fields(NMConnection* connection, BMessage* outInfo)
{
	BString method = "unknown";
	BString address, netmask, gateway, dns;

	NMSettingIPConfig* ipSetting = connection != NULL
		? nm_connection_get_setting_ip4_config(connection) : NULL;

	if (ipSetting != NULL) {
		const char* methodStr = nm_setting_ip_config_get_method(ipSetting);
		if (methodStr != NULL)
			method = methodStr;

		if (nm_setting_ip_config_get_num_addresses(ipSetting) > 0) {
			NMIPAddress* addr = nm_setting_ip_config_get_address(ipSetting, 0);
			if (addr != NULL) {
				const char* addrStr = nm_ip_address_get_address(addr);
				if (addrStr != NULL)
					address = addrStr;
				_PrefixToNetmask(nm_ip_address_get_prefix(addr), netmask);
			}
		}

		const char* gw = nm_setting_ip_config_get_gateway(ipSetting);
		if (gw != NULL)
			gateway = gw;

		guint dnsCount = nm_setting_ip_config_get_num_dns(ipSetting);
		for (guint i = 0; i < dnsCount; i++) {
			const char* dnsAddr = nm_setting_ip_config_get_dns(ipSetting, i);
			if (dnsAddr == NULL)
				continue;
			if (!dns.IsEmpty())
				dns << ",";
			dns << dnsAddr;
		}
	}

	outInfo->AddString(kNMFieldIP4Method, method);
	outInfo->AddString(kNMFieldIP4Address, address);
	outInfo->AddString(kNMFieldIP4Netmask, netmask);
	outInfo->AddString(kNMFieldIP4Gateway, gateway);
	outInfo->AddString(kNMFieldIP4DNS, dns);
}


// Fills the Gateway/DNS rows (live runtime state, from NMDevice's
// NMIPConfig -- reflects whatever is actually in effect, DHCP or static) and
// the ip4_* fields StaticIPView seeds itself from by default (the active
// connection's configured NMSettingIPConfig, not the live lease). A device
// with no active connection gets method "unknown" and empty profile fields
// -- StaticIPView falls back to its DHCP-mode default in that case.
static void
_FillIP4ConfigFields(NMDevice* device, BMessage* outInfo)
{
	BString gateway, dns;
	NMIPConfig* ip4Config = nm_device_get_ip4_config(device);
	if (ip4Config != NULL) {
		const char* gw = nm_ip_config_get_gateway(ip4Config);
		if (gw != NULL)
			gateway = gw;

		const char* const* nameservers = nm_ip_config_get_nameservers(ip4Config);
		if (nameservers != NULL) {
			for (int i = 0; nameservers[i] != NULL; i++) {
				if (!dns.IsEmpty())
					dns << ",";
				dns << nameservers[i];
			}
		}
	}
	outInfo->AddString(kNMFieldGateway, gateway);
	outInfo->AddString(kNMFieldDNS, dns);

	NMActiveConnection* active = nm_device_get_active_connection(device);
	NMConnection* connection = active != NULL
		? NM_CONNECTION(nm_active_connection_get_connection(active)) : NULL;
	_FillConnectionIP4Fields(connection, outInfo);
}


// Fills every field the Connection Information dialog and the wired detail
// pane need. Populated from the real NMDevice, never echoed back from the
// caller's argument -- the previous "echo the argument" implementation was
// the device-identity bug (device path vs. interface name confused at
// call sites). Also what the snapshot cache stores per device, so GetDevices()
// and GetDeviceInfo() see the same fields whether they came from the cache
// or (async callers) a live query.
static void
_FillDeviceInfoMessage(NMDevice* device, BMessage* outInfo)
{
	outInfo->MakeEmpty();
	const char* path = nm_device_get_path(device);
	const char* iface = nm_device_get_iface(device);
	outInfo->AddString(kNMFieldPath, path != NULL ? path : "");
	outInfo->AddString(kNMFieldInterface, iface != NULL ? iface : "");
	outInfo->AddString(kNMFieldType, _DeviceTypeString(device));
	outInfo->AddUInt32(kNMFieldState, nm_device_get_state(device));

	const char* hwAddress = nm_device_get_hw_address(device);
	outInfo->AddString(kNMFieldHWAddress, hwAddress != NULL ? hwAddress : "");

	outInfo->AddUInt32(kNMFieldMTU, nm_device_get_mtu(device));

	const char* driver = nm_device_get_driver(device);
	outInfo->AddString(kNMFieldDriver, driver != NULL ? driver : "");

	outInfo->AddBool(kNMFieldManaged, nm_device_get_managed(device) != FALSE);

	_FillIP4ConfigFields(device, outInfo);
}


static NMDevice*
_FindDeviceByPath(NMClient* nmClient, const char* devicePath)
{
	if (nmClient == NULL || devicePath == NULL)
		return NULL;

	const GPtrArray* devices = nm_client_get_devices(nmClient);
	if (devices == NULL)
		return NULL;

	for (guint i = 0; i < devices->len; i++) {
		NMDevice* device = (NMDevice*)g_ptr_array_index(devices, i);
		if (device == NULL)
			continue;
		// nm_device_get_path() returns NULL for a device NM has not finished
		// exporting on the bus yet -- strcmp() against NULL is a segfault,
		// not a "no match".
		const char* path = nm_device_get_path(device);
		if (path != NULL && strcmp(path, devicePath) == 0)
			return device;
	}

	return NULL;
}


// Shared by the sync and async paths and by the snapshot cache, so there is
// exactly one place that walks NMClient's device list. Must run on the
// dispatch thread: it reads NMClient's cached device list, which is mutated
// by D-Bus signal handlers running there with no locking of its own.
static void
_FillDevicesMessage(NMClient* nmClient, BMessage* outMessage)
{
	outMessage->MakeEmpty();

	// Lets every caller tell "NetworkManager is not running/reachable" apart
	// from "reachable but genuinely has no devices" -- both look like an
	// empty device_count otherwise, and the two must be distinguishable.
	outMessage->AddBool(kNMFieldNMAvailable, nmClient != NULL);

	if (nmClient == NULL) {
		outMessage->AddInt32(kNMFieldDeviceCount, 0);
		return;
	}

	const GPtrArray* devices = nm_client_get_devices(nmClient);
	if (devices == NULL) {
		outMessage->AddInt32(kNMFieldDeviceCount, 0);
		return;
	}

	int32 count = 0;
	for (guint i = 0; i < devices->len; i++) {
		NMDevice* device = (NMDevice*)g_ptr_array_index(devices, i);
		if (device == NULL)
			continue;

		// A device mid-add on the bus can have no path yet; skip it rather
		// than publish an entry no later lookup by path can ever match.
		if (nm_device_get_path(device) == NULL)
			continue;

		char deviceName[32];
		snprintf(deviceName, sizeof(deviceName), "device_%" B_PRId32, count);

		BMessage deviceInfo;
		_FillDeviceInfoMessage(device, &deviceInfo);
		outMessage->AddMessage(deviceName, &deviceInfo);
		count++;
	}

	outMessage->AddInt32(kNMFieldDeviceCount, count);
}


// #pragma mark - Signal wiring / snapshot cache


// Defined in the VPN section below; forward-declared so
// _RefreshSnapshotAndNotify()/_ConnectClientSignals() can refresh the VPN
// snapshot alongside the device snapshot on every relevant event.
static void _FillVPNMessage(NMClient* nmClient, BMessage* outMessage);


// Mirrors BlueZBackend::_NotifyWatchers exactly. Caller must hold fLock.
void
NMBackend::_NotifyWatchers(uint32 type, BMessage& message)
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


// Single choke point every signal handler below goes through: refresh the
// cache from NMClient's live state, then notify. Both must happen under the
// same fLock acquisition so a watcher that reacts to the notification by
// calling GetDevices() synchronously never observes a stale snapshot.
void
NMBackend::_RefreshSnapshotAndNotify(uint32 type, BMessage& message)
{
	BAutolock lock(fLock);
	_FillDevicesMessage((NMClient*)fNMClient, &fDeviceSnapshot);
	_FillVPNMessage((NMClient*)fNMClient, &fVPNSnapshot);
	if (!fWatchers.empty())
		_NotifyWatchers(type, message);
}


void
NMBackend::_HandleDeviceAdded(void* deviceRaw)
{
	NMDevice* device = (NMDevice*)deviceRaw;
	const char* path = nm_device_get_path(device);

	// A device with no path yet cannot be tracked by path; it will pick up
	// state-change coverage once _RefreshSnapshotAndNotify catches it with a
	// path on a later signal (e.g. its own notify::state).
	if (path != NULL) {
		gulong id = g_signal_connect(device, "notify::state",
			G_CALLBACK(_OnDeviceStateNotify), this);
		fDeviceStateHandlers[path] = id;

		if (nm_device_get_device_type(device) == NM_DEVICE_TYPE_WIFI) {
			gulong addedId = g_signal_connect(device, "access-point-added",
				G_CALLBACK(_OnAccessPointAdded), this);
			gulong removedId = g_signal_connect(device, "access-point-removed",
				G_CALLBACK(_OnAccessPointRemoved), this);
			fAPAddedHandlers[path] = addedId;
			fAPRemovedHandlers[path] = removedId;
			_RefreshWiFiSnapshot(device, path);
		}
	}

	_UpdateActiveAPWatch();

	BMessage message((uint32)NOTIFICATION_DEVICE_ADDED);
	if (path != NULL)
		message.AddString(kNMFieldPath, path);
	_RefreshSnapshotAndNotify(NOTIFICATION_DEVICE_ADDED, message);
}


void
NMBackend::_HandleDeviceRemoved(void* deviceRaw)
{
	NMDevice* device = (NMDevice*)deviceRaw;
	const char* path = nm_device_get_path(device);

	if (path != NULL) {
		std::map<BString, gulong>::iterator it = fDeviceStateHandlers.find(path);
		if (it != fDeviceStateHandlers.end()) {
			g_signal_handler_disconnect(device, it->second);
			fDeviceStateHandlers.erase(it);
		}

		std::map<BString, gulong>::iterator addedIt = fAPAddedHandlers.find(path);
		if (addedIt != fAPAddedHandlers.end())
			fAPAddedHandlers.erase(addedIt);
		std::map<BString, gulong>::iterator removedIt
			= fAPRemovedHandlers.find(path);
		if (removedIt != fAPRemovedHandlers.end())
			fAPRemovedHandlers.erase(removedIt);

		BAutolock lock(fLock);
		fWiFiSnapshot.erase(path);
	}

	_UpdateActiveAPWatch();

	BMessage message((uint32)NOTIFICATION_DEVICE_REMOVED);
	if (path != NULL)
		message.AddString(kNMFieldPath, path);
	_RefreshSnapshotAndNotify(NOTIFICATION_DEVICE_REMOVED, message);
}


void
NMBackend::_HandleDeviceStateChanged(void* deviceRaw)
{
	NMDevice* device = (NMDevice*)deviceRaw;
	const char* path = nm_device_get_path(device);

	BMessage message((uint32)NOTIFICATION_DEVICE_STATE_CHANGED);
	if (path != NULL)
		message.AddString(kNMFieldPath, path);
	message.AddUInt32(kNMFieldState, nm_device_get_state(device));
	_RefreshSnapshotAndNotify(NOTIFICATION_DEVICE_STATE_CHANGED, message);
}


void
NMBackend::_HandleActiveConnectionChanged()
{
	_UpdateActiveAPWatch();

	BMessage message((uint32)NOTIFICATION_CONNECTION_STATUS_CHANGED);
	_RefreshSnapshotAndNotify(NOTIFICATION_CONNECTION_STATUS_CHANGED, message);
}


void
NMBackend::_HandleAPStrengthChanged(void* apRaw)
{
	NMAccessPoint* ap = (NMAccessPoint*)apRaw;

	BMessage message((uint32)NOTIFICATION_SIGNAL_STRENGTH_CHANGED);
	message.AddUInt8(kNMFieldAPStrength, nm_access_point_get_strength(ap));
	_RefreshSnapshotAndNotify(NOTIFICATION_SIGNAL_STRENGTH_CHANGED, message);
}


// Finds the first Wi-Fi device's active access point and moves the
// notify::strength subscription there, disconnecting from whichever AP (if
// any) held it before. Called after device-added/removed and after
// notify::active-connection -- any of those can change which AP is "the
// active one". Dispatch thread only.
void
NMBackend::_UpdateActiveAPWatch()
{
	NMAccessPoint* newAP = NULL;

	const GPtrArray* devices = nm_client_get_devices((NMClient*)fNMClient);
	if (devices != NULL) {
		for (guint i = 0; i < devices->len; i++) {
			NMDevice* device = (NMDevice*)g_ptr_array_index(devices, i);
			if (device == NULL
				|| nm_device_get_device_type(device) != NM_DEVICE_TYPE_WIFI) {
				continue;
			}

			NMAccessPoint* ap = nm_device_wifi_get_active_access_point(
				(NMDeviceWifi*)device);
			if (ap != NULL) {
				newAP = ap;
				break;
			}
		}
	}

	if ((void*)newAP == fActiveAP)
		return;

	if (fActiveAP != NULL && fActiveAPStrengthHandlerId != 0) {
		g_signal_handler_disconnect(fActiveAP, fActiveAPStrengthHandlerId);
		fActiveAPStrengthHandlerId = 0;
	}

	fActiveAP = newAP;

	if (fActiveAP != NULL) {
		fActiveAPStrengthHandlerId = g_signal_connect(fActiveAP,
			"notify::strength", G_CALLBACK(_OnActiveAPStrengthNotify), this);
	}
}


void
NMBackend::_OnDeviceAdded(void* client, void* device, void* userData)
{
	((NMBackend*)userData)->_HandleDeviceAdded(device);
}


void
NMBackend::_OnDeviceRemoved(void* client, void* device, void* userData)
{
	((NMBackend*)userData)->_HandleDeviceRemoved(device);
}


void
NMBackend::_OnDeviceStateNotify(GObject* device, GParamSpec* pspec,
	void* userData)
{
	((NMBackend*)userData)->_HandleDeviceStateChanged(device);
}


void
NMBackend::_OnActiveConnectionNotify(GObject* client, GParamSpec* pspec,
	void* userData)
{
	((NMBackend*)userData)->_HandleActiveConnectionChanged();
}


void
NMBackend::_OnActiveAPStrengthNotify(GObject* ap, GParamSpec* pspec,
	void* userData)
{
	((NMBackend*)userData)->_HandleAPStrengthChanged(ap);
}


// Connects every signal StartWatching's callers care about, and does the
// initial snapshot fill so GetDevices()/GetDeviceInfo() have something
// correct to return even before the first signal fires. Runs once, from the
// dispatch thread, shortly after the client is ready -- not gated on any
// watcher being registered yet, unlike BlueZ's lazy per-watcher
// subscription: NMClient's device-added/removed/notify::state are cheap to
// keep connected for the life of the process and the snapshot needs them
// regardless of whether anyone is watching.
void
NMBackend::_ConnectClientSignals()
{
	if (fNMClient == NULL)
		return;

	NMClient* client = (NMClient*)fNMClient;

	fDeviceAddedHandlerId = g_signal_connect(client, "device-added",
		G_CALLBACK(_OnDeviceAdded), this);
	fDeviceRemovedHandlerId = g_signal_connect(client, "device-removed",
		G_CALLBACK(_OnDeviceRemoved), this);
	// This libnm has no singular "active-connection" property -- only the
	// list property below (NM_CLIENT_ACTIVE_CONNECTIONS) and the
	// active-connection-added/removed signals, which fire for the same
	// underlying change.
	fActiveConnectionHandlerId = g_signal_connect(client,
		"notify::" NM_CLIENT_ACTIVE_CONNECTIONS,
		G_CALLBACK(_OnActiveConnectionNotify), this);

	const GPtrArray* devices = nm_client_get_devices(client);
	if (devices != NULL) {
		for (guint i = 0; i < devices->len; i++) {
			NMDevice* device = (NMDevice*)g_ptr_array_index(devices, i);
			if (device == NULL)
				continue;
			const char* path = nm_device_get_path(device);
			if (path == NULL)
				continue;
			gulong id = g_signal_connect(device, "notify::state",
				G_CALLBACK(_OnDeviceStateNotify), this);
			fDeviceStateHandlers[path] = id;

			if (nm_device_get_device_type(device) == NM_DEVICE_TYPE_WIFI) {
				gulong addedId = g_signal_connect(device, "access-point-added",
					G_CALLBACK(_OnAccessPointAdded), this);
				gulong removedId = g_signal_connect(device,
					"access-point-removed", G_CALLBACK(_OnAccessPointRemoved),
					this);
				fAPAddedHandlers[path] = addedId;
				fAPRemovedHandlers[path] = removedId;
				_RefreshWiFiSnapshot(device, path);
			}
		}
	}

	_UpdateActiveAPWatch();

	BAutolock lock(fLock);
	_FillDevicesMessage(client, &fDeviceSnapshot);
	_FillVPNMessage(client, &fVPNSnapshot);
}


// #pragma mark - _ResolveDevicePath (name -> D-Bus object path)


// Reads the snapshot cache under fLock instead of hopping to the dispatch
// thread and blocking on a semaphore -- see the "3.1b snapshot cache" note
// on GetDevices() below for why that rendezvous is gone.
status_t
NMBackend::_ResolveDevicePath(const char* interfaceName, BString& outPath)
{
	if (interfaceName == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL)
		return B_ERROR;

	BAutolock lock(fLock);

	int32 count = 0;
	fDeviceSnapshot.FindInt32(kNMFieldDeviceCount, &count);
	for (int32 i = 0; i < count; i++) {
		char deviceName[32];
		snprintf(deviceName, sizeof(deviceName), "device_%" B_PRId32, i);

		BMessage device;
		if (fDeviceSnapshot.FindMessage(deviceName, &device) != B_OK)
			continue;

		BString iface;
		if (device.FindString(kNMFieldInterface, &iface) == B_OK
				&& iface == interfaceName) {
			return device.FindString(kNMFieldPath, &outPath);
		}
	}

	outPath.Truncate(0);
	return B_ENTRY_NOT_FOUND;
}


// #pragma mark - GetDevicesAsync (the path every UI surface must use)


struct _GetDevicesCookie {
	NMClient* nmClient;
};


static void
_RunGetDevicesAsync(void* cookie, BMessage* reply)
{
	_GetDevicesCookie* job = (_GetDevicesCookie*)cookie;
	_FillDevicesMessage(job->nmClient, reply);
	delete job;
}


status_t
NMBackend::GetDevicesAsync(const BMessenger& replyTo, uint32 replyWhat)
{
	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_GetDevicesCookie* cookie = new _GetDevicesCookie;
	cookie->nmClient = (NMClient*)fNMClient;

	return _RunOnDispatchThread(_RunGetDevicesAsync, cookie, replyTo,
		replyWhat);
}


// #pragma mark - GetDevices (synchronous; non-UI callers only, see header)


status_t
NMBackend::GetDevices(BMessage* outDevices)
{
	if (outDevices == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL)
		return B_ERROR;

	// No dispatch-thread round trip and no raw semaphore: the dispatch
	// thread keeps fDeviceSnapshot current from NMClient's signals (see
	// _RefreshSnapshot below), so a synchronous caller just copies it under
	// fLock. This is the only reason BNetworkRoster's synchronous contract
	// (NetworkRoster.cpp) can stay synchronous without blocking on the
	// dispatch thread on every call -- UI call sites still use
	// GetDevicesAsync() and never reach here.
	BAutolock lock(fLock);
	*outDevices = fDeviceSnapshot;
	return B_OK;
}


status_t
NMBackend::GetDeviceInfo(const char* devicePath, BMessage* outInfo)
{
	if (devicePath == NULL || outInfo == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL)
		return B_ERROR;

	BAutolock lock(fLock);

	int32 count = 0;
	fDeviceSnapshot.FindInt32(kNMFieldDeviceCount, &count);
	for (int32 i = 0; i < count; i++) {
		char deviceName[32];
		snprintf(deviceName, sizeof(deviceName), "device_%" B_PRId32, i);

		BMessage device;
		if (fDeviceSnapshot.FindMessage(deviceName, &device) != B_OK)
			continue;

		BString path;
		if (device.FindString(kNMFieldPath, &path) == B_OK
				&& path == devicePath) {
			*outInfo = device;
			return B_OK;
		}
	}

	outInfo->MakeEmpty();
	return B_ENTRY_NOT_FOUND;
}


struct _GetDeviceInfoCookie {
	NMClient* nmClient;
	BString devicePath;
};


static void
_RunGetDeviceInfoAsync(void* cookie, BMessage* reply)
{
	_GetDeviceInfoCookie* job = (_GetDeviceInfoCookie*)cookie;
	NMDevice* device = _FindDeviceByPath(job->nmClient,
		job->devicePath.String());
	if (device != NULL)
		_FillDeviceInfoMessage(device, reply);
	else
		reply->AddString(kNMFieldPath, job->devicePath.String());
	delete job;
}


status_t
NMBackend::GetDeviceInfoAsync(const char* devicePath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_GetDeviceInfoCookie* cookie = new _GetDeviceInfoCookie;
	cookie->nmClient = (NMClient*)fNMClient;
	cookie->devicePath = devicePath;

	return _RunOnDispatchThread(_RunGetDeviceInfoAsync, cookie, replyTo,
		replyWhat);
}


// #pragma mark - ConnectDevice / DisconnectDevice


struct _ConnectDeviceJob {
	NMBackend* backend;
	NMClient* nmClient;
	BString devicePath;
};


static void
_OnActivateConnectionDone(GObject* source, GAsyncResult* result,
	gpointer userData)
{
	_ConnectDeviceJob* job = (_ConnectDeviceJob*)userData;

	GError* error = NULL;
	NMActiveConnection* active = nm_client_activate_connection_finish(
		job->nmClient, result, &error);

	BMessage message((uint32)NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
	message.AddString(kNMFieldPath, job->devicePath);
	if (active != NULL) {
		g_object_unref(active);
	} else {
		message.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
	}

	job->backend->_RefreshSnapshotAndNotify(
		NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED, message);
	delete job;
}


static gboolean
_RunConnectDevice(gpointer data)
{
	_ConnectDeviceJob* job = (_ConnectDeviceJob*)data;

	NMDevice* device = _FindDeviceByPath(job->nmClient, job->devicePath.String());
	if (device == NULL) {
		BMessage message((uint32)NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
		message.AddString(kNMFieldPath, job->devicePath);
		message.AddString("reason", "no such device");
		job->backend->_RefreshSnapshotAndNotify(
			NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED, message);
		delete job;
		return G_SOURCE_REMOVE;
	}

	// NULL connection + NULL specific_object: let NM pick the best saved
	// profile for this device (its most-recently-used one), matching what a
	// plain "Connect" click means. A device with no saved profile at all
	// fails here with a real NM error message, which is exactly the honest
	// outcome wanted -- not a fabricated success.
	nm_client_activate_connection_async(job->nmClient, NULL, device, NULL,
		NULL, _OnActivateConnectionDone, job);
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::ConnectDevice(const char* devicePath)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_ConnectDeviceJob* job = new _ConnectDeviceJob;
	job->backend = this;
	job->nmClient = (NMClient*)fNMClient;
	job->devicePath = devicePath;

	g_main_context_invoke((GMainContext*)fMainContext, _RunConnectDevice, job);
	return B_OK;
}


static void
_OnDisconnectDone(GObject* sourceDevice, GAsyncResult* result,
	gpointer userData)
{
	_ConnectDeviceJob* job = (_ConnectDeviceJob*)userData;

	GError* error = NULL;
	gboolean ok = nm_device_disconnect_finish((NMDevice*)sourceDevice, result,
		&error);

	BMessage message((uint32)NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
	message.AddString(kNMFieldPath, job->devicePath);
	if (!ok) {
		message.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
	}

	job->backend->_RefreshSnapshotAndNotify(
		NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED, message);
	delete job;
}


static gboolean
_RunDisconnectDevice(gpointer data)
{
	_ConnectDeviceJob* job = (_ConnectDeviceJob*)data;

	NMDevice* device = _FindDeviceByPath(job->nmClient, job->devicePath.String());
	if (device == NULL) {
		BMessage message((uint32)NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
		message.AddString(kNMFieldPath, job->devicePath);
		message.AddString("reason", "no such device");
		job->backend->_RefreshSnapshotAndNotify(
			NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED, message);
		delete job;
		return G_SOURCE_REMOVE;
	}

	nm_device_disconnect_async(device, NULL, _OnDisconnectDone, job);
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::DisconnectDevice(const char* devicePath)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_ConnectDeviceJob* job = new _ConnectDeviceJob;
	job->backend = this;
	job->nmClient = (NMClient*)fNMClient;
	job->devicePath = devicePath;

	g_main_context_invoke((GMainContext*)fMainContext, _RunDisconnectDevice,
		job);
	return B_OK;
}


// #pragma mark - WiFi scanning / AP snapshot


// Determines the security label the same way nm-applet does: RSN present
// means WPA2/WPA3, WPA-only means WPA, PRIVACY with neither means WEP.
static bool
_APIsSecured(NMAccessPoint* ap)
{
	NM80211ApSecurityFlags wpaFlags = nm_access_point_get_wpa_flags(ap);
	NM80211ApSecurityFlags rsnFlags = nm_access_point_get_rsn_flags(ap);
	NM80211ApFlags flags = nm_access_point_get_flags(ap);

	if (wpaFlags != NM_802_11_AP_SEC_NONE || rsnFlags != NM_802_11_AP_SEC_NONE)
		return true;
	return (flags & NM_802_11_AP_FLAGS_PRIVACY) != 0;
}


// Builds the kNMFieldAP* message for one NMDeviceWifi's currently-cached AP
// list. Dispatch thread only -- reads the GObject-owned AP list libnm keeps
// current from D-Bus PropertiesChanged, the same object nm_device_wifi_get_
// access_points() would race against from any other thread.
static void
_FillWiFiMessage(NMDeviceWifi* wifiDevice, BMessage* outMessage)
{
	outMessage->MakeEmpty();

	if (wifiDevice == NULL) {
		outMessage->AddInt32(kNMFieldAPCount, 0);
		return;
	}

	NMAccessPoint* activeAP = nm_device_wifi_get_active_access_point(wifiDevice);
	const char* activePath = activeAP != NULL
		? nm_object_get_path(NM_OBJECT(activeAP)) : NULL;

	const GPtrArray* aps = nm_device_wifi_get_access_points(wifiDevice);
	int32 count = 0;

	// Dedupe by SSID, keeping the strongest AP per SSID -- upstream's
	// GNOME/nm-applet behaviour and what WirelessNetworkMenuItem's one-row-
	// per-network shape expects; NM itself lists one AP object per BSSID; a
	// network with several access points would otherwise show duplicate rows.
	std::map<BString, BMessage> bySSID;
	std::map<BString, int32> strengthBySSID;

	if (aps != NULL) {
		for (guint i = 0; i < aps->len; i++) {
			NMAccessPoint* ap = (NMAccessPoint*)g_ptr_array_index(aps, i);
			if (ap == NULL)
				continue;

			// A GBytes SSID can be NULL (hidden network still being probed)
			// or zero-length; either way there is nothing to show a user.
			GBytes* ssidBytes = nm_access_point_get_ssid(ap);
			if (ssidBytes == NULL)
				continue;

			gsize len = 0;
			gconstpointer data = g_bytes_get_data(ssidBytes, &len);
			if (data == NULL || len == 0)
				continue;

			BString ssid((const char*)data, len);
			if (ssid.IsEmpty())
				continue;

			int32 strength = nm_access_point_get_strength(ap);

			std::map<BString, int32>::iterator it = strengthBySSID.find(ssid);
			if (it != strengthBySSID.end() && it->second >= strength)
				continue;
			strengthBySSID[ssid] = strength;

			const char* path = nm_object_get_path(NM_OBJECT(ap));

			BMessage apInfo;
			apInfo.AddString(kNMFieldAPSSID, ssid);
			apInfo.AddInt32(kNMFieldAPStrength, strength);
			apInfo.AddBool(kNMFieldAPSecured, _APIsSecured(ap));
			apInfo.AddBool(kNMFieldAPConnected,
				path != NULL && activePath != NULL
					&& strcmp(path, activePath) == 0);
			bySSID[ssid] = apInfo;
		}
	}

	for (std::map<BString, BMessage>::iterator it = bySSID.begin();
			it != bySSID.end(); ++it) {
		char apName[32];
		snprintf(apName, sizeof(apName), "ap_%" B_PRId32, count);
		outMessage->AddMessage(apName, &it->second);
		count++;
	}

	outMessage->AddInt32(kNMFieldAPCount, count);
}


void
NMBackend::_RefreshWiFiSnapshot(void* wifiDeviceRaw, const BString& devicePath)
{
	NMDeviceWifi* wifiDevice = (NMDeviceWifi*)wifiDeviceRaw;

	BMessage snapshot;
	_FillWiFiMessage(wifiDevice, &snapshot);

	BAutolock lock(fLock);
	fWiFiSnapshot[devicePath] = snapshot;
}


void
NMBackend::_HandleAccessPointsChanged(void* wifiDeviceRaw,
	const BString& devicePath)
{
	_RefreshWiFiSnapshot(wifiDeviceRaw, devicePath);

	BMessage message((uint32)NOTIFICATION_WIFI_NETWORK_FOUND);
	message.AddString(kNMFieldPath, devicePath);
	BAutolock lock(fLock);
	if (!fWatchers.empty())
		_NotifyWatchers(NOTIFICATION_WIFI_NETWORK_FOUND, message);
}


void
NMBackend::_OnAccessPointAdded(void* wifiDevice, void* ap, void* userData)
{
	NMBackend* backend = (NMBackend*)userData;
	const char* path = nm_device_get_path((NMDevice*)wifiDevice);
	if (path != NULL)
		backend->_HandleAccessPointsChanged(wifiDevice, path);
}


void
NMBackend::_OnAccessPointRemoved(void* wifiDevice, void* ap, void* userData)
{
	NMBackend* backend = (NMBackend*)userData;
	const char* path = nm_device_get_path((NMDevice*)wifiDevice);
	if (path != NULL)
		backend->_HandleAccessPointsChanged(wifiDevice, path);
}




status_t
NMBackend::ScanWiFiNetworks(const char* devicePath, BMessage* outNetworks)
{
	if (devicePath == NULL || outNetworks == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL)
		return B_ERROR;

	{
		BAutolock lock(fLock);
		std::map<BString, BMessage>::iterator it
			= fWiFiSnapshot.find(devicePath);
		if (it != fWiFiSnapshot.end()) {
			*outNetworks = it->second;
		} else {
			outNetworks->MakeEmpty();
			outNetworks->AddInt32(kNMFieldAPCount, 0);
		}
	}

	// Kick a fresh scan in the background so a repeated menu-open sees
	// current results; never wait for it here.
	if (fMainContext != NULL) {
		BString path(devicePath);
		g_main_context_invoke((GMainContext*)fMainContext,
			[](gpointer data) -> gboolean {
				BString* pathCopy = (BString*)data;
				NMBackend* backend = NMBackend::Instance();
				NMDevice* device = backend != NULL
					? _FindDeviceByPath((NMClient*)backend->fNMClient,
						pathCopy->String())
					: NULL;
				if (device != NULL
						&& nm_device_get_device_type(device) == NM_DEVICE_TYPE_WIFI) {
					nm_device_wifi_request_scan_async((NMDeviceWifi*)device,
						NULL, NULL, NULL);
				}
				delete pathCopy;
				return G_SOURCE_REMOVE;
			}, new BString(path));
	}

	return B_OK;
}


// #pragma mark - ConnectToWiFi (sync fire-and-forget) / ForgetWiFiNetwork


status_t
NMBackend::ConnectToWiFi(const char* devicePath, const char* ssid,
	const char* password, const char* security)
{
	if (devicePath == NULL || ssid == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL)
		return B_ERROR;

	// Fire-and-forget wrapper around the same async path used everywhere
	// else -- BNetworkDevice::JoinNetwork() has no reply channel of its own.
	return ConnectToWiFiAsync(devicePath, ssid, password, security, true,
		BMessenger(), 0);
}


struct _ForgetWiFiJob {
	NMClient* nmClient;
	BString ssid;
};


static gboolean
_RunForgetWiFiNetwork(gpointer data)
{
	_ForgetWiFiJob* job = (_ForgetWiFiJob*)data;

	const GPtrArray* connections = nm_client_get_connections(job->nmClient);
	if (connections != NULL) {
		for (guint i = 0; i < connections->len; i++) {
			NMConnection* connection
				= (NMConnection*)g_ptr_array_index(connections, i);
			if (connection == NULL)
				continue;

			NMSettingWireless* wireless
				= nm_connection_get_setting_wireless(connection);
			if (wireless == NULL)
				continue;

			GBytes* ssidBytes = nm_setting_wireless_get_ssid(wireless);
			if (ssidBytes == NULL)
				continue;

			gsize len = 0;
			gconstpointer bytes = g_bytes_get_data(ssidBytes, &len);
			if (bytes == NULL || len == 0)
				continue;

			BString ssid((const char*)bytes, len);
			if (ssid != job->ssid)
				continue;

			// NM_IS_REMOTE_CONNECTION guards connections libnm surfaces that
			// are not yet backed by a saved profile on the bus (transient).
			if (NM_IS_REMOTE_CONNECTION(connection)) {
				nm_remote_connection_delete_async(
					NM_REMOTE_CONNECTION(connection), NULL, NULL, NULL);
			}
		}
	}

	delete job;
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::ForgetWiFiNetwork(const char* ssid)
{
	if (ssid == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_ForgetWiFiJob* job = new _ForgetWiFiJob;
	job->nmClient = (NMClient*)fNMClient;
	job->ssid = ssid;

	g_main_context_invoke((GMainContext*)fMainContext, _RunForgetWiFiNetwork,
		job);
	return B_OK;
}


// #pragma mark - Saved-network management


// Dispatch thread only -- walks NMClient's connection list, GObject-owned
// and mutated from D-Bus signals delivered there.
static void
_FillSavedWiFiMessage(NMClient* nmClient, BMessage* outMessage)
{
	outMessage->MakeEmpty();

	if (nmClient == NULL) {
		outMessage->AddInt32(kNMFieldSavedCount, 0);
		return;
	}

	const GPtrArray* connections = nm_client_get_connections(nmClient);
	int32 count = 0;

	if (connections != NULL) {
		for (guint i = 0; i < connections->len; i++) {
			NMConnection* connection
				= (NMConnection*)g_ptr_array_index(connections, i);
			if (connection == NULL)
				continue;

			NMSettingWireless* wireless
				= nm_connection_get_setting_wireless(connection);
			if (wireless == NULL)
				continue;

			GBytes* ssidBytes = nm_setting_wireless_get_ssid(wireless);
			gsize len = 0;
			gconstpointer bytes = ssidBytes != NULL
				? g_bytes_get_data(ssidBytes, &len) : NULL;
			if (bytes == NULL || len == 0)
				continue;

			const char* path = nm_connection_get_path(connection);
			if (path == NULL)
				continue;

			NMSettingConnection* connSetting
				= nm_connection_get_setting_connection(connection);

			BMessage savedInfo;
			savedInfo.AddString(kNMFieldSavedSSID,
				BString((const char*)bytes, len));
			savedInfo.AddString(kNMFieldSavedPath, path);
			savedInfo.AddBool(kNMFieldSavedAutoconnect, connSetting != NULL
				? nm_setting_connection_get_autoconnect(connSetting) : true);
			savedInfo.AddInt32(kNMFieldSavedPriority, connSetting != NULL
				? nm_setting_connection_get_autoconnect_priority(connSetting)
				: 0);

			char savedName[32];
			snprintf(savedName, sizeof(savedName), "saved_%" B_PRId32, count);
			outMessage->AddMessage(savedName, &savedInfo);
			count++;
		}
	}

	outMessage->AddInt32(kNMFieldSavedCount, count);
}


struct _GetSavedWiFiCookie {
	NMClient* nmClient;
};


static void
_RunGetSavedWiFiAsync(void* cookie, BMessage* reply)
{
	_GetSavedWiFiCookie* job = (_GetSavedWiFiCookie*)cookie;
	_FillSavedWiFiMessage(job->nmClient, reply);
	delete job;
}


status_t
NMBackend::GetSavedWiFiNetworksAsync(const BMessenger& replyTo,
	uint32 replyWhat)
{
	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_GetSavedWiFiCookie* cookie = new _GetSavedWiFiCookie;
	cookie->nmClient = (NMClient*)fNMClient;

	return _RunOnDispatchThread(_RunGetSavedWiFiAsync, cookie, replyTo,
		replyWhat);
}


// #pragma mark - GetDeviceConnectionProfilesAsync / GetConnectionIP4ConfigAsync


// Dispatch thread only -- nm_device_filter_connections() walks NMClient's
// connection list against the device's own compatibility check (interface
// name / MAC / connection type), the same list nm_client_get_connections()
// exposes, so no extra signal wiring is needed beyond what already keeps
// that list current.
static void
_FillDeviceConnectionProfilesMessage(NMClient* nmClient,
	const char* devicePath, BMessage* outMessage)
{
	outMessage->MakeEmpty();
	outMessage->AddString(kNMFieldPath, devicePath != NULL ? devicePath : "");

	NMDevice* device = _FindDeviceByPath(nmClient, devicePath);
	if (device == NULL) {
		outMessage->AddInt32(kNMFieldProfileCount, 0);
		return;
	}

	const GPtrArray* allConnections = nm_client_get_connections(nmClient);
	GPtrArray* applicable = allConnections != NULL
		? nm_device_filter_connections(device, allConnections) : NULL;

	NMActiveConnection* active = nm_device_get_active_connection(device);
	NMRemoteConnection* activeConnection = active != NULL
		? nm_active_connection_get_connection(active) : NULL;
	const char* activePath = activeConnection != NULL
		? nm_connection_get_path(NM_CONNECTION(activeConnection)) : NULL;

	int32 count = 0;
	if (applicable != NULL) {
		for (guint i = 0; i < applicable->len; i++) {
			NMConnection* connection
				= (NMConnection*)g_ptr_array_index(applicable, i);
			if (connection == NULL)
				continue;

			const char* path = nm_connection_get_path(connection);
			if (path == NULL)
				continue;

			const char* id = nm_connection_get_id(connection);

			BMessage profileInfo;
			profileInfo.AddString(kNMFieldProfileID, id != NULL ? id : "");
			profileInfo.AddString(kNMFieldProfilePath, path);
			profileInfo.AddBool(kNMFieldProfileActive,
				activePath != NULL && strcmp(path, activePath) == 0);

			char profileName[32];
			snprintf(profileName, sizeof(profileName), "profile_%" B_PRId32,
				count);
			outMessage->AddMessage(profileName, &profileInfo);
			count++;
		}
		g_ptr_array_unref(applicable);
	}

	outMessage->AddInt32(kNMFieldProfileCount, count);
}


struct _GetProfilesCookie {
	NMClient* nmClient;
	BString devicePath;
};


static void
_RunGetDeviceConnectionProfilesAsync(void* cookie, BMessage* reply)
{
	_GetProfilesCookie* job = (_GetProfilesCookie*)cookie;
	_FillDeviceConnectionProfilesMessage(job->nmClient,
		job->devicePath.String(), reply);
	delete job;
}


status_t
NMBackend::GetDeviceConnectionProfilesAsync(const char* devicePath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_GetProfilesCookie* cookie = new _GetProfilesCookie;
	cookie->nmClient = (NMClient*)fNMClient;
	cookie->devicePath = devicePath;

	return _RunOnDispatchThread(_RunGetDeviceConnectionProfilesAsync, cookie,
		replyTo, replyWhat);
}


struct _GetConnectionIP4Cookie {
	NMClient* nmClient;
	BString connectionPath;
};


static void
_RunGetConnectionIP4ConfigAsync(void* cookie, BMessage* reply)
{
	_GetConnectionIP4Cookie* job = (_GetConnectionIP4Cookie*)cookie;

	reply->AddString(kNMFieldPath, job->connectionPath);

	NMConnection* connection = (NMConnection*)nm_client_get_connection_by_path(
		job->nmClient, job->connectionPath.String());
	if (connection == NULL) {
		reply->AddInt32("status", (int32)B_ENTRY_NOT_FOUND);
		delete job;
		return;
	}

	_FillConnectionIP4Fields(connection, reply);
	reply->AddInt32("status", (int32)B_OK);
	delete job;
}


status_t
NMBackend::GetConnectionIP4ConfigAsync(const char* connectionPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (connectionPath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_GetConnectionIP4Cookie* cookie = new _GetConnectionIP4Cookie;
	cookie->nmClient = (NMClient*)fNMClient;
	cookie->connectionPath = connectionPath;

	return _RunOnDispatchThread(_RunGetConnectionIP4ConfigAsync, cookie,
		replyTo, replyWhat);
}


struct _ForgetSavedJob {
	NMClient* nmClient;
	BString connectionPath;
	BMessenger replyTo;
	uint32 replyWhat;
};


static void
_OnForgetSavedDone(GObject* source, GAsyncResult* result, gpointer userData)
{
	_ForgetSavedJob* job = (_ForgetSavedJob*)userData;

	GError* error = NULL;
	gboolean ok = nm_remote_connection_delete_finish(
		NM_REMOTE_CONNECTION(source), result, &error);

	BMessage reply(job->replyWhat);
	reply.AddInt32("status", ok ? (int32)B_OK : (int32)B_ERROR);
	if (!ok) {
		reply.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
	}
	job->replyTo.SendMessage(&reply);
	delete job;
}


static gboolean
_RunForgetSavedNetwork(gpointer data)
{
	_ForgetSavedJob* job = (_ForgetSavedJob*)data;

	NMConnection* connection = (NMConnection*)nm_client_get_connection_by_path(
		job->nmClient, job->connectionPath.String());
	if (connection == NULL || !NM_IS_REMOTE_CONNECTION(connection)) {
		BMessage reply(job->replyWhat);
		reply.AddInt32("status", (int32)B_ENTRY_NOT_FOUND);
		reply.AddString("reason", "no such saved connection");
		job->replyTo.SendMessage(&reply);
		delete job;
		return G_SOURCE_REMOVE;
	}

	nm_remote_connection_delete_async(NM_REMOTE_CONNECTION(connection), NULL,
		_OnForgetSavedDone, job);
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::ForgetSavedNetworkAsync(const char* connectionPath,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (connectionPath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_ForgetSavedJob* job = new _ForgetSavedJob;
	job->nmClient = (NMClient*)fNMClient;
	job->connectionPath = connectionPath;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext, _RunForgetSavedNetwork,
		job);
	return B_OK;
}


// Shared by SetWiFiAutoconnectAsync() and SetWiFiPriorityAsync() -- both
// write a single NMSettingConnection property then commit the same way.
struct _SavedPropertyJob {
	NMClient* nmClient;
	BString connectionPath;
	bool setAutoconnect;	// true: write fAutoconnect; false: write fPriority
	bool autoconnect;
	int32 priority;
	BMessenger replyTo;
	uint32 replyWhat;
};


static void
_OnSavedPropertyCommitDone(GObject* source, GAsyncResult* result,
	gpointer userData)
{
	_SavedPropertyJob* job = (_SavedPropertyJob*)userData;

	GError* error = NULL;
	gboolean ok = nm_remote_connection_commit_changes_finish(
		NM_REMOTE_CONNECTION(source), result, &error);

	BMessage reply(job->replyWhat);
	reply.AddInt32("status", ok ? (int32)B_OK : (int32)B_ERROR);
	if (!ok) {
		reply.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
	}
	job->replyTo.SendMessage(&reply);
	delete job;
}


static gboolean
_RunSetSavedProperty(gpointer data)
{
	_SavedPropertyJob* job = (_SavedPropertyJob*)data;

	NMConnection* connection = (NMConnection*)nm_client_get_connection_by_path(
		job->nmClient, job->connectionPath.String());
	NMSettingConnection* connSetting = connection != NULL
		? nm_connection_get_setting_connection(connection) : NULL;
	if (connSetting == NULL || !NM_IS_REMOTE_CONNECTION(connection)) {
		BMessage reply(job->replyWhat);
		reply.AddInt32("status", (int32)B_ENTRY_NOT_FOUND);
		reply.AddString("reason", "no such saved connection");
		job->replyTo.SendMessage(&reply);
		delete job;
		return G_SOURCE_REMOVE;
	}

	if (job->setAutoconnect) {
		g_object_set(connSetting, NM_SETTING_CONNECTION_AUTOCONNECT,
			(gboolean)job->autoconnect, NULL);
	} else {
		g_object_set(connSetting, NM_SETTING_CONNECTION_AUTOCONNECT_PRIORITY,
			(gint)job->priority, NULL);
	}

	nm_remote_connection_commit_changes_async(NM_REMOTE_CONNECTION(connection),
		TRUE, NULL, _OnSavedPropertyCommitDone, job);
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::SetWiFiAutoconnectAsync(const char* connectionPath,
	bool autoconnect, const BMessenger& replyTo, uint32 replyWhat)
{
	if (connectionPath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_SavedPropertyJob* job = new _SavedPropertyJob;
	job->nmClient = (NMClient*)fNMClient;
	job->connectionPath = connectionPath;
	job->setAutoconnect = true;
	job->autoconnect = autoconnect;
	job->priority = 0;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext, _RunSetSavedProperty,
		job);
	return B_OK;
}


status_t
NMBackend::SetWiFiPriorityAsync(const char* connectionPath, int32 priority,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (connectionPath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_SavedPropertyJob* job = new _SavedPropertyJob;
	job->nmClient = (NMClient*)fNMClient;
	job->connectionPath = connectionPath;
	job->setAutoconnect = false;
	job->autoconnect = false;
	job->priority = priority;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext, _RunSetSavedProperty,
		job);
	return B_OK;
}


// #pragma mark - VPN


// Dispatch thread only -- walks NMClient's connection and active-connection
// lists, both GObject-owned and mutated from D-Bus signals delivered there.
static void
_FillVPNMessage(NMClient* nmClient, BMessage* outMessage)
{
	outMessage->MakeEmpty();

	if (nmClient == NULL) {
		outMessage->AddInt32(kNMFieldVPNCount, 0);
		return;
	}

	const GPtrArray* activeConnections = nm_client_get_active_connections(nmClient);

	const GPtrArray* connections = nm_client_get_connections(nmClient);
	int32 count = 0;

	if (connections != NULL) {
		for (guint i = 0; i < connections->len; i++) {
			NMConnection* connection
				= (NMConnection*)g_ptr_array_index(connections, i);
			if (connection == NULL)
				continue;

			NMSettingConnection* connSetting
				= nm_connection_get_setting_connection(connection);
			if (connSetting == NULL)
				continue;

			const char* type = nm_setting_connection_get_connection_type(
				connSetting);
			// VPN plugins (OpenVPN, WireGuard, etc) all register under the
			// "vpn" setting type; nm_connection_get_setting_vpn() alone would
			// miss WireGuard, which uses its own top-level setting instead.
			bool isVPN = type != NULL
				&& (strcmp(type, NM_SETTING_VPN_SETTING_NAME) == 0
					|| strcmp(type, NM_SETTING_WIREGUARD_SETTING_NAME) == 0);
			if (!isVPN)
				continue;

			const char* id = nm_connection_get_id(connection);
			const char* path = nm_connection_get_path(connection);
			if (id == NULL || path == NULL)
				continue;

			bool connected = false;
			if (activeConnections != NULL) {
				for (guint j = 0; j < activeConnections->len; j++) {
					NMActiveConnection* active = (NMActiveConnection*)
						g_ptr_array_index(activeConnections, j);
					if (active == NULL)
						continue;
					const char* activeUUID = nm_active_connection_get_uuid(active);
					const char* connUUID = nm_connection_get_uuid(connection);
					if (activeUUID != NULL && connUUID != NULL
							&& strcmp(activeUUID, connUUID) == 0
							&& nm_active_connection_get_state(active)
								== NM_ACTIVE_CONNECTION_STATE_ACTIVATED) {
						connected = true;
						break;
					}
				}
			}

			char vpnName[32];
			snprintf(vpnName, sizeof(vpnName), "vpn_%" B_PRId32, count);

			BMessage vpnInfo;
			vpnInfo.AddString(kNMFieldVPNName, id);
			vpnInfo.AddString(kNMFieldVPNPath, path);
			vpnInfo.AddBool(kNMFieldVPNConnected, connected);
			outMessage->AddMessage(vpnName, &vpnInfo);
			count++;
		}
	}

	outMessage->AddInt32(kNMFieldVPNCount, count);
}


status_t
NMBackend::GetVPNConnections(BMessage* outVPNs)
{
	if (outVPNs == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL)
		return B_ERROR;

	BAutolock lock(fLock);
	*outVPNs = fVPNSnapshot;
	return B_OK;
}


struct _VPNActionJob {
	NMBackend* backend;
	NMClient* nmClient;
	BString connectionPath;
	bool connect;	// true = activate, false = deactivate
};


static void
_OnVPNActivateDone(GObject* source, GAsyncResult* result, gpointer userData)
{
	_VPNActionJob* job = (_VPNActionJob*)userData;

	GError* error = NULL;
	NMActiveConnection* active = nm_client_activate_connection_finish(
		job->nmClient, result, &error);

	BMessage message((uint32)NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
	message.AddString(kNMFieldVPNPath, job->connectionPath);
	if (active != NULL) {
		g_object_unref(active);
	} else {
		message.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
	}

	job->backend->_RefreshSnapshotAndNotify(
		NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED, message);
	delete job;
}


static gboolean
_RunConnectVPN(gpointer data)
{
	_VPNActionJob* job = (_VPNActionJob*)data;

	NMConnection* connection = (NMConnection*)nm_client_get_connection_by_path(
		job->nmClient, job->connectionPath.String());
	if (connection == NULL) {
		BMessage message((uint32)NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
		message.AddString(kNMFieldVPNPath, job->connectionPath);
		message.AddString("reason", "no such VPN connection profile");
		job->backend->_RefreshSnapshotAndNotify(
			NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED, message);
		delete job;
		return G_SOURCE_REMOVE;
	}

	// NULL device: let NM pick the default route's interface, standard for
	// VPN activation (a VPN is not bound to one physical device).
	nm_client_activate_connection_async(job->nmClient, connection, NULL, NULL,
		NULL, _OnVPNActivateDone, job);
	return G_SOURCE_REMOVE;
}


static gboolean
_RunDisconnectVPN(gpointer data)
{
	_VPNActionJob* job = (_VPNActionJob*)data;

	const GPtrArray* activeConnections
		= nm_client_get_active_connections(job->nmClient);
	NMActiveConnection* target = NULL;
	if (activeConnections != NULL) {
		for (guint i = 0; i < activeConnections->len; i++) {
			NMActiveConnection* active = (NMActiveConnection*)
				g_ptr_array_index(activeConnections, i);
			if (active == NULL)
				continue;
			const char* path = nm_active_connection_get_connection(active) != NULL
				? nm_connection_get_path(NM_CONNECTION(
					nm_active_connection_get_connection(active)))
				: NULL;
			if (path != NULL && job->connectionPath == path) {
				target = active;
				break;
			}
		}
	}

	BMessage message((uint32)NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED);
	message.AddString(kNMFieldVPNPath, job->connectionPath);
	if (target == NULL) {
		message.AddString("reason", "VPN connection is not active");
	} else {
		GError* error = NULL;
		if (!nm_client_deactivate_connection(job->nmClient, target, NULL,
				&error)) {
			message.AddString("reason",
				error != NULL ? error->message : "unknown error");
			if (error != NULL)
				g_error_free(error);
		}
	}

	job->backend->_RefreshSnapshotAndNotify(
		NMBackend::NOTIFICATION_CONNECTION_STATUS_CHANGED, message);
	delete job;
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::ConnectVPN(const char* connectionPath)
{
	if (connectionPath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_VPNActionJob* job = new _VPNActionJob;
	job->backend = this;
	job->nmClient = (NMClient*)fNMClient;
	job->connectionPath = connectionPath;
	job->connect = true;

	g_main_context_invoke((GMainContext*)fMainContext, _RunConnectVPN, job);
	return B_OK;
}


status_t
NMBackend::DisconnectVPN(const char* connectionPath)
{
	if (connectionPath == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_VPNActionJob* job = new _VPNActionJob;
	job->backend = this;
	job->nmClient = (NMClient*)fNMClient;
	job->connectionPath = connectionPath;
	job->connect = false;

	g_main_context_invoke((GMainContext*)fMainContext, _RunDisconnectVPN, job);
	return B_OK;
}


// Live-write, no Apply -- single atomic NMClient property writes.
// nm_client_networking_set_enabled/nm_client_wireless_set_enabled are
// themselves synchronous D-Bus calls in libnm, so route through the
// dispatch thread rather than calling from whatever thread the menu click
// landed on.
struct _SetEnabledJob {
	NMClient* nmClient;
	bool enabled;
	bool wireless;	// false => networking
};


static gboolean
_RunSetEnabled(gpointer data)
{
	_SetEnabledJob* job = (_SetEnabledJob*)data;
	if (job->wireless)
		nm_client_wireless_set_enabled(job->nmClient, job->enabled);
	else
		nm_client_networking_set_enabled(job->nmClient, job->enabled, NULL);
	delete job;
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::SetNetworkingEnabled(bool enabled)
{
	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_SetEnabledJob* job = new _SetEnabledJob;
	job->nmClient = (NMClient*)fNMClient;
	job->enabled = enabled;
	job->wireless = false;
	g_main_context_invoke((GMainContext*)fMainContext, _RunSetEnabled, job);
	return B_OK;
}


status_t
NMBackend::SetWirelessEnabled(bool enabled)
{
	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_SetEnabledJob* job = new _SetEnabledJob;
	job->nmClient = (NMClient*)fNMClient;
	job->enabled = enabled;
	job->wireless = true;
	g_main_context_invoke((GMainContext*)fMainContext, _RunSetEnabled, job);
	return B_OK;
}


bool
NMBackend::IsNetworkingEnabled()
{
	// NMClient caches these booleans locally (no D-Bus round trip), so a
	// direct read off whatever thread is calling is safe -- unlike the
	// device list, nothing else mutates it concurrently without going
	// through the same cached GObject property.
	if (fNMClient == NULL)
		return false;
	return nm_client_networking_get_enabled((NMClient*)fNMClient) != FALSE;
}


bool
NMBackend::IsWirelessEnabled()
{
	if (fNMClient == NULL)
		return false;
	return nm_client_wireless_get_enabled((NMClient*)fNMClient) != FALSE;
}


status_t
NMBackend::StartWatching(const BMessenger& target, uint32 notificationMask)
{
	// Recorded regardless of whether fNMClient exists yet -- a caller can
	// reach here while NetworkManager isn't running (fNMClient == NULL
	// until _TryCreateNMClient() succeeds, possibly much later via the
	// org.freedesktop.NetworkManager name watcher). Rejecting outright used
	// to mean a watcher that registered during that window never got
	// NM's notifications even after the daemon came up -- the underlying
	// NMClient signals are connected once for the process's lifetime (see
	// _ConnectClientSignals), so this just needs the entry to already be
	// in fWatchers by the time that happens.
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

	return B_OK;
}


status_t
NMBackend::StopWatching(const BMessenger& target)
{
	BAutolock lock(fLock);

	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers.erase(fWatchers.begin() + i);
			break;
		}
	}

	return B_OK;
}


// #pragma mark - Minimum ConnectToWiFi slice


struct _ConnectWiFiJob {
	NMBackend* backend;
	NMClient* nmClient;
	BString devicePath;
	BString ssid;
	BString password;
	BString security;
	bool remember;
	BMessenger replyTo;
	uint32 replyWhat;
};


static void
_OnAddAndActivateDone(GObject* source, GAsyncResult* result, gpointer userData)
{
	_ConnectWiFiJob* job = (_ConnectWiFiJob*)userData;

	GError* error = NULL;
	NMActiveConnection* active = nm_client_add_and_activate_connection_finish(
		job->nmClient, result, &error);

	BMessage reply(job->replyWhat);
	if (active != NULL) {
		// "Started activating" -- not "joined". If the profile has no
		// inline secret, this is precisely the moment NM turns around and
		// calls our SecretAgent.GetSecrets; the actual join completes
		// asynchronously from there, not here.
		reply.AddInt32("status", (int32)B_OK);
		g_object_unref(active);
	} else {
		reply.AddInt32("status", (int32)B_ERROR);
		reply.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
	}
	job->replyTo.SendMessage(&reply);
	delete job;
}


static gboolean
_RunConnectToWiFi(gpointer data)
{
	_ConnectWiFiJob* job = (_ConnectWiFiJob*)data;

	NMDevice* device = _FindDeviceByPath(job->nmClient, job->devicePath.String());
	if (device == NULL) {
		BMessage reply(job->replyWhat);
		reply.AddInt32("status", (int32)B_ENTRY_NOT_FOUND);
		reply.AddString("reason", "no such device");
		job->replyTo.SendMessage(&reply);
		delete job;
		return G_SOURCE_REMOVE;
	}

	// nm_connection_add_setting() takes ownership of each setting object;
	// nothing below is leaked by not unref'ing them individually.
	NMConnection* connection = nm_simple_connection_new();

	NMSettingConnection* connSetting
		= (NMSettingConnection*)nm_setting_connection_new();
	g_object_set(connSetting,
		NM_SETTING_CONNECTION_ID, job->ssid.String(),
		NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRELESS_SETTING_NAME,
		NM_SETTING_CONNECTION_AUTOCONNECT, (gboolean)job->remember,
		NULL);
	nm_connection_add_setting(connection, NM_SETTING(connSetting));

	NMSettingWireless* wirelessSetting
		= (NMSettingWireless*)nm_setting_wireless_new();
	GBytes* ssidBytes = g_bytes_new(job->ssid.String(), job->ssid.Length());
	g_object_set(wirelessSetting, NM_SETTING_WIRELESS_SSID, ssidBytes, NULL);
	g_bytes_unref(ssidBytes);
	nm_connection_add_setting(connection, NM_SETTING(wirelessSetting));

	// No password: an intentionally incomplete profile for a secured
	// network, the case that forces NM to call our agent's GetSecrets on
	// activation -- see 1.1's "control case vs agent-covered case" split.
	if (!job->password.IsEmpty()) {
		NMSettingWirelessSecurity* secSetting =
			(NMSettingWirelessSecurity*)nm_setting_wireless_security_new();

		if (job->security == "wep") {
			g_object_set(secSetting, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT,
				"none", NULL);
			nm_setting_wireless_security_set_wep_key(secSetting, 0,
				job->password.String());
			g_object_set(secSetting,
				NM_SETTING_WIRELESS_SECURITY_WEP_TX_KEYIDX, 0, NULL);
		} else {
			g_object_set(secSetting,
				NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, "wpa-psk",
				NM_SETTING_WIRELESS_SECURITY_PSK, job->password.String(),
				NULL);
		}
		nm_connection_add_setting(connection, NM_SETTING(secSetting));
	}

	nm_client_add_and_activate_connection_async(job->nmClient, connection,
		device, NULL, NULL, _OnAddAndActivateDone, job);

	g_object_unref(connection);
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::ConnectToWiFiAsync(const char* devicePath, const char* ssid,
	const char* password, const char* security, bool remember,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL || ssid == NULL)
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_ConnectWiFiJob* job = new _ConnectWiFiJob;
	job->backend = this;
	job->nmClient = (NMClient*)fNMClient;
	job->devicePath = devicePath;
	job->ssid = ssid;
	job->password = password != NULL ? password : "";
	job->security = security != NULL ? security : "";
	job->remember = remember;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext, _RunConnectToWiFi, job);
	return B_OK;
}


// #pragma mark - SetStaticIPConfigAsync


struct _SetStaticIPJob {
	NMClient* nmClient;
	BString connectionPath;
	NMBackend::IP4ConfigMode mode;
	BString address;
	BString netmask;
	BString gateway;
	BString dns;
	BMessenger replyTo;
	uint32 replyWhat;
};


static void
_OnCommitIPConfigDone(GObject* source, GAsyncResult* result, gpointer userData)
{
	_SetStaticIPJob* job = (_SetStaticIPJob*)userData;

	GError* error = NULL;
	gboolean ok = nm_remote_connection_commit_changes_finish(
		NM_REMOTE_CONNECTION(source), result, &error);

	BMessage reply(job->replyWhat);
	reply.AddInt32("status", ok ? (int32)B_OK : (int32)B_ERROR);
	if (!ok) {
		reply.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
	}
	job->replyTo.SendMessage(&reply);
	delete job;
}


static gboolean
_RunSetStaticIPConfig(gpointer data)
{
	_SetStaticIPJob* job = (_SetStaticIPJob*)data;

	// Keyed on the profile directly now -- the caller (StaticIPView's
	// profile chooser) decides which saved connection to write to; this no
	// longer requires the device to have anything active.
	NMConnection* connection = (NMConnection*)nm_client_get_connection_by_path(
		job->nmClient, job->connectionPath.String());
	if (connection == NULL || !NM_IS_REMOTE_CONNECTION(connection)) {
		BMessage reply(job->replyWhat);
		reply.AddInt32("status", (int32)B_ENTRY_NOT_FOUND);
		reply.AddString("reason", "no such saved connection profile");
		job->replyTo.SendMessage(&reply);
		delete job;
		return G_SOURCE_REMOVE;
	}

	NMRemoteConnection* remote = NM_REMOTE_CONNECTION(connection);
	NMSettingIPConfig* ipSetting = nm_connection_get_setting_ip4_config(connection);
	if (ipSetting == NULL) {
		ipSetting = (NMSettingIPConfig*)nm_setting_ip4_config_new();
		nm_connection_add_setting(connection, NM_SETTING(ipSetting));
	}

	nm_setting_ip_config_clear_addresses(ipSetting);
	nm_setting_ip_config_clear_dns(ipSetting);
	g_object_set(ipSetting, NM_SETTING_IP_CONFIG_GATEWAY, NULL, NULL);

	const char* methodStr = NM_SETTING_IP4_CONFIG_METHOD_AUTO;
	if (job->mode == NMBackend::IP4_CONFIG_MANUAL)
		methodStr = NM_SETTING_IP4_CONFIG_METHOD_MANUAL;
	else if (job->mode == NMBackend::IP4_CONFIG_DISABLED)
		methodStr = NM_SETTING_IP4_CONFIG_METHOD_DISABLED;
	g_object_set(ipSetting, NM_SETTING_IP_CONFIG_METHOD, methodStr, NULL);

	if (job->mode == NMBackend::IP4_CONFIG_MANUAL) {
		int32 prefix = _NetmaskToPrefix(job->netmask.String());
		if (prefix < 0) {
			BMessage reply(job->replyWhat);
			reply.AddInt32("status", (int32)B_BAD_VALUE);
			reply.AddString("reason", "invalid IPv4 netmask");
			job->replyTo.SendMessage(&reply);
			delete job;
			return G_SOURCE_REMOVE;
		}

		GError* error = NULL;
		NMIPAddress* addr = nm_ip_address_new(AF_INET, job->address.String(),
			prefix, &error);
		if (addr == NULL) {
			BMessage reply(job->replyWhat);
			reply.AddInt32("status", (int32)B_BAD_VALUE);
			reply.AddString("reason",
				error != NULL ? error->message : "invalid IPv4 address");
			if (error != NULL)
				g_error_free(error);
			job->replyTo.SendMessage(&reply);
			delete job;
			return G_SOURCE_REMOVE;
		}
		nm_setting_ip_config_add_address(ipSetting, addr);
		nm_ip_address_unref(addr);

		if (!job->gateway.IsEmpty()) {
			g_object_set(ipSetting, NM_SETTING_IP_CONFIG_GATEWAY,
				job->gateway.String(), NULL);
		}

		BString dnsList(job->dns);
		int32 start = 0;
		while (start < dnsList.Length()) {
			int32 comma = dnsList.FindFirst(',', start);
			BString token = comma < 0
				? BString(dnsList.String() + start)
				: BString(dnsList.String() + start, comma - start);
			token.Trim();
			start = comma < 0 ? dnsList.Length() : comma + 1;
			if (!token.IsEmpty())
				nm_setting_ip_config_add_dns(ipSetting, token.String());
		}
	}

	nm_remote_connection_commit_changes_async(remote, TRUE, NULL,
		_OnCommitIPConfigDone, job);
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::SetStaticIPConfigAsync(const char* connectionPath,
	IP4ConfigMode mode, const char* address, const char* netmask,
	const char* gateway, const char* dns, const BMessenger& replyTo,
	uint32 replyWhat)
{
	if (connectionPath == NULL)
		return B_BAD_VALUE;
	if (mode == IP4_CONFIG_MANUAL && (address == NULL || address[0] == '\0'))
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_SetStaticIPJob* job = new _SetStaticIPJob;
	job->nmClient = (NMClient*)fNMClient;
	job->connectionPath = connectionPath;
	job->mode = mode;
	job->address = address != NULL ? address : "";
	job->netmask = netmask != NULL ? netmask : "";
	job->gateway = gateway != NULL ? gateway : "";
	job->dns = dns != NULL ? dns : "";
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext, _RunSetStaticIPConfig,
		job);
	return B_OK;
}


// #pragma mark - CreateWiredConnectionProfileAsync


struct _CreateProfileJob {
	NMClient* nmClient;
	BString devicePath;
	BString name;
	BMessenger replyTo;
	uint32 replyWhat;
};


static void
_OnAddConnectionDone(GObject* source, GAsyncResult* result, gpointer userData)
{
	_CreateProfileJob* job = (_CreateProfileJob*)userData;

	GError* error = NULL;
	NMRemoteConnection* remote = nm_client_add_connection_finish(
		NM_CLIENT(source), result, &error);

	BMessage reply(job->replyWhat);
	if (remote != NULL) {
		const char* path = nm_connection_get_path(NM_CONNECTION(remote));
		reply.AddInt32("status", (int32)B_OK);
		reply.AddString(kNMFieldProfilePath, path != NULL ? path : "");
		reply.AddString(kNMFieldProfileID, job->name);
		g_object_unref(remote);
	} else {
		reply.AddInt32("status", (int32)B_ERROR);
		reply.AddString("reason",
			error != NULL ? error->message : "unknown error");
		if (error != NULL)
			g_error_free(error);
	}
	job->replyTo.SendMessage(&reply);
	delete job;
}


static gboolean
_RunCreateWiredConnectionProfile(gpointer data)
{
	_CreateProfileJob* job = (_CreateProfileJob*)data;

	NMDevice* device = _FindDeviceByPath(job->nmClient, job->devicePath.String());
	if (device == NULL) {
		BMessage reply(job->replyWhat);
		reply.AddInt32("status", (int32)B_ENTRY_NOT_FOUND);
		reply.AddString("reason", "no such device");
		job->replyTo.SendMessage(&reply);
		delete job;
		return G_SOURCE_REMOVE;
	}

	if (nm_device_get_device_type(device) != NM_DEVICE_TYPE_ETHERNET) {
		BMessage reply(job->replyWhat);
		reply.AddInt32("status", (int32)B_NOT_SUPPORTED);
		reply.AddString("reason",
			"profile creation is only supported for Ethernet devices; "
			"join a WiFi network to create a wireless profile");
		job->replyTo.SendMessage(&reply);
		delete job;
		return G_SOURCE_REMOVE;
	}

	const char* iface = nm_device_get_iface(device);
	if (iface == NULL || iface[0] == '\0') {
		BMessage reply(job->replyWhat);
		reply.AddInt32("status", (int32)B_ERROR);
		reply.AddString("reason", "device has no interface name to bind to");
		job->replyTo.SendMessage(&reply);
		delete job;
		return G_SOURCE_REMOVE;
	}

	// nm_connection_add_setting() takes ownership of each setting object;
	// nothing below is leaked by not unref'ing them individually.
	NMConnection* connection = nm_simple_connection_new();

	char* uuid = nm_utils_uuid_generate();
	NMSettingConnection* connSetting
		= (NMSettingConnection*)nm_setting_connection_new();
	g_object_set(connSetting,
		NM_SETTING_CONNECTION_ID, job->name.String(),
		NM_SETTING_CONNECTION_UUID, uuid,
		NM_SETTING_CONNECTION_TYPE, NM_SETTING_WIRED_SETTING_NAME,
		NM_SETTING_CONNECTION_INTERFACE_NAME, iface,
		NM_SETTING_CONNECTION_AUTOCONNECT, (gboolean)TRUE,
		NULL);
	g_free(uuid);
	nm_connection_add_setting(connection, NM_SETTING(connSetting));

	nm_connection_add_setting(connection,
		NM_SETTING(nm_setting_wired_new()));

	NMSettingIPConfig* ip4Setting
		= (NMSettingIPConfig*)nm_setting_ip4_config_new();
	g_object_set(ip4Setting, NM_SETTING_IP_CONFIG_METHOD,
		NM_SETTING_IP4_CONFIG_METHOD_AUTO, NULL);
	nm_connection_add_setting(connection, NM_SETTING(ip4Setting));

	// Not user-facing (StaticIPView is IPv4-only) but every field-tested NM
	// connection carries one; omitting it left profiles that nmcli/NM
	// considered incomplete in earlier testing.
	NMSettingIPConfig* ip6Setting
		= (NMSettingIPConfig*)nm_setting_ip6_config_new();
	g_object_set(ip6Setting, NM_SETTING_IP_CONFIG_METHOD,
		NM_SETTING_IP6_CONFIG_METHOD_AUTO, NULL);
	nm_connection_add_setting(connection, NM_SETTING(ip6Setting));

	// Saved, not activated -- an existing link must not be disrupted by
	// merely creating a profile, and a fresh device has nothing yet to
	// disrupt either way; the user brings it up explicitly once configured.
	nm_client_add_connection_async(job->nmClient, connection, TRUE, NULL,
		_OnAddConnectionDone, job);

	g_object_unref(connection);
	return G_SOURCE_REMOVE;
}


status_t
NMBackend::CreateWiredConnectionProfileAsync(const char* devicePath,
	const char* name, const BMessenger& replyTo, uint32 replyWhat)
{
	if (devicePath == NULL || name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	if (fNMClient == NULL || fMainContext == NULL)
		return B_ERROR;

	_CreateProfileJob* job = new _CreateProfileJob;
	job->nmClient = (NMClient*)fNMClient;
	job->devicePath = devicePath;
	job->name = name;
	job->replyTo = replyTo;
	job->replyWhat = replyWhat;

	g_main_context_invoke((GMainContext*)fMainContext,
		_RunCreateWiredConnectionProfile, job);
	return B_OK;
}


// #pragma mark - org.freedesktop.NetworkManager.SecretAgent


// The fixed object path every NM secret agent must export itself at -- NM
// calls back on this exact path on the agent's own unique bus name; unlike
// BlueZ's Agent1 it is not something the agent gets to choose.
static const char* kSecretAgentPath
	= "/org/freedesktop/NetworkManager/SecretAgent";
static const char* kSecretAgentIdentifier = "org.vitruvian.NetworkStatus";

static const gint kNMCallTimeoutMs = 5000;

static const char* kSecretAgentIntrospectionXML =
	"<node>"
	"  <interface name='org.freedesktop.NetworkManager.SecretAgent'>"
	"    <method name='GetSecrets'>"
	"      <arg type='a{sa{sv}}' name='connection' direction='in'/>"
	"      <arg type='o' name='connection_path' direction='in'/>"
	"      <arg type='s' name='setting_name' direction='in'/>"
	"      <arg type='as' name='hints' direction='in'/>"
	"      <arg type='u' name='flags' direction='in'/>"
	"      <arg type='a{sa{sv}}' name='secrets' direction='out'/>"
	"    </method>"
	"    <method name='CancelGetSecrets'>"
	"      <arg type='o' name='connection_path' direction='in'/>"
	"      <arg type='s' name='setting_name' direction='in'/>"
	"    </method>"
	"    <method name='SaveSecrets'>"
	"      <arg type='a{sa{sv}}' name='connection' direction='in'/>"
	"      <arg type='o' name='connection_path' direction='in'/>"
	"    </method>"
	"    <method name='DeleteSecrets'>"
	"      <arg type='a{sa{sv}}' name='connection' direction='in'/>"
	"      <arg type='o' name='connection_path' direction='in'/>"
	"    </method>"
	"  </interface>"
	"</node>";


// Holds enough of a still-open GetSecrets call to answer it later: which
// invocation, which setting the reply must be nested under, and which key
// within that setting the typed password becomes (psk / wep-key0 /
// password). Stored as the AuthPromptRouter cookie for the request.
struct _SecretRequestContext {
	GDBusMethodInvocation* invocation;
	BString settingName;
	BString keyName;
};


static BString
_ExtractStringProperty(GVariant* connection, const char* settingName,
	const char* propName)
{
	BString result;
	GVariant* setting = g_variant_lookup_value(connection, settingName,
		G_VARIANT_TYPE("a{sv}"));
	if (setting != NULL) {
		GVariant* value = g_variant_lookup_value(setting, propName,
			G_VARIANT_TYPE_STRING);
		if (value != NULL) {
			result = g_variant_get_string(value, NULL);
			g_variant_unref(value);
		}
		g_variant_unref(setting);
	}
	return result;
}


static BString
_ExtractSSID(GVariant* connection)
{
	BString result;
	GVariant* setting = g_variant_lookup_value(connection, "802-11-wireless",
		G_VARIANT_TYPE("a{sv}"));
	if (setting != NULL) {
		GVariant* ssidBytes = g_variant_lookup_value(setting, "ssid",
			G_VARIANT_TYPE("ay"));
		if (ssidBytes != NULL) {
			gsize len = 0;
			gconstpointer data = g_variant_get_fixed_array(ssidBytes, &len,
				sizeof(guint8));
			if (data != NULL && len > 0)
				result.SetTo((const char*)data, len);
			g_variant_unref(ssidBytes);
		}
		g_variant_unref(setting);
	}
	return result;
}


static BString
_ExtractEAPMethod(GVariant* connection)
{
	BString result;
	GVariant* setting = g_variant_lookup_value(connection, "802-1x",
		G_VARIANT_TYPE("a{sv}"));
	if (setting != NULL) {
		GVariant* eapArray = g_variant_lookup_value(setting, "eap",
			G_VARIANT_TYPE("as"));
		if (eapArray != NULL) {
			gsize count = 0;
			const gchar** methods = g_variant_get_strv(eapArray, &count);
			if (methods != NULL && count > 0)
				result = methods[0];
			g_free(methods);
			g_variant_unref(eapArray);
		}
		g_variant_unref(setting);
	}
	return result;
}


// Returns false (and fills missingPath) if a referenced 802.1x certificate
// file is not present on disk. NM's own validation usually catches this
// before ever calling GetSecrets, but a cert moved/deleted after the
// profile was created is not guaranteed to be caught there, and we list
// "missing certificate" as its own dialog case.
static bool
_Extract8021xCertificatesPresent(GVariant* connection, BString& missingPath)
{
	static const char* const kCertProps[]
		= { "ca-cert", "client-cert", "private-key" };

	GVariant* setting = g_variant_lookup_value(connection, "802-1x",
		G_VARIANT_TYPE("a{sv}"));
	if (setting == NULL)
		return true;

	bool allPresent = true;
	for (size_t i = 0; i < sizeof(kCertProps) / sizeof(kCertProps[0]); i++) {
		GVariant* certBytes = g_variant_lookup_value(setting, kCertProps[i],
			G_VARIANT_TYPE("ay"));
		if (certBytes == NULL)
			continue;

		gsize len = 0;
		gconstpointer data = g_variant_get_fixed_array(certBytes, &len,
			sizeof(guint8));
		// NM's "blob" cert scheme: a NUL-terminated "file://<path>" byte
		// string when the cert is referenced by path rather than embedded.
		if (data != NULL && len > 7 && memcmp(data, "file://", 7) == 0) {
			size_t pathLen = len - 7;
			if (pathLen > 0 && ((const char*)data)[len - 1] == '\0')
				pathLen--;
			BString path((const char*)data + 7, pathLen);
			if (path.Length() > 0 && access(path.String(), F_OK) != 0) {
				missingPath = path;
				allPresent = false;
			}
		}
		g_variant_unref(certBytes);
		if (!allPresent)
			break;
	}
	g_variant_unref(setting);
	return allPresent;
}


void
NMBackend::_CancelPendingSecretRequest()
{
	std::vector<void*> cookies;
	fSecretRouter.TakeAll(cookies);
	for (size_t i = 0; i < cookies.size(); i++) {
		_SecretRequestContext* ctx = (_SecretRequestContext*)cookies[i];
		g_dbus_method_invocation_return_dbus_error(ctx->invocation,
			"org.freedesktop.NetworkManager.SecretAgent.Error.UserCanceled",
			"Canceled");
		delete ctx;
	}
	fPendingSecretRequestId = 0;
	fPendingConnectionPath = "";
	fPendingSettingName = "";
}


void
NMBackend::_HandleGetSecrets(GVariant* parameters,
	GDBusMethodInvocation* invocation)
{
	GVariant* connection = NULL;
	const char* connectionPath = NULL;
	const char* settingName = NULL;
	GVariant* hints = NULL;
	guint32 flags = 0;
	g_variant_get(parameters, "(@a{sa{sv}}&o&s@asu)", &connection,
		&connectionPath, &settingName, &hints, &flags);

	bool allowInteraction
		= (flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION) != 0;
	bool requestNew
		= (flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_REQUEST_NEW) != 0;

	if (!allowInteraction) {
		// Autoconnect/roaming probe with no cached secret and no permission
		// to prompt -- must return without ever showing a dialog.
		g_dbus_method_invocation_return_dbus_error(invocation,
			"org.freedesktop.NetworkManager.SecretAgent.Error.NoSecrets",
			"No cached secret and interaction not allowed");
		g_variant_unref(connection);
		g_variant_unref(hints);
		return;
	}

	BString ssid = _ExtractSSID(connection);

	int32 kind = -1;
	BString keyName("psk");
	BString method;
	BString missingFile;

	if (strcmp(settingName, "802-11-wireless-security") == 0) {
		BString keyMgmt = _ExtractStringProperty(connection,
			"802-11-wireless-security", "key-mgmt");
		if (keyMgmt == "none") {
			kind = SECRET_KIND_WEP;
			keyName = "wep-key0";
		} else {
			// wpa-psk, sae (WPA3), or unset -- all get the PSK dialog; NM
			// only calls GetSecrets for this setting on the PSK path, the
			// WPA-Enterprise case arrives as setting_name "802-1x" instead.
			kind = SECRET_KIND_WPA_PSK;
			keyName = "psk";
		}
	} else if (strcmp(settingName, "802-1x") == 0) {
		BString connType = _ExtractStringProperty(connection, "connection",
			"type");
		bool wired = (connType == "802-3-ethernet");
		keyName = "password";
		method = _ExtractEAPMethod(connection);

		if (!_Extract8021xCertificatesPresent(connection, missingFile))
			kind = SECRET_KIND_MISSING_CERTIFICATE;
		else
			kind = wired ? SECRET_KIND_WIRED_8021X : SECRET_KIND_ENTERPRISE;
	} else {
		// Not one of the dialog cases (e.g. a VPN plugin's own secret) --
		// this agent has no dialog for it; decline rather than guess.
		BString reason;
		reason.SetToFormat("No dialog implemented for setting %s",
			settingName);
		g_dbus_method_invocation_return_dbus_error(invocation,
			"org.freedesktop.NetworkManager.SecretAgent.Error.NoSecrets",
			reason.String());
		g_variant_unref(connection);
		g_variant_unref(hints);
		return;
	}

	g_variant_unref(connection);
	g_variant_unref(hints);

	// One live dialog at a time: a second GetSecrets cancels whatever
	// is still pending before this one starts.
	_CancelPendingSecretRequest();

	BMessage request((uint32)SECRET_REQUEST);
	request.AddInt32("kind", kind);
	request.AddString("ssid", ssid);
	request.AddBool("request_new", requestNew);
	if (!method.IsEmpty())
		request.AddString("method", method);
	if (!missingFile.IsEmpty())
		request.AddString("missing_file", missingFile);

	_SecretRequestContext* ctx = new _SecretRequestContext;
	ctx->invocation = invocation;
	ctx->settingName = settingName;
	ctx->keyName = keyName;

	uint32 requestId = 0;
	status_t status = fSecretRouter.BeginRequest(request, ctx, &requestId);
	if (status != B_OK) {
		// No UI registered (or it just died) -- fail loud and fast rather
		// than leaving NM's own ~25s secrets timeout as the only thing
		// standing between this and looking like broken WiFi.
		g_dbus_method_invocation_return_dbus_error(invocation,
			"org.freedesktop.NetworkManager.SecretAgent.Error.InternalError",
			"No WiFi credential UI is registered");
		delete ctx;
		return;
	}

	BAutolock lock(fLock);
	fPendingSecretRequestId = requestId;
	fPendingConnectionPath = connectionPath;
	fPendingSettingName = settingName;
}


void
NMBackend::_HandleCancelGetSecrets(GVariant* parameters,
	GDBusMethodInvocation* invocation)
{
	const char* connectionPath = NULL;
	const char* settingName = NULL;
	g_variant_get(parameters, "(&o&s)", &connectionPath, &settingName);

	uint32 idToCancel = 0;
	{
		BAutolock lock(fLock);
		if (fPendingSecretRequestId != 0
				&& fPendingConnectionPath == connectionPath
				&& fPendingSettingName == settingName) {
			idToCancel = fPendingSecretRequestId;
			fPendingSecretRequestId = 0;
		}
	}

	if (idToCancel != 0) {
		void* cookieRaw = fSecretRouter.Take(idToCancel);
		if (cookieRaw != NULL) {
			_SecretRequestContext* ctx = (_SecretRequestContext*)cookieRaw;
			g_dbus_method_invocation_return_dbus_error(ctx->invocation,
				"org.freedesktop.NetworkManager.SecretAgent.Error.UserCanceled",
				"Canceled");
			delete ctx;

			BMessenger uiHandler = fSecretRouter.UIHandler();
			if (uiHandler.IsValid()) {
				BMessage cancel((uint32)SECRET_CANCEL);
				uiHandler.SendMessage(&cancel);
			}
		}
	}

	g_dbus_method_invocation_return_value(invocation, NULL);
}


void
NMBackend::_SecretAgentMethodCall(GDBusConnection* connection,
	const char* sender, const char* objectPath, const char* interfaceName,
	const char* methodName, GVariant* parameters,
	GDBusMethodInvocation* invocation, void* userData)
{
	NMBackend* backend = (NMBackend*)userData;

	if (strcmp(methodName, "GetSecrets") == 0) {
		backend->_HandleGetSecrets(parameters, invocation);
	} else if (strcmp(methodName, "CancelGetSecrets") == 0) {
		backend->_HandleCancelGetSecrets(parameters, invocation);
	} else if (strcmp(methodName, "SaveSecrets") == 0) {
		// Storage is delegated to NM itself (system-owned secrets, the
		// default flags on connections this backend creates) -- there is
		// nothing for this agent to persist. Acknowledging is honest: NM
		// only needs to know the agent handled the notification, not that
		// it wrote anything of its own.
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else if (strcmp(methodName, "DeleteSecrets") == 0) {
		// Same rationale as SaveSecrets -- this agent holds nothing to
		// delete.
		g_dbus_method_invocation_return_value(invocation, NULL);
	} else {
		g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR,
			G_DBUS_ERROR_UNKNOWN_METHOD, "Unknown SecretAgent method %s",
			methodName);
	}
}


void
NMBackend::_CompleteSecretRequest(uint32 requestId, bool accepted,
	const BString& password, const BString& identity, bool remember)
{
	void* cookieRaw = fSecretRouter.Take(requestId);
	if (cookieRaw == NULL)
		return;	// already answered, cancelled, or stale -- idempotent

	_SecretRequestContext* ctx = (_SecretRequestContext*)cookieRaw;

	{
		BAutolock lock(fLock);
		if (fPendingSecretRequestId == requestId)
			fPendingSecretRequestId = 0;
	}

	if (!accepted) {
		g_dbus_method_invocation_return_dbus_error(ctx->invocation,
			"org.freedesktop.NetworkManager.SecretAgent.Error.UserCanceled",
			"Canceled by user");
		delete ctx;
		return;
	}

	// Closes the 1.1 gap: "remember" (autoconnect) is now applied to the
	// EXISTING connection profile GetSecrets was called about, not only to
	// profiles ConnectToWiFiAsync() itself creates. fPendingConnectionPath
	// is the object path NM handed us in GetSecrets -- update-and-commit is
	// fire-and-forget; a failure here does not block answering the secrets
	// request, which must happen regardless.
	if (fNMClient != NULL && !fPendingConnectionPath.IsEmpty()) {
		NMConnection* connection = (NMConnection*)nm_client_get_connection_by_path(
			(NMClient*)fNMClient, fPendingConnectionPath.String());
		if (connection != NULL) {
			NMSettingConnection* connSetting
				= nm_connection_get_setting_connection(connection);
			if (connSetting != NULL) {
				g_object_set(connSetting, NM_SETTING_CONNECTION_AUTOCONNECT,
					(gboolean)remember, NULL);
				if (NM_IS_REMOTE_CONNECTION(connection)) {
					nm_remote_connection_commit_changes_async(
						NM_REMOTE_CONNECTION(connection), TRUE, NULL, NULL,
						NULL);
				}
			}
		}
	}
	(void)identity;

	GVariantBuilder settingBuilder;
	g_variant_builder_init(&settingBuilder, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&settingBuilder, "{sv}", ctx->keyName.String(),
		g_variant_new_string(password.String()));

	GVariantBuilder outerBuilder;
	g_variant_builder_init(&outerBuilder, G_VARIANT_TYPE("a{sa{sv}}"));
	g_variant_builder_add(&outerBuilder, "{s@a{sv}}", ctx->settingName.String(),
		g_variant_builder_end(&settingBuilder));

	g_dbus_method_invocation_return_value(ctx->invocation,
		g_variant_new("(@a{sa{sv}})", g_variant_builder_end(&outerBuilder)));

	delete ctx;
}


static gboolean
_RunCompleteSecretRequest(gpointer data);


struct _CompleteSecretCookie {
	NMBackend* backend;
	uint32 requestId;
	bool accepted;
	BString password;
	BString identity;
	bool remember;
};


static gboolean
_RunCompleteSecretRequest(gpointer data)
{
	_CompleteSecretCookie* cookie = (_CompleteSecretCookie*)data;
	cookie->backend->_CompleteSecretRequest(cookie->requestId,
		cookie->accepted, cookie->password, cookie->identity,
		cookie->remember);
	delete cookie;
	return G_SOURCE_REMOVE;
}


void
NMBackend::CompleteSecretRequest(uint32 requestId, bool accepted,
	const BString& password, const BString& identity, bool remember)
{
	if (fMainContext == NULL || requestId == 0)
		return;

	_CompleteSecretCookie* cookie = new _CompleteSecretCookie;
	cookie->backend = this;
	cookie->requestId = requestId;
	cookie->accepted = accepted;
	cookie->password = password;
	cookie->identity = identity;
	cookie->remember = remember;

	// GDBusMethodInvocation completion is documented thread-safe from any
	// thread, but routing through the dispatch thread keeps a single
	// synchronization idiom for all SecretAgent/router state instead of a
	// second one just for this call.
	g_main_context_invoke((GMainContext*)fMainContext,
		_RunCompleteSecretRequest, cookie);
}


status_t
NMBackend::_RegisterSecretAgent(const BMessenger& uiHandler)
{
	if (fNMClient == NULL)
		return B_ERROR;

	BAutolock lock(fLock);

	fSecretRouter.SetUIHandler(uiHandler);

	GDBusConnection* connection = (GDBusConnection*)nm_client_get_dbus_connection(
		(NMClient*)fNMClient);
	if (connection == NULL) {
		fprintf(stderr, "NMBackend: no D-Bus connection to export "
			"SecretAgent on\n");
		return B_ERROR;
	}

	// The GDBus object export is tied to the system-bus connection, not to
	// NetworkManager's bus name -- it survives an NM restart untouched, so
	// only do it once. What does NOT survive an NM restart is the
	// AgentManager's own record of us (that's NM's state, wiped when NM
	// restarts), so RegisterWithCapabilities below always re-runs -- this is
	// also how NM restart recovery (_OnNMNameAppeared -> _TryCreateNMClient)
	// gets the agent working again without re-exporting the object.
	if (fSecretAgentRegistrationId == 0) {
		GError* error = NULL;
		GDBusNodeInfo* nodeInfo = g_dbus_node_info_new_for_xml(
			kSecretAgentIntrospectionXML, &error);
		if (nodeInfo == NULL) {
			fprintf(stderr, "NMBackend: bad SecretAgent introspection XML: %s\n",
				error ? error->message : "unknown error");
			if (error)
				g_error_free(error);
			return B_ERROR;
		}

		static const GDBusInterfaceVTable vtable = {
			_SecretAgentMethodCall, NULL, NULL
		};

		guint id = g_dbus_connection_register_object(connection,
			kSecretAgentPath, nodeInfo->interfaces[0], &vtable, this, NULL,
			&error);
		g_dbus_node_info_unref(nodeInfo);

		if (id == 0) {
			fprintf(stderr, "NMBackend: failed to export SecretAgent: %s\n",
				error ? error->message : "unknown error");
			if (error)
				g_error_free(error);
			return B_ERROR;
		}
		fSecretAgentRegistrationId = id;
	}

	GError* error = NULL;
	GVariant* result = g_dbus_connection_call_sync(connection,
		"org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager/AgentManager",
		"org.freedesktop.NetworkManager.AgentManager", "RegisterWithCapabilities",
		g_variant_new("(su)", kSecretAgentIdentifier, (guint32)0), NULL,
		G_DBUS_CALL_FLAGS_NONE, kNMCallTimeoutMs, NULL, &error);
	if (result == NULL) {
		// Loud, not silent: there should be exactly one
		// registered agent, so a failure here (e.g. a stale registration
		// from a crashed previous instance) is worth knowing about, not
		// swallowing. The exported object is left in place either way --
		// a later retry (e.g. the next NM restart) can still use it.
		fprintf(stderr, "NMBackend: AgentManager.RegisterWithCapabilities "
			"failed: %s\n", error ? error->message : "unknown error");
		if (error)
			g_error_free(error);
		return B_ERROR;
	}
	g_variant_unref(result);

	return B_OK;
}


status_t
NMBackend::_UnregisterSecretAgent()
{
	BAutolock lock(fLock);

	_CancelPendingSecretRequest();
	fSecretRouter.SetUIHandler(BMessenger());

	if (fSecretAgentRegistrationId == 0)
		return B_OK;

	if (fNMClient != NULL) {
		GDBusConnection* connection
			= (GDBusConnection*)nm_client_get_dbus_connection((NMClient*)fNMClient);
		if (connection != NULL) {
			GError* error = NULL;
			GVariant* result = g_dbus_connection_call_sync(connection,
				"org.freedesktop.NetworkManager",
				"/org/freedesktop/NetworkManager/AgentManager",
				"org.freedesktop.NetworkManager.AgentManager", "Unregister",
				NULL, NULL, G_DBUS_CALL_FLAGS_NONE, kNMCallTimeoutMs, NULL,
				&error);
			if (result != NULL)
				g_variant_unref(result);
			else if (error != NULL)
				g_error_free(error);

			g_dbus_connection_unregister_object(connection,
				fSecretAgentRegistrationId);
		}
	}
	fSecretAgentRegistrationId = 0;

	return B_OK;
}


struct _SecretAgentRegisterCookie {
	NMBackend* backend;
	BMessenger uiHandler;
};


static void
_RunRegisterSecretAgentAsync(void* cookie, BMessage* reply)
{
	_SecretAgentRegisterCookie* job = (_SecretAgentRegisterCookie*)cookie;
	status_t status = job->backend->_RegisterSecretAgent(job->uiHandler);
	reply->AddInt32("status", status);
	delete job;
}


status_t
NMBackend::RegisterSecretAgentAsync(const BMessenger& uiHandler,
	const BMessenger& replyTo, uint32 replyWhat)
{
	_SecretAgentRegisterCookie* cookie = new _SecretAgentRegisterCookie;
	cookie->backend = this;
	cookie->uiHandler = uiHandler;

	return _RunOnDispatchThread(_RunRegisterSecretAgentAsync, cookie, replyTo,
		replyWhat);
}


static void
_RunUnregisterSecretAgentAsync(void* cookie, BMessage* reply)
{
	NMBackend* backend = (NMBackend*)cookie;
	status_t status = backend->_UnregisterSecretAgent();
	reply->AddInt32("status", status);
}


status_t
NMBackend::UnregisterSecretAgentAsync(const BMessenger& replyTo,
	uint32 replyWhat)
{
	return _RunOnDispatchThread(_RunUnregisterSecretAgentAsync, this, replyTo,
		replyWhat);
}