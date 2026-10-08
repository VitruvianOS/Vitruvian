/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _NETWORKKIT_NM_BACKEND_H
#define _NETWORKKIT_NM_BACKEND_H


#include <SupportDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Locker.h>
#include <String.h>

#include <map>
#include <vector>

#include <AuthPromptRouter.h>


static const uint32 kNMDeviceStateActivated = 100;

static const char* const kNMFieldPath = "path";
static const char* const kNMFieldInterface = "interface";
static const char* const kNMFieldType = "type";
static const char* const kNMFieldState = "state";
static const char* const kNMFieldHWAddress = "hw_address";
static const char* const kNMFieldMTU = "mtu";
static const char* const kNMFieldDriver = "driver";
static const char* const kNMFieldManaged = "managed";

static const char* const kNMFieldGateway = "gateway";
static const char* const kNMFieldDNS = "dns";

// Live runtime addresses from NMIPConfig (D-Bus AddressData), nested as
// "address_%d" BMessages under each device-info snapshot.
static const char* const kNMFieldAddressCount = "address_count";
static const char* const kNMFieldAddressFamily = "family";
static const char* const kNMFieldAddressString = "address";
static const char* const kNMFieldAddressPrefix = "prefix";

static const char* const kNMFieldIP4Method = "ip4_method";
static const char* const kNMFieldIP4Address = "ip4_address";
static const char* const kNMFieldIP4Netmask = "ip4_netmask";
static const char* const kNMFieldIP4Gateway = "ip4_gateway";
static const char* const kNMFieldIP4DNS = "ip4_dns";

static const char* const kNMFieldNMAvailable = "nm_available";
static const char* const kNMFieldDeviceCount = "device_count";

static const char* const kNMFieldAPCount = "network_count";
static const char* const kNMFieldAPSSID = "ssid";
static const char* const kNMFieldAPStrength = "strength";
static const char* const kNMFieldAPSecured = "secured";
static const char* const kNMFieldAPConnected = "connected";

static const char* const kNMFieldVPNCount = "vpn_count";
static const char* const kNMFieldVPNName = "name";
static const char* const kNMFieldVPNPath = "path";
static const char* const kNMFieldVPNConnected = "connected";
static const char* const kNMFieldVPNActivating = "activating";
static const char* const kNMFieldVPNType = "type";
static const char* const kNMFieldVPNServer = "server";
static const char* const kNMFieldVPNUser = "user";
static const char* const kNMFieldVPNAutoconnect = "autoconnect";
// While connected: one string per address ("10.8.0.2/24") and DNS server.
static const char* const kNMFieldVPNAddress = "address";
static const char* const kNMFieldVPNDNS = "dns";

static const char* const kNMFieldSavedCount = "saved_count";
static const char* const kNMFieldSavedSSID = "ssid";
static const char* const kNMFieldSavedPath = "path";
static const char* const kNMFieldSavedAutoconnect = "autoconnect";
static const char* const kNMFieldSavedPriority = "priority";

static const char* const kNMFieldProfileCount = "profile_count";
static const char* const kNMFieldProfileID = "id";
static const char* const kNMFieldProfilePath = "path";
static const char* const kNMFieldProfileActive = "active";

// NMDeviceWifiCapabilities: whether AP mode is possible.
static const char* const kNMFieldWiFiCaps = "wifi_caps";

// Per-device snapshot: active AP strength in percent; the Deskbar tray reads it.
static const char* const kNMFieldDeviceSignalStrength = "signal_strength";

// Hotspot (WiFi AP mode) reply fields.
static const char* const kNMFieldHotspotActive = "hotspot_active";
static const char* const kNMFieldHotspotUUID = "hotspot_uuid";
static const char* const kNMFieldHotspotSSID = "hotspot_ssid";
static const char* const kNMFieldHotspotPassword = "hotspot_password";
static const char* const kNMFieldHotspotConnectionPath
	= "hotspot_connection_path";
static const char* const kNMFieldHotspotWillDisconnect
	= "hotspot_will_disconnect";
static const char* const kNMFieldHotspotCanStart = "hotspot_can_start";

// Modem device snapshot extras: NM reports GSM/CDMA support as capability
// bits; the preflet needs a boolean, not the raw mask.
static const char* const kNMFieldModemCaps = "modem_caps";
static const char* const kNMFieldModemIsGSM = "modem_is_gsm";
static const char* const kNMFieldModemIsCDMA = "modem_is_cdma";

// Mobile broadband connection profile fields (NMSettingGsm / NMSettingCdma).
static const char* const kNMFieldMobileName = "mobile_name";
static const char* const kNMFieldMobileAPN = "mobile_apn";
static const char* const kNMFieldMobileUser = "mobile_user";
static const char* const kNMFieldMobilePassword = "mobile_password";
static const char* const kNMFieldMobileNumber = "mobile_number";
static const char* const kNMFieldMobileConnected = "mobile_connected";
static const char* const kNMFieldMobileHasProfile = "mobile_has_profile";

typedef unsigned int guint;
typedef unsigned long gulong;
typedef int gboolean;
typedef void* gpointer;
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GVariant GVariant;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef struct _GObject GObject;
typedef struct _GParamSpec GParamSpec;


class NMBackend {
public:
	static NMBackend* Instance();

	status_t _ResolveDevicePath(const char* interfaceName, BString& outPath);

	status_t GetDevices(BMessage* outDevices);
	status_t GetDevicesAsync(const BMessenger& replyTo, uint32 replyWhat);
	status_t GetDeviceInfo(const char* devicePath, BMessage* outInfo);
	status_t GetDeviceInfoAsync(const char* devicePath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Live-writable global toggles -- simple atomic property writes,
	// no Apply/profile transaction involved.
	status_t SetNetworkingEnabled(bool enabled);
	status_t SetWirelessEnabled(bool enabled);
	bool IsNetworkingEnabled();
	bool IsWirelessEnabled();

	// Queues func(cookie, &reply) onto this backend's GMainContext dispatch
	// thread and returns immediately; never blocks the caller. func must
	// free `cookie` itself before returning (own it start to finish). Once
	// func returns, a BMessage with `what` = replyWhat carrying whatever func
	// wrote into it is posted to replyTo. There is no completion primitive
	// (no sem_id, no BLocker used as a condition variable, no worker
	// thread) -- the looper's message queue on the receiving side is the
	// synchronisation. A BLocker cannot serve as the completion signal here:
	// it is a recursive mutex keyed to the owning thread, not a condition
	// variable. Public only so the free functions in
	// NMBackend.cpp that implement each async call can name the type;
	// callers outside this file have no way to construct a DispatchFunc
	// cookie and should use the typed *Async() wrappers (GetDevicesAsync())
	// instead.
	typedef void (*DispatchFunc)(void* cookie, BMessage* reply);
	
	// Connection management. Both dispatch nm_client_activate_connection_async()
	// / nm_device_disconnect_async() on the dispatch thread and return
	// immediately once the request is queued -- B_OK here means "dispatched",
	// not "connected". Real outcome (including WHY a connect failed) is
	// reported via NOTIFICATION_CONNECTION_STATUS_CHANGED with a "reason"
	// string field set only on failure; success carries no "reason".
	status_t ConnectDevice(const char* devicePath);
	status_t DisconnectDevice(const char* devicePath);

	// WiFi operations
	//
	// ScanWiFiNetworks() is synchronous but never blocks on D-Bus: it reads
	// the AP snapshot cache (fWiFiSnapshot, refreshed on the dispatch thread
	// from NMDeviceWifi's access-point-added/removed signals, the same
	// pattern as fDeviceSnapshot) and, as a side effect, fires an
	// asynchronous rescan request so the next call sees fresher results.
	status_t ScanWiFiNetworks(const char* devicePath, BMessage* outNetworks);
	// Same snapshot read, without the rescan side effect.
	status_t GetWiFiNetworks(const char* devicePath, BMessage* outNetworks);
	// Fire-and-forget: dispatches the same add-and-activate path as
	// ConnectToWiFiAsync() but with no reply target, for BNetworkDevice's
	// synchronous JoinNetwork() contract. Real completion is not observable
	// from the return value -- callers wanting a real answer must use
	// ConnectToWiFiAsync().
	status_t ConnectToWiFi(const char* devicePath, const char* ssid,
		const char* password, const char* security);
	// Deletes every saved NM connection profile whose 802-11-wireless SSID
	// matches. Fire-and-forget for the same reason as ConnectToWiFi() above.
	status_t ForgetWiFiNetwork(const char* ssid);

	// Saved-network management: NM's stored 802-11-wireless connection
	// profiles, independent of whether their AP is currently in range.
	//
	// GetSavedWiFiNetworksAsync() enumerates every connection profile whose
	// type is NM_SETTING_WIRELESS_SETTING_NAME, unsorted -- callers wanting
	// priority order sort by kNMFieldSavedPriority themselves. Async:
	// dispatches and returns immediately; reply carries the kNMFieldSaved*
	// shape and no "status" field (unlike the write calls below), mirroring
	// GetDevicesAsync()/GetVPNConnections().
	status_t GetSavedWiFiNetworksAsync(const BMessenger& replyTo,
		uint32 replyWhat);

	// Enumerates every saved connection profile applicable to devicePath
	// (nm_device_filter_connections() against NMClient's full connection
	// list -- not activation history, so a never-yet-activated profile still
	// shows up). Reply carries the kNMFieldProfile* shape above and no
	// "status" field, mirroring GetSavedWiFiNetworksAsync(). Async.
	status_t GetDeviceConnectionProfilesAsync(const char* devicePath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Reads connectionPath's own NMSettingIPConfig (not the device's active
	// one) into the kNMFieldIP4* fields, for StaticIPView to reseed itself
	// when the user switches the profile chooser. Reply carries "status" and,
	// on B_ENTRY_NOT_FOUND, no reason beyond that. Async.
	status_t GetConnectionIP4ConfigAsync(const char* connectionPath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Deletes exactly the profile at connectionPath (unlike ForgetWiFiNetwork(),
	// which matches by SSID and can hit several profiles at once). Async;
	// reply carries "status" and, on failure, "reason".
	status_t ForgetSavedNetworkAsync(const char* connectionPath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Live-toggle-style writes to an existing saved profile's
	// NMSettingConnection, committed via
	// nm_remote_connection_commit_changes_async(). Async; reply carries
	// "status" and, on failure, "reason".
	status_t SetWiFiAutoconnectAsync(const char* connectionPath,
		bool autoconnect, const BMessenger& replyTo, uint32 replyWhat);
	status_t SetWiFiPriorityAsync(const char* connectionPath, int32 priority,
		const BMessenger& replyTo, uint32 replyWhat);

	// Minimum connect slice: builds an NMConnection
	// (NMSettingWireless + NMSettingWirelessSecurity when a password/security
	// is given) and calls nm_client_add_and_activate_connection_async().
	// Fires immediately and returns; reply carries "status" (status_t) and,
	// on failure, "reason" (BString, human-readable). remember=true sets the
	// connection's autoconnect (the "Remember this network" checkbox).
	// Full saved-network management (editing/forgetting an existing
	// profile, static IP, etc) is not implemented -- this only covers the
	// create-and-join path an agent-driven reconnect or a fresh join need.
	// security: "none", "wpa" (default), "sae" or "wep". hidden marks a
	// network that does not broadcast its name; a hidden join never writes
	// a secret into the profile (key-mgmt only), so NM calls the SecretAgent
	// exactly as for a scanned network. password is still honored for the
	// non-hidden callers (ConnectToWiFi(), NetworkStatus).
	status_t ConnectToWiFiAsync(const char* devicePath, const char* ssid,
		const char* password, const char* security, bool remember,
		const BMessenger& replyTo, uint32 replyWhat, bool hidden = false);
	
	// IPv4 configuration write mode -- mirrors NM_SETTING_IP4_CONFIG_METHOD_*
	// at the public-API boundary so UI code doesn't need libnm headers.
	enum IP4ConfigMode {
		IP4_CONFIG_AUTO = 0,
		IP4_CONFIG_MANUAL,
		IP4_CONFIG_DISABLED
	};

	// Writes IPv4 configuration onto connectionPath -- any saved profile, not
	// only whichever one is active -- and commits it to disk via
	// nm_remote_connection_commit_changes_async(). Callers resolve which
	// profile via GetDeviceConnectionProfilesAsync(); the "no active
	// connection" refusal that used to live here is gone because the caller
	// now names the profile directly. A path that doesn't resolve to a saved
	// connection replies B_ENTRY_NOT_FOUND. dns is a comma-separated list,
	// may be empty; address/netmask/gateway are ignored unless mode is
	// IP4_CONFIG_MANUAL. Async: dispatches and returns immediately; reply
	// carries "status" (status_t) and, on failure, "reason" (BString).
	status_t SetStaticIPConfigAsync(const char* connectionPath,
		IP4ConfigMode mode, const char* address, const char* netmask,
		const char* gateway, const char* dns, const BMessenger& replyTo,
		uint32 replyWhat);

	// Creates a new saved connection profile for a wired (Ethernet) device
	// and saves it to disk via nm_client_add_connection_async() -- it is
	// not activated, so an existing link stays up on whatever profile
	// already brought it up. Wireless devices are refused
	// (B_NOT_SUPPORTED): ConnectToWiFiAsync() is the only profile-creation
	// path for WiFi, because a profile made here would have no SSID/security
	// and the no-password-field rule means this call can never collect one.
	// The new profile gets DHCP (NMSettingIPConfig method "auto") and is
	// bound to the device via NM_SETTING_CONNECTION_INTERFACE_NAME -- the
	// device's nm_device_get_iface(), not its MAC, matching how every other
	// device-scoped lookup in this file already keys off devicePath/iface.
	// name becomes the connection's id verbatim (NM ids need not be unique;
	// the caller is responsible for asking the user and warning on
	// collision if it cares). Async; reply carries "status" and, on B_OK,
	// kNMFieldProfilePath/kNMFieldProfileID for the caller to select
	// immediately; on failure, "reason".
	status_t CreateWiredConnectionProfileAsync(const char* devicePath,
		const char* name, const BMessenger& replyTo, uint32 replyWhat);

	// Mobile broadband (GSM/CDMA) connections; signal, operator and SIM PIN
	// live in ModemManagerBackend. remember=true saves the profile.
	// Reply carries "status" and, on failure, "reason".
	status_t ConnectMobileAsync(const char* devicePath, const char* name,
		const char* apn, const char* user, const char* password,
		const char* number, bool remember,
		const BMessenger& replyTo, uint32 replyWhat);

	// Same device disconnect path as DisconnectDevice(); separate name so
	// the preflet's mobile pane cannot be confused with a wired/WiFi call.
	// Reply carries "status" and, on failure, "reason".
	status_t DisconnectMobileAsync(const char* devicePath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Reads the device's active or saved GSM/CDMA profile into the
	// kNMFieldMobile* fields for the preflet form. No profile yields
	// kNMFieldMobileHasProfile=false. Async; reply carries "status".
	status_t GetMobileConnectionAsync(const char* devicePath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Wi-Fi hotspot: one saved WPA2 profile keyed by profileUUID, reused
	// on every start. Reply carries the kNMFieldHotspot* fields or "reason".
	status_t StartHotspotAsync(const char* devicePath, const char* profileUUID,
		const char* ssid, const char* password, const BMessenger& replyTo,
		uint32 replyWhat);

	// Deactivates the hotspot profile without deleting it. Async; reply
	// carries "status" and, on failure, "reason".
	status_t StopHotspotAsync(const char* profileUUID,
		const BMessenger& replyTo, uint32 replyWhat);

	// Hotspot state for the preflet; the password is filled whenever the
	// saved profile exists, so the setup dialog can default to it.
	status_t GetHotspotStateAsync(const char* devicePath,
		const char* profileUUID, const BMessenger& replyTo, uint32 replyWhat);

	// VPN operations
	status_t GetVPNConnections(BMessage* outVPNs);
	status_t ConnectVPN(const char* connectionPath);
	status_t DisconnectVPN(const char* connectionPath);

	// Saves (does not activate) a profile imported by the VPN plugins or,
	// failing that, from a WireGuard .conf. Reply: "status"; on success
	// kNMFieldProfilePath/kNMFieldProfileID, else "reason" and
	// "supported_formats" (a BMessage of "format" strings).
	status_t ImportVPNAsync(const char* filePath, const BMessenger& replyTo,
		uint32 replyWhat);

	// Reply: "status", and "reason" on failure.
	status_t RemoveVPNAsync(const char* connectionPath,
		const BMessenger& replyTo, uint32 replyWhat);
	
	// Notifications (BMessage protocol)
	status_t StartWatching(const BMessenger& target, uint32 notificationMask);
	status_t StopWatching(const BMessenger& target);

	enum NotificationType {
		NOTIFICATION_DEVICE_ADDED = 'DVAD',
		NOTIFICATION_DEVICE_REMOVED = 'DVRM',
		NOTIFICATION_DEVICE_STATE_CHANGED = 'DVSC',
		NOTIFICATION_DEVICE_IP_CHANGED = 'DIPC',
		NOTIFICATION_WIFI_NETWORK_FOUND = 'WNFD',
		NOTIFICATION_CONNECTION_STATUS_CHANGED = 'COSC',
		NOTIFICATION_SIGNAL_STRENGTH_CHANGED = 'SSCH'
	};

	// org.freedesktop.NetworkManager.SecretAgent -- registered by the
	// NetworkStatus replicant only: it owns the WPA/802.1x/VPN prompt UI.
	// uiHandler receives SECRET_REQUEST ("request_id", "kind" a
	// secret_dialog_kind mirrored below, "ssid", "request_new",
	// "method"/"missing_file" as applicable, "connection_name"/
	// "secret_keys"/"secret_messages" for vpn and wireguard -- shaped
	// exactly like SecretDialogWindow's request bag) and SECRET_CANCEL.
	// Both calls run the D-Bus work on the dispatch thread and return
	// immediately -- safe from a window thread
	// (AttachedToWindow/DetachedFromWindow).
	status_t RegisterSecretAgentAsync(const BMessenger& uiHandler,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t UnregisterSecretAgentAsync(const BMessenger& replyTo,
		uint32 replyWhat);

	// Delivers the user's answer for a still-open GetSecrets request,
	// completing the held GDBusMethodInvocation. Safe from any thread; a
	// stale or already-answered/cancelled requestId is a silent no-op.
	// remember maps to the connection's autoconnect flag (not for VPNs).
	// secrets, for vpn and wireguard, holds secret_key/secret_value pairs
	// and replaces password.
	void CompleteSecretRequest(uint32 requestId, bool accepted,
		const BString& password, const BString& identity, bool remember,
		const BMessage* secrets = NULL);

	// Mirrors secret_dialog_kind in
	// src/apps/networkstatus/SecretDialogWindow.h exactly (0..6, same
	// order) -- kept as a plain int32 here rather than an #include because
	// kit code must not depend on an app header; only the numeric
	// convention is shared, carried across in the "kind" BMessage field.
	enum SecretDialogKind {
		SECRET_KIND_WPA_PSK = 0,
		SECRET_KIND_WEP,
		SECRET_KIND_ENTERPRISE,
		SECRET_KIND_WIRED_8021X,
		SECRET_KIND_MISSING_CERTIFICATE,
		SECRET_KIND_VPN,
		SECRET_KIND_WIREGUARD
	};

	// Mirrors NetworkStatusView's {kMsgAgentRequest,kMsgAgentCancel}-style
	// literal `what` codes -- BlueZBackend::AgentMessageType is the
	// precedent for this being a plain enum here, matched by name on the UI
	// side.
	enum SecretAgentMessageType {
		SECRET_REQUEST = 'NSAR',
		SECRET_CANCEL = 'NSAC'
	};

	// Public only so the free dispatch-job functions in NMBackend.cpp can
	// call them, mirroring BlueZBackend's equivalent methods. Not part of
	// the intended external surface.
	status_t _RegisterSecretAgent(const BMessenger& uiHandler);
	status_t _UnregisterSecretAgent();
	void _CompleteSecretRequest(uint32 requestId, bool accepted,
		const BString& password, const BString& identity, bool remember,
		const BMessage* secrets);

	// Public for the same reason as the SecretAgent helpers above: the
	// dispatch-job completion callbacks for ConnectDevice/DisconnectDevice/
	// ConnectVPN/DisconnectVPN are free functions (not members) and need to
	// refresh the snapshot + fan out the outcome once their async libnm call
	// finishes.
	void _RefreshSnapshotAndNotify(uint32 type, BMessage& message);

private:
	NMBackend();
	~NMBackend();

	bool _InitLibNM();
	void _CleanupLibNM();

	// Recovery when NetworkManager isn't running yet (or has been stopped):
	// a org.freedesktop.NetworkManager bus-name watcher, set up once on the
	// dispatch thread regardless of whether the initial nm_client_new()
	// succeeded, so _InitLibNM() failing is no longer permanent. Without
	// this every method stayed B_ERROR forever until the app restarted.
	static gboolean _SetupNMWatchSource(gpointer cookie);
	void _SetupNMWatch();
	static void _OnNMNameAppeared(GDBusConnection* connection,
		const char* name, const char* nameOwner, void* userData);
	static void _OnNMNameVanished(GDBusConnection* connection,
		const char* name, void* userData);
	void _TryCreateNMClient();
	void _HandleNMVanished();
	guint fNMWatcherId;

	status_t _RunOnDispatchThread(DispatchFunc func, void* cookie,
		const BMessenger& replyTo, uint32 replyWhat);

	void* fNMClient;
	void* fMainContext;
	void* fMainLoop;
	thread_id fDispatchThread;
	static int32 _DispatchThreadEntry(void* data);
	void _DispatchThread();

	// SecretAgent -- exported on the same GDBusConnection NMClient already
	// uses (nm_client_get_dbus_connection()), at the fixed well-known path
	// NM requires every secret agent to use. All of this runs on the
	// dispatch thread; GDBus invokes the method-call vtable callback on
	// whatever thread pumps the connection's bound GMainContext, which is
	// fMainContext.
	static void _SecretAgentMethodCall(GDBusConnection* connection,
		const char* sender, const char* objectPath,
		const char* interfaceName, const char* methodName,
		GVariant* parameters, GDBusMethodInvocation* invocation,
		void* userData);
	void _HandleGetSecrets(GVariant* parameters,
		GDBusMethodInvocation* invocation);
	void _HandleCancelGetSecrets(GVariant* parameters,
		GDBusMethodInvocation* invocation);
	void _CancelPendingSecretRequest();

	// #pragma mark - Signal wiring / snapshot cache (all dispatch-thread only
	// except where noted)

	// NMClient was created under push_thread_default(fMainContext), so every
	// g_signal_connect against it or its devices must happen on the
	// dispatch thread -- signals it emits arrive there, and GObject signal
	// APIs are not otherwise thread-safe against concurrent connect/
	// disconnect from another thread.
	static gboolean _ConnectClientSignalsSource(gpointer cookie);
	void _ConnectClientSignals();

	static void _OnDeviceAdded(void* client, void* device, void* userData);
	static void _OnDeviceRemoved(void* client, void* device, void* userData);
	static void _OnDeviceStateNotify(GObject* device, GParamSpec* pspec,
		void* userData);
	static void _OnDeviceIPConfigNotify(GObject* device, GParamSpec* pspec,
		void* userData);
	static void _OnActiveConnectionNotify(GObject* client, GParamSpec* pspec,
		void* userData);
	static void _OnActiveAPStrengthNotify(GObject* ap, GParamSpec* pspec,
		void* userData);

	void _HandleDeviceAdded(void* device);
	void _HandleDeviceRemoved(void* device);
	void _HandleDeviceStateChanged(void* device);
	void _HandleDeviceIPConfigChanged(void* device);
	void _HandleActiveConnectionChanged();
	void _HandleAPStrengthChanged(void* ap);

	// Re-resolves which NMAccessPoint (if any) is "the active one" -- the
	// first Wi-Fi device's active AP -- and moves the notify::strength
	// subscription there. Dispatch thread only.
	void _UpdateActiveAPWatch();

	// Rebuild fWiFiSnapshot from libnm; state and active-connection changes
	// move the connected flag without an access-point-added signal.
	void _RefreshWiFiSnapshots();

	// Fans a notification out to every watcher whose mask includes `type`,
	// pruning dead messengers. Caller must hold fLock -- mirrors
	// BlueZBackend::_NotifyWatchers.
	void _NotifyWatchers(uint32 type, BMessage& message);

	// device D-Bus path -> notify::state handler id, so device-removed can
	// disconnect exactly the handler device-added attached.
	std::map<BString, gulong> fDeviceStateHandlers;
	// Same for notify::ip4-config and notify::ip6-config: IP config can land without a state transition.
	std::map<BString, gulong> fDeviceIP4Handlers;
	std::map<BString, gulong> fDeviceIP6Handlers;

	gulong fDeviceAddedHandlerId;
	gulong fDeviceRemovedHandlerId;
	gulong fActiveConnectionHandlerId;

	void* fActiveAP;
	gulong fActiveAPStrengthHandlerId;

	// Snapshot cache: the dispatch thread is the only writer (from signal
	// callbacks), guarded by fLock so GetDevices()/GetDeviceInfo() can copy
	// it from any thread without a cross-thread wait.
	BMessage fDeviceSnapshot;

	// Per-WiFi-device AP snapshot: devicePath -> BMessage shaped per the
	// kNMFieldAP* contract. Dispatch thread is the only writer (from
	// access-point-added/removed signals and the initial fill when a WiFi
	// device is seen); ScanWiFiNetworks() reads it under fLock from any
	// thread, mirroring fDeviceSnapshot.
	std::map<BString, BMessage> fWiFiSnapshot;
	std::map<BString, gulong> fAPAddedHandlers;
	std::map<BString, gulong> fAPRemovedHandlers;

	void _RefreshWiFiSnapshot(void* wifiDevice, const BString& devicePath);

	static void _OnAccessPointAdded(void* wifiDevice, void* ap,
		void* userData);
	static void _OnAccessPointRemoved(void* wifiDevice, void* ap,
		void* userData);
	void _HandleAccessPointsChanged(void* wifiDeviceRaw,
		const BString& devicePath);

	// VPN snapshot (kNMFieldVPN* shape), refreshed alongside fDeviceSnapshot
	// on every device/active-connection event -- see _RefreshSnapshotAndNotify.
	BMessage fVPNSnapshot;

	// Multiple independent watchers, mirrors BlueZBackend::fWatchers/Watcher
	// exactly -- see its header comment for the pruning rationale.
	struct Watcher {
		BMessenger messenger;
		uint32 mask;
	};
	std::vector<Watcher> fWatchers;

	guint fSecretAgentRegistrationId;
	BPrivate::AuthPromptRouter fSecretRouter;

	// At most one live GetSecrets request tracked at a time: a second
	// GetSecrets cancels the first. Enough state for CancelGetSecrets
	// (which only carries connection_path/setting_name, not a request id)
	// to find and cancel it.
	uint32 fPendingSecretRequestId;
	BString fPendingConnectionPath;
	BString fPendingSettingName;

	BLocker fLock;
};


#endif // _NETWORKKIT_NM_BACKEND_H