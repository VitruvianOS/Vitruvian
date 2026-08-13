/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _BLUETOOTH_KIT_BLUEZ_BACKEND_H
#define _BLUETOOTH_KIT_BLUEZ_BACKEND_H


#include <SupportDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Locker.h>
#include <String.h>

#include <AuthPromptRouter.h>

#include <map>
#include <vector>


// Opaque GLib/GDBus forward declarations so this header doesn't force
// gio/glib include paths onto every consumer of BlueZBackend.h; the .cpp
// includes <gio/gio.h> for the real definitions, which are ABI-compatible
// with these (same struct tags / same underlying typedefs).
typedef unsigned int guint;
typedef int gboolean;
typedef void* gpointer;
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GVariant GVariant;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef struct _GError GError;
typedef struct _GObject GObject;
typedef struct _GAsyncResult GAsyncResult;


class BlueZBackend {
public:
	static BlueZBackend* Instance();

	// Adapter enumeration
	//
	// The synchronous forms below block the caller until the D-Bus round trip
	// (or org.bluez auto-activation) completes -- up to kBlueZCallTimeoutMs.
	// They are kept for non-UI callers only. No UI surface may call them: see
	// GetAdaptersAsync()/GetDevicesAsync() etc, which fire the request and
	// return immediately, delivering the result as a BMessage.
	status_t GetAdapters(BMessage* outAdapters);
	status_t GetAdapterInfo(const char* adapterPath, BMessage* outInfo);
	status_t GetAdaptersAsync(const BMessenger& replyTo, uint32 replyWhat);

	// True once org.bluez has an owner on the system bus (bluetoothd is
	// up), independent of whether an adapter/device has been enumerated
	// yet. GetStatusAsync()'s reply carries this as "bluez_available" so
	// BluetoothWindow/the replicant can render "Bluetooth service is not
	// running" instead of an empty device list.
	bool IsServiceAvailable();

	// Adapter control
	status_t SetAdapterPowered(const char* adapterPath, bool powered);
	status_t SetAdapterPoweredAsync(const char* adapterPath, bool powered,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t SetAdapterDiscoverable(const char* adapterPath, bool discoverable);
	status_t SetAdapterDiscoverableAsync(const char* adapterPath,
		bool discoverable, const BMessenger& replyTo, uint32 replyWhat);
	// org.bluez.Adapter1.Pairable: whether the adapter accepts incoming
	// pairing requests at all. This is the only genuine BlueZ knob for
	// "incoming connections policy" -- there is no multi-level policy
	// (all/trusted-only/ask) on the daemon side, just this on/off gate.
	status_t SetAdapterPairable(const char* adapterPath, bool pairable);
	status_t SetAdapterPairableAsync(const char* adapterPath, bool pairable,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t StartDiscovery(const char* adapterPath);
	status_t StopDiscovery(const char* adapterPath);
	status_t StartDiscoveryAsync(const char* adapterPath,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t StopDiscoveryAsync(const char* adapterPath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Device operations
	status_t GetDevices(BMessage* outDevices);
	status_t GetDeviceInfo(const char* devicePath, BMessage* outInfo);
	status_t GetDevicesAsync(const BMessenger& replyTo, uint32 replyWhat);
	status_t ConnectDevice(const char* devicePath);
	status_t DisconnectDevice(const char* devicePath);
	status_t PairDevice(const char* devicePath, const char* pin = NULL);
	status_t UnpairDevice(const char* devicePath);
	status_t ConnectDeviceAsync(const char* devicePath,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t DisconnectDeviceAsync(const char* devicePath,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t PairDeviceAsync(const char* devicePath,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t UnpairDeviceAsync(const char* devicePath,
		const BMessenger& replyTo, uint32 replyWhat);

	// org.bluez.Device1 Trusted/Blocked. Trusted lets the agent auto-answer
	// AuthorizeService for this device (see _HandleAgentMethodCall); Blocked
	// rejects incoming connections from it at the daemon level.
	status_t SetDeviceTrusted(const char* devicePath, bool trusted);
	status_t SetDeviceTrustedAsync(const char* devicePath, bool trusted,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t SetDeviceBlocked(const char* devicePath, bool blocked);
	status_t SetDeviceBlockedAsync(const char* devicePath, bool blocked,
		const BMessenger& replyTo, uint32 replyWhat);

	// Combined adapters+devices snapshot in one round trip -- what the
	// replicant's periodic poll and the preflet's initial scan both actually
	// want, avoiding two dispatch hops for one status refresh. Reply carries
	// two sub-BMessages, "adapters" and "devices", each in the same flat
	// shape as GetAdapters()/GetDevices() (adapter_count/adapter_N, ...).
	status_t GetStatusAsync(const BMessenger& replyTo, uint32 replyWhat);

	// org.bluez.Agent1 -- registered by the BluetoothStatus replicant only,
	// capability KeyboardDisplay.
	// uiHandler receives kMsgAgentRequest ("request_id", "kind" a
	// pairing_dialog_kind, "device_name", plus per-kind fields matching
	// PairingDialogWindow's request bag) and kMsgAgentCancel. Both calls run
	// the actual D-Bus work on the dispatch thread and return immediately --
	// safe to call from a window thread (AttachedToWindow/DetachedFromWindow).
	status_t RegisterAgentAsync(const BMessenger& uiHandler,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t UnregisterAgentAsync(const BMessenger& replyTo, uint32 replyWhat);

	// Delivers the user's answer for a still-open Agent1 request, completing
	// the held GDBusMethodInvocation. Safe to call from any thread (routes
	// onto the dispatch thread internally); a stale or already-answered
	// requestId is a silent no-op, matching Agent1.Cancel's idempotence
	// requirement.
	void CompleteAgentRequest(uint32 requestId, bool accepted,
		const BString& value);

	// Public only so the free dispatch-job functions in BlueZBackend.cpp
	// (the _RunOnDispatchThread jobs for RegisterAgentAsync/
	// UnregisterAgentAsync/CompleteAgentRequest) can call them, mirroring
	// how ConnectDevice() etc are public for the same reason. Not part of
	// the intended external surface -- callers outside this file want
	// RegisterAgentAsync/UnregisterAgentAsync/CompleteAgentRequest instead.
	status_t _RegisterAgent(const BMessenger& uiHandler);
	status_t _UnregisterAgent();
	void _CompleteAgentRequest(uint32 requestId, bool accepted,
		const BString& value);

	// Queues func(cookie, &reply) onto this backend's GMainContext dispatch
	// thread and returns IMMEDIATELY -- never blocks the caller. Mirrors
	// NMBackend::_RunOnDispatchThread (see its header comment for the full
	// rationale: no completion primitive, no worker thread, the looper's
	// message queue on the receiving side is the synchronisation). Public
	// only so the free functions in BlueZBackend.cpp that implement each
	// async call can name the type.
	typedef void (*DispatchFunc)(void* cookie, BMessage* reply);

	// Notifications (BMessage protocol)
	status_t StartWatching(const BMessenger& target, uint32 notificationMask);
	status_t StopWatching(const BMessenger& target);

	enum NotificationType {
		NOTIFICATION_ADAPTER_ADDED = 'BTAD',
		NOTIFICATION_ADAPTER_REMOVED = 'BTRM',
		NOTIFICATION_ADAPTER_PROPERTY_CHANGED = 'BTAP',
		NOTIFICATION_DEVICE_FOUND = 'BTDF',
		NOTIFICATION_DEVICE_CONNECTED = 'BTDC',
		NOTIFICATION_DEVICE_DISCONNECTED = 'BTDD',
		NOTIFICATION_DEVICE_PROPERTY_CHANGED = 'BTDP',
		NOTIFICATION_DEVICE_REMOVED = 'BTDR'
	};

	// Mirrors LocalDevice::{kAgentRequest, kAgentCancel} exactly -- literal
	// `what` codes posted to the Agent1 UI handler, matched on both sides.
	enum AgentMessageType {
		AGENT_REQUEST = 'BTAR',
		AGENT_CANCEL = 'BTAC'
	};

private:
	BlueZBackend();
	~BlueZBackend();

	bool _InitBlueZ();
	void _CleanupBlueZ();
	static int32 _InitThreadEntry(void* data);

	// org.bluez name watcher: recovers automatically when bluetoothd starts
	// (or restarts) after this backend's init already ran, instead of
	// GetAdapters()/GetDevices() staying empty forever. fBlueZConnection
	// itself only fails when the system bus is unreachable, which is a
	// separate, rarer condition this doesn't change.
	static gboolean _SetupBluezWatchSource(gpointer cookie);
	void _SetupBluezWatch();
	static void _OnBluezNameAppeared(GDBusConnection* connection,
		const char* name, const char* nameOwner, void* userData);
	static void _OnBluezNameVanished(GDBusConnection* connection,
		const char* name, void* userData);
	guint fBluezWatcherId;
	bool fBluezAvailable;

	status_t _RunOnDispatchThread(DispatchFunc func, void* cookie,
		const BMessenger& replyTo, uint32 replyWhat);

	// Queued _BlueZDispatchJob* (opaque here -- the struct is file-local to
	// BlueZBackend.cpp) accumulated while fMainContext doesn't exist yet,
	// i.e. before _InitBlueZ()'s one-shot init thread gets there.
	// AttachedToWindow() commonly calls RegisterAgentAsync()/StartWatching()
	// within this window; _RunOnDispatchThread() used to fail those calls
	// immediately and permanently instead of queuing them. Guarded by fLock.
	std::vector<void*> fPendingDispatchJobs;
	void _FlushPendingDispatchJobs();
	void _FailPendingDispatchJobs();

	// Re-subscribes org.bluez's PropertiesChanged/InterfacesAdded/
	// InterfacesRemoved once fBlueZConnection exists, for watchers that
	// registered (via StartWatching(), which now queues into fWatchers
	// unconditionally) before the connection was ready. Caller must hold
	// fLock; StartWatching() itself and _InitBlueZ()'s post-connection
	// replay both call this.
	void _SubscribeSignalsLocked();

	status_t _GetObjects(const char* filterInterface, const char* countField,
		const char* prefix, BMessage* outMessage);
	status_t _CallAdapterMethod(const char* adapterPath, const char* method);
	status_t _CallDeviceMethod(const char* devicePath, const char* method);
	status_t _SetAdapterProperty(const char* adapterPath,
		const char* property, bool value);
	status_t _SetDeviceProperty(const char* devicePath,
		const char* property, bool value);

	// Object-path-keyed snapshot cache: the single source of truth for
	// GetAdapters()/GetDevices(), maintained from PropertiesChanged/
	// InterfacesAdded/InterfacesRemoved so those calls are a locked memory
	// read, never IPC. fSnapshotPopulated distinguishes "never queried yet"
	// (false -- callers get an honest B_ERROR while a query is kicked off in
	// the background) from "queried, daemon currently has nothing" (true,
	// maps empty). Caller must hold fLock for all of the following.
	std::map<BString, BMessage> fAdapterSnapshot;
	std::map<BString, BMessage> fDeviceSnapshot;
	bool fSnapshotPopulated;

	// Bumped by every locked mutation of fAdapterSnapshot/fDeviceSnapshot
	// (signal callbacks, optimistic updates, clears). A snapshot query
	// captures this before issuing its GetManagedObjects call; if it no
	// longer matches when the reply lands, something newer than the query's
	// own view already landed in the cache and the query result must not
	// clobber it -- see _HandleSnapshotQueryReply.
	uint32 fSnapshotGeneration;

	// Health is distinct from fBluezAvailable (org.bluez name ownership): a
	// hung bluetoothd can own the name while never answering a call, which
	// is exactly the reported failure. Gates both whether GetAdapters()/
	// GetDevices() kick a background refresh and the backoff schedule below.
	bool fDaemonHealthy;
	bool fQueryInFlight;
	bigtime_t fBackoffUntil;
	bigtime_t fCurrentBackoffUs;

	void _ClearSnapshotLocked();
	static status_t _MessageFromObjectsLocked(
		const std::map<BString, BMessage>& objects, const char* countField,
		const char* prefix, BMessage* outMessage);

	// Kicks an async GetManagedObjects refresh if one isn't already in
	// flight and the backoff window has elapsed; always returns immediately,
	// never touches fLock across D-Bus I/O. Safe to call from any thread --
	// internally hops onto the dispatch thread, since g_dbus_connection_call()
	// delivers its callback via whatever GMainContext is thread-default at
	// the moment the call is issued, which is only fMainContext on the
	// thread that pushed it (the dispatch thread).
	void _StartSnapshotQuery();
	static gboolean _IssueSnapshotQuerySource(gpointer data);
	void _IssueSnapshotQuery(void* cookie);
	static void _OnGetManagedObjectsReply(GObject* source,
		GAsyncResult* res, gpointer userData);
	void _HandleSnapshotQueryReply(GVariant* reply, GError* error,
		void* cookie);

	// Removes the field an invalidated BlueZ property name maps to from a
	// cached entry -- the invalidated-properties half of PropertiesChanged
	// (s a{sv} as): properties BlueZ still has but chose not to send a value
	// for inline, meaning "this is now unknown", not "unchanged".
	static void _RemovePropertyFieldLocked(BMessage& entry,
		const char* propName);

	// Trusted-device lookup, now a read of fDeviceSnapshot's cached
	// "trusted" field instead of a second parallel cache. Lets the Agent1
	// handler decide "is this device already trusted?" without a synchronous
	// D-Bus call from inside its own method-call callback (that would
	// re-enter GDBus dispatch -- see the class-level warning below). Caller
	// must hold fLock.
	bool _IsDeviceCachedTrusted(const char* devicePath);

	// GDBus marshalling helpers. These call g_dbus_connection_call_sync()
	// directly on the caller's own thread rather than via
	// g_main_context_invoke -- sync GDBus calls are thread-safe and don't
	// need to run on the thread pumping fMainContext, unlike NMClient's
	// cached-property reads. fLock serializes access to fBlueZConnection.
	status_t _DoCallMethod(const char* path, const char* iface,
		const char* method, const char* paramObjectPath = NULL);
	status_t _DoSetProperty(const char* path, const char* iface,
		const char* property, bool value);

	static void _PropertiesChangedCallback(GDBusConnection* connection,
		const char* senderName, const char* objectPath,
		const char* interfaceName, const char* signalName,
		GVariant* parameters, void* userData);

	// ObjectManager.InterfacesAdded is the only signal BlueZ emits when a
	// still-unknown device is found during discovery -- PropertiesChanged
	// only fires for objects that already exist. Without this, DiscoveryAgent
	// would never see a device it has not seen before.
	static void _InterfacesAddedCallback(GDBusConnection* connection,
		const char* senderName, const char* objectPath,
		const char* interfaceName, const char* signalName,
		GVariant* parameters, void* userData);

	// ObjectManager.InterfacesRemoved -- the counterpart to InterfacesAdded,
	// fired when a device disappears (forgotten, out of range and expired
	// from BlueZ's cache). Needed for NOTIFICATION_ADAPTER_REMOVED.
	static void _InterfacesRemovedCallback(GDBusConnection* connection,
		const char* senderName, const char* objectPath,
		const char* interfaceName, const char* signalName,
		GVariant* parameters, void* userData);

	// Agent1 -- all of this runs on the dispatch thread; GDBus invokes the
	// method-call vtable callback on whatever thread pumps the connection's
	// bound GMainContext, which is fMainContext. (_RegisterAgent,
	// _UnregisterAgent and _CompleteAgentRequest are declared public above.)
	static void _AgentMethodCall(GDBusConnection* connection,
		const char* sender, const char* objectPath,
		const char* interfaceName, const char* methodName,
		GVariant* parameters, GDBusMethodInvocation* invocation,
		void* userData);
	void _HandleAgentMethodCall(const char* methodName, GVariant* parameters,
		GDBusMethodInvocation* invocation);
	void _CancelPendingAgentRequests();

	void* fBlueZConnection;
	void* fMainContext;
	void* fMainLoop;
	thread_id fDispatchThread;
	thread_id fInitThread;
	static int32 _DispatchThreadEntry(void* data);
	void _DispatchThread();

	guint fAgentRegistrationId;
	BPrivate::AuthPromptRouter fAgentRouter;

	// Multiple independent watchers (e.g. a preflet's PropertiesChanged
	// subscription and a concurrent DiscoveryAgent scan) each get their own
	// mask; a notification fans out to every entry whose mask matches. Dead
	// messengers are pruned lazily on the next fan-out that would target them
	// -- BMessenger gives no death notification, only IsValid()/SendMessage()
	// failure.
	struct Watcher {
		BMessenger messenger;
		uint32 mask;
	};
	std::vector<Watcher> fWatchers;
	void _NotifyWatchers(uint32 type, BMessage& message);

	guint fPropertiesChangedSubscriptionId;
	guint fInterfacesAddedSubscriptionId;
	guint fInterfacesRemovedSubscriptionId;

	BLocker fLock;
};


#endif // _BLUETOOTH_KIT_BLUEZ_BACKEND_H
