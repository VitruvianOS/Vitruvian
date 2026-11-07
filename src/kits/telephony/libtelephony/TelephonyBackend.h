/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _TELEPHONY_BACKEND_H
#define _TELEPHONY_BACKEND_H


#include <Messenger.h>
#include <SupportDefs.h>

class BMessage;


// BMessage field keys shared by every telephony backend. Both the
// ModemManager backend and the SIP backend fill snapshots and
// notifications with these exact keys, so BModem and BTelephonyRoster
// read either line type without knowing which backend produced it.
static const char* const kMMFieldModemPath = "modem_path";
static const char* const kMMFieldModemState = "state";
static const char* const kMMFieldModemStateReason = "state_reason";
static const char* const kMMFieldModemEquipmentId = "equipment_id";
static const char* const kMMFieldModemSimPath = "sim_path";
static const char* const kMMFieldModemOperatorName = "operator_name";
static const char* const kMMFieldModemOperatorCode = "operator_code";
static const char* const kMMFieldModemManufacturer = "manufacturer";
static const char* const kMMFieldModemModel = "model";
static const char* const kMMFieldModemSignalQuality = "signal_quality";
static const char* const kMMFieldModemSignalBars = "signal_bars";
static const char* const kMMFieldModemEnabled = "enabled";
static const char* const kMMFieldModemCount = "modem_count";
static const char* const kMMFieldMMAvailable = "mm_available";

// 3GPP registration (Modem3gpp.RegistrationState plus the derived
// access-technology class from Modem.AccessTechnologies). SIP accounts
// reuse the registration keys: registered maps to kMMRegHome.
static const char* const kMMFieldModemRegistrationState
	= "registration_state";
static const char* const kMMFieldModemRegistrationName
	= "registration_name";
static const char* const kMMFieldModemIsRoaming = "is_roaming";
static const char* const kMMFieldModemAccessTechMask = "access_tech_mask";
static const char* const kMMFieldModemAccessType = "access_type";

// Call objects (org.freedesktop.ModemManager1.Call); SIP calls carry
// the same fields with a "sip-call:" path.
static const char* const kMMFieldCallPath = "call_path";
static const char* const kMMFieldCallModemPath = "call_modem_path";
static const char* const kMMFieldCallState = "call_state";
static const char* const kMMFieldCallStateName = "call_state_name";
static const char* const kMMFieldCallNumber = "call_number";
static const char* const kMMFieldCallDirection = "call_direction";
static const char* const kMMFieldCallDirectionName = "call_direction_name";
static const char* const kMMFieldCallReason = "call_reason";
static const char* const kMMFieldCallCount = "call_count";

// SMS objects; SIP MESSAGE traffic is reported with the same fields
// and a "sip-msg:" path so the Messages app handles both.
static const char* const kMMFieldSmsPath = "sms_path";
static const char* const kMMFieldSmsModemPath = "sms_modem_path";
static const char* const kMMFieldSmsNumber = "sms_number";
static const char* const kMMFieldSmsText = "sms_text";
static const char* const kMMFieldSmsState = "sms_state";
static const char* const kMMFieldSmsStateName = "sms_state_name";
static const char* const kMMFieldSmsTimestamp = "sms_timestamp";
static const char* const kMMFieldSmsDeliveryState = "sms_delivery_state";
static const char* const kMMFieldSmsPduType = "sms_pdu_type";
static const char* const kMMFieldSmsReceived = "sms_received";
static const char* const kMMFieldSmsPartCount = "sms_part_count";
static const char* const kMMFieldSmsCount = "sms_count";


// Value constants shared by the seam. Numeric values mirror the
// ModemManager enums so a raw answer can be shown untranslated.
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

static const uint32 kMMCallStateUnknown = 0;
static const uint32 kMMCallStateDialing = 1;
static const uint32 kMMCallStateRingingOut = 2;
static const uint32 kMMCallStateRingingIn = 3;
static const uint32 kMMCallStateActive = 4;
static const uint32 kMMCallStateHeld = 5;
static const uint32 kMMCallStateWaiting = 6;
static const uint32 kMMCallStateTerminated = 7;

static const uint32 kMMCallDirectionUnknown = 0;
static const uint32 kMMCallDirectionIncoming = 1;
static const uint32 kMMCallDirectionOutgoing = 2;

static const uint32 kMMSmsStateUnknown = 0;
static const uint32 kMMSmsStateStored = 1;
static const uint32 kMMSmsStateReceiving = 2;
static const uint32 kMMSmsStateReceived = 3;
static const uint32 kMMSmsStateSending = 4;
static const uint32 kMMSmsStateSent = 5;

static const uint32 kMMSmsPduTypeDeliver = 1;

// SIM lock state from the Modem UnlockRequired property (MMModemLock);
// SIP lines answer false / zero for every query.
static const char* const kMMFieldSimPinRequired = "sim_pin_required";
static const char* const kMMFieldSimPukRequired = "sim_puk_required";
static const char* const kMMFieldSimUnlockRequired = "sim_unlock_required";
static const char* const kMMFieldSimUnlockRetries = "sim_unlock_retries";
static const char* const kMMFieldSimStatus = "sim_status";
static const char* const kMMFieldSimLocked = "sim_locked";


// The seam every telephony backend (ModemManager, SIP) implements; BTelephonyRoster
// fans notifications out from all of them.
class TelephonyBackend {
public:
	virtual						~TelephonyBackend() {}

	// Modem/line enumeration: modem_count plus modem_0..modem_N
	// submessages with the kMMField* keys.
	virtual	status_t			GetModems(BMessage* outModems) = 0;
	virtual	status_t			GetModemInfo(const char* modemPath,
									BMessage* outInfo) = 0;

	// Cached call and SMS objects of one line; snapshot-only.
	virtual	status_t			GetCalls(const char* modemPath,
									BMessage* outCalls) = 0;
	virtual	status_t			GetSmsMessages(const char* modemPath,
									BMessage* outMessages) = 0;

	// Voice. Replies carry "status", "reason" and, for Dial,
	// kMMFieldCallPath.
	virtual	status_t			DialAsync(const char* modemPath,
									const char* number,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;
	virtual	status_t			AnswerAsync(const char* callPath,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;
	virtual	status_t			HangupAsync(const char* modemPath,
									const char* callPath,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;
	virtual	status_t			HoldAsync(const char* modemPath,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;
	virtual	status_t			SendDtmfAsync(const char* callPath,
									const char* digits,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;

	// SMS; SIP backends deliver them as MESSAGE requests.
	virtual	status_t			SendMessageAsync(const char* modemPath,
									const char* number,
									const char* text,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;
	virtual	status_t			DeleteSmsAsync(const char* modemPath,
									const char* smsPath,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;

	// SIM and radio control. Lines without a SIM answer
	// B_NOT_SUPPORTED.
	virtual	status_t			EnableModemAsync(const char* modemPath,
									bool enabled,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;
	virtual	status_t			UnlockSimAsync(const char* modemPath,
									const char* pin,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;
	virtual	status_t			UnlockSimPukAsync(const char* modemPath,
									const char* puk,
									const char* newPin,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;
	virtual	status_t			SetupSignalRateAsync(const char* modemPath,
									uint32 rateSeconds,
									const BMessenger& replyTo,
									uint32 replyWhat) = 0;

	virtual	bool				IsServiceAvailable() = 0;

	virtual	status_t			StartWatching(const BMessenger& target,
									uint32 notificationMask) = 0;
	virtual	status_t			StopWatching(const BMessenger& target) = 0;

	// Notification 'what' values delivered to StartWatching() targets.
	// Shared so SIP notifications travel the roster unchanged.
	enum NotificationType {
		NOTIFICATION_MODEM_ADDED = 'MMAD',
		NOTIFICATION_MODEM_REMOVED = 'MMRM',
		NOTIFICATION_MODEM_STATE_CHANGED = 'MMSC',
		NOTIFICATION_MODEM_SIGNAL_CHANGED = 'MMSI',
		NOTIFICATION_SERVICE_APPEARED = 'MMSA',
		NOTIFICATION_SERVICE_VANISHED = 'MMSV',
		NOTIFICATION_REGISTRATION_CHANGED = 'MRGC',
		NOTIFICATION_CALL_ADDED = 'MCAD',
		NOTIFICATION_CALL_REMOVED = 'MCRV',
		NOTIFICATION_CALL_STATE_CHANGED = 'MCSC',
		NOTIFICATION_SMS_ADDED = 'MSAD',
		NOTIFICATION_SMS_REMOVED = 'MSRV',
		NOTIFICATION_SMS_STATE_CHANGED = 'MSSC'
	};
};


#endif // _TELEPHONY_BACKEND_H
