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

#include "TelephonyBackend.h"


// ModemManager-only enum values (MMModemLock, MMModemAccessTechnology),
// numeric values as shipped by ModemManager 1.24. The shared state,
// registration, call, SMS and field-key definitions moved to
// TelephonyBackend.h.
static const uint32 kMMLockUnknown = 0;
static const uint32 kMMLockNone = 1;
static const uint32 kMMLockSimPin = 2;
static const uint32 kMMLockSimPin2 = 3;
static const uint32 kMMLockSimPuk = 4;
static const uint32 kMMLockSimPuk2 = 5;

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


class ModemManagerBackend : public TelephonyBackend {
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

	// PUK unlock via Sim.SendPuk(puk, newPin). Reply carries "status".
	status_t UnlockSimPukAsync(const char* modemPath, const char* puk,
		const char* newPin, const BMessenger& replyTo, uint32 replyWhat);

	// Cached call objects of one modem: call_count plus call_0..call_N
	// submessages with the kMMFieldCall* fields. Snapshot-only.
	status_t GetCalls(const char* modemPath, BMessage* outCalls);

	// Cached SMS objects of one modem: sms_count plus sms_0..sms_N
	// submessages with the kMMFieldSms* fields. Snapshot-only.
	status_t GetSmsMessages(const char* modemPath, BMessage* outMessages);

	// Hold uses Voice.HoldAndAccept (MM 1.24 has no resume method).
	// Hangup with a NULL callPath hangs up everything.
	status_t DialAsync(const char* modemPath, const char* number,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t AnswerAsync(const char* callPath,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t HangupAsync(const char* modemPath, const char* callPath,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t HoldAsync(const char* modemPath,
		const BMessenger& replyTo, uint32 replyWhat);
	status_t SendDtmfAsync(const char* callPath, const char* digits,
		const BMessenger& replyTo, uint32 replyWhat);

	// SMS. Create + Send on the Sms object; progress arrives as
	// NOTIFICATION_SMS_STATE_CHANGED. Delete is Messaging.Delete.
	// Replies carry "status", "reason" and, for SendMessage,
	// kMMFieldSmsPath.
	status_t SendMessageAsync(const char* modemPath, const char* number,
		const char* text, const BMessenger& replyTo, uint32 replyWhat);
	status_t DeleteSmsAsync(const char* modemPath, const char* smsPath,
		const BMessenger& replyTo, uint32 replyWhat);

	// Asks Modem.Signal to sample SignalQuality every rateSeconds so the
	// PropertiesChanged stream carries live signal for the replicant.
	status_t SetupSignalRateAsync(const char* modemPath,
		uint32 rateSeconds, const BMessenger& replyTo, uint32 replyWhat);

	// Service availability
	bool IsServiceAvailable();

	// Queues func(cookie, &reply) onto this backend's GMainContext dispatch
	// thread and returns immediately; mirrors NMBackend/BlueZBackend's
	// DispatchFunc pattern.
	typedef void (*DispatchFunc)(void* cookie, BMessage* reply);

	// Notifications (BMessage protocol)
	status_t StartWatching(const BMessenger& target, uint32 notificationMask);
	status_t StopWatching(const BMessenger& target);

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
	// Call and SMS objects key by their own path and carry the owning
	// modem path so GetCalls()/GetSmsMessages() can filter per modem.
	std::map<BString, BMessage> fModemSnapshot;
	std::map<BString, BMessage> fCallSnapshot;
	std::map<BString, BMessage> fSmsSnapshot;
	BMessage fModemListSnapshot;
	bool fSnapshotPopulated;
};


#endif // _NETWORKKIT_MODEM_MANAGER_BACKEND_H
