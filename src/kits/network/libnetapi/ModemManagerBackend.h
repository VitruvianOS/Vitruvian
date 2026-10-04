/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _NETWORKKIT_MODEM_MANAGER_BACKEND_H
#define _NETWORKKIT_MODEM_MANAGER_BACKEND_H


#include <SupportDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Locker.h>
#include <String.h>

#include <map>
#include <vector>


// BMessage field keys for modem information.
static const char* const kMMFieldModemPath = "modem_path";
static const char* const kMMFieldModemState = "state";
static const char* const kMMFieldModemStateReason = "state_reason";
static const char* const kMMFieldModemEquipmentId = "equipment_id";
static const char* const kMMFieldModemSimPath = "sim_path";
static const char* const kMMFieldModemOperatorName = "operator_name";
static const char* const kMMFieldModemSignalQuality = "signal_quality";
static const char* const kMMFieldModemSignalBars = "signal_bars";
static const char* const kMMFieldModemEnabled = "enabled";
static const char* const kMMFieldModemCount = "modem_count";
static const char* const kMMFieldMMAvailable = "mm_available";

// SIM lock state from the Modem UnlockRequired property (MMModemLock).
static const char* const kMMFieldSimPinRequired = "sim_pin_required";
static const char* const kMMFieldSimPukRequired = "sim_puk_required";
static const char* const kMMFieldSimUnlockRequired = "sim_unlock_required";
static const char* const kMMFieldSimUnlockRetries = "sim_unlock_retries";
static const char* const kMMFieldSimStatus = "sim_status";
static const char* const kMMFieldSimLocked = "sim_locked";


// Opaque GLib/GDBus forward declarations so this header doesn't force
// gio/glib include paths onto every consumer; the .cpp includes
// <gio/gio.h> for the real definitions.
typedef unsigned int guint;
typedef unsigned long gulong;
typedef int gboolean;
typedef void* gpointer;
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GVariant GVariant;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef struct _GObject GObject;
typedef struct _GParamSpec GParamSpec;
typedef struct _GAsyncResult GAsyncResult;


class ModemManagerBackend {
public:
	static ModemManagerBackend* Instance();

	// Modem enumeration
	status_t GetModems(BMessage* outModems);
	status_t GetModemsAsync(const BMessenger& replyTo, uint32 replyWhat);
	status_t GetModemInfo(const char* modemPath, BMessage* outInfo);
	status_t GetModemInfoAsync(const char* modemPath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Modem control
	status_t EnableModem(const char* modemPath, bool enabled);
	status_t EnableModemAsync(const char* modemPath, bool enabled,
		const BMessenger& replyTo, uint32 replyWhat);

	// Signal quality query
	status_t GetSignalQuality(const char* modemPath,
		BMessage* outSignal);
	status_t GetSignalQualityAsync(const char* modemPath,
		const BMessenger& replyTo, uint32 replyWhat);

	// SIM lock state from the Modem UnlockRequired/UnlockRetries properties.
	// Reply carries kMMFieldSim* plus "status".
	status_t GetSimStatus(const char* modemPath, BMessage* outSim);
	status_t GetSimStatusAsync(const char* modemPath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Sends the PIN via org.freedesktop.ModemManager1.Sim.SendPin on the
	// SIM object (Modem.Sim path). Reply carries "status" and, on failure,
	// "reason".
	status_t UnlockSim(const char* modemPath, const char* pin);
	status_t UnlockSimAsync(const char* modemPath, const char* pin,
		const BMessenger& replyTo, uint32 replyWhat);

	// Service availability
	bool IsServiceAvailable();

	// Queues func(cookie, &reply) onto this backend's GMainContext dispatch
	// thread and returns immediately; mirrors NMBackend/BlueZBackend's
	// DispatchFunc pattern.
	typedef void (*DispatchFunc)(void* cookie, BMessage* reply);

	// Notifications (BMessage protocol)
	status_t StartWatching(const BMessenger& target, uint32 notificationMask);
	status_t StopWatching(const BMessenger& target);

	enum NotificationType {
		NOTIFICATION_MODEM_ADDED = 'MMAD',
		NOTIFICATION_MODEM_REMOVED = 'MMRM',
		NOTIFICATION_MODEM_STATE_CHANGED = 'MMSC',
		NOTIFICATION_MODEM_SIGNAL_CHANGED = 'MMSI',
		NOTIFICATION_SERVICE_APPEARED = 'MMSA',
		NOTIFICATION_SERVICE_VANISHED = 'MMSV'
	};

	// Public only so the free dispatch-job functions in ModemManagerBackend.cpp
	// can call them, mirroring NMBackend/BlueZBackend's equivalent methods.
	void _RefreshSnapshot();
	void _NotifyWatchers(uint32 type, BMessage& message);

	// Multiple independent watchers (mirrors NMBackend::fWatchers/Watcher)
	struct Watcher {
		BMessenger messenger;
		uint32 mask;
	};
	std::vector<Watcher> fWatchers;

	BLocker fLock;

private:
	ModemManagerBackend();
	~ModemManagerBackend();

	bool _InitModemManager();
	void _CleanupModemManager();

	status_t _RunOnDispatchThread(DispatchFunc func, void* cookie,
		const BMessenger& replyTo, uint32 replyWhat);

	// org.freedesktop.ModemManager1 name watcher; recovers when MM
	// starts (or restarts) after this backend's init already ran.
	static gboolean _SetupMMWatchSource(gpointer cookie);
	void _SetupMMWatch();
	static void _OnMMNameAppeared(GDBusConnection* connection,
		const char* name, const char* nameOwner, void* userData);
	static void _OnMMNameVanished(GDBusConnection* connection,
		const char* name, void* userData);
	guint fMMWatcherId;

	void* fDBusConnection;
	void* fMainContext;
	void* fMainLoop;
	thread_id fDispatchThread;
	static int32 _DispatchThreadEntry(void* data);
	void _DispatchThread();

	// ObjectManager signals
	static void _PropertiesChangedCallback(GDBusConnection* connection,
		const char* senderName, const char* objectPath,
		const char* interfaceName, const char* signalName,
		GVariant* parameters, void* userData);
	static void _InterfacesAddedCallback(GDBusConnection* connection,
		const char* senderName, const char* objectPath,
		const char* interfaceName, const char* signalName,
		GVariant* parameters, void* userData);
	static void _InterfacesRemovedCallback(GDBusConnection* connection,
		const char* senderName, const char* objectPath,
		const char* interfaceName, const char* signalName,
		GVariant* parameters, void* userData);

	guint fPropertiesChangedSubscriptionId;
	guint fInterfacesAddedSubscriptionId;
	guint fInterfacesRemovedSubscriptionId;

	// Snapshot cache: dispatch thread is the only writer (from signal
	// callbacks); GetModems()/GetModemInfo() copy it under fLock.
	std::map<BString, BMessage> fModemSnapshot;
	BMessage fModemListSnapshot;
	bool fSnapshotPopulated;
};


#endif // _NETWORKKIT_MODEM_MANAGER_BACKEND_H
