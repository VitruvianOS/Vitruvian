/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _TELEPHONY_ROSTER_H
#define _TELEPHONY_ROSTER_H


#include <Locker.h>
#include <Messenger.h>
#include <String.h>

#include <map>
#include <vector>


class BModem;
class BMessage;
class TelephonyBackend;
class _TelephonyNotifier;


// BMessage field names used by the telephony kit, both in notification
// messages and in operation replies ("status"/"reason" ride along).
static const char* const kTelephonyFieldModemPath = "modem_path";
static const char* const kTelephonyFieldCallPath = "call_path";
static const char* const kTelephonyFieldCallState = "call_state";
static const char* const kTelephonyFieldCallStateName = "call_state_name";
static const char* const kTelephonyFieldCallNumber = "call_number";
static const char* const kTelephonyFieldCallDirection = "call_direction";
static const char* const kTelephonyFieldCallDirectionName
	= "call_direction_name";
static const char* const kTelephonyFieldSmsPath = "sms_path";
static const char* const kTelephonyFieldSmsNumber = "sms_number";
static const char* const kTelephonyFieldSmsText = "sms_text";
static const char* const kTelephonyFieldSmsState = "sms_state";
static const char* const kTelephonyFieldSmsStateName = "sms_state_name";
static const char* const kTelephonyFieldSmsTimestamp = "sms_timestamp";
static const char* const kTelephonyFieldSmsDeliveryState
	= "sms_delivery_state";
static const char* const kTelephonyFieldSmsReceived = "sms_received";


// Notification 'what' values delivered to StartWatching() targets.
enum {
	kTelephonyMsgModemAdded		= 'TlmA',
	kTelephonyMsgModemRemoved	= 'TlmR',
	kTelephonyMsgModemChanged	= 'TlmC',
	kTelephonyMsgSimUnlockRequired	= 'TlmP',
	kTelephonyMsgServiceAppeared	= 'TlmS',
	kTelephonyMsgServiceVanished	= 'TlmV',
	kTelephonyMsgCallAdded		= 'TlcA',
	kTelephonyMsgCallRemoved	= 'TlcR',
	kTelephonyMsgCallStateChanged	= 'TlcC',
	kTelephonyMsgSmsAdded		= 'TlsA',
	kTelephonyMsgSmsRemoved		= 'TlsR',
	kTelephonyMsgSmsStateChanged	= 'TlsC'
};


// Mask bits for StartWatching(); OR them together.
enum {
	kTelephonyWatchModems	= 1 << 0,
	kTelephonyWatchCalls	= 1 << 1,
	kTelephonyWatchMessages	= 1 << 2,
	kTelephonyWatchAll	= 0xff
};


// Registration state; numeric values mirror MMModem3gppRegistrationState
// so a raw modem answer can be shown without translation.
enum telephony_registration_state {
	B_TELEPHONY_REGISTRATION_IDLE = 0,
	B_TELEPHONY_REGISTRATION_HOME = 1,
	B_TELEPHONY_REGISTRATION_SEARCHING = 2,
	B_TELEPHONY_REGISTRATION_DENIED = 3,
	B_TELEPHONY_REGISTRATION_UNKNOWN = 4,
	B_TELEPHONY_REGISTRATION_ROAMING = 5
};


// Coarse radio technology derived from MMModemAccessTechnology bits.
enum {
	B_TELEPHONY_ACCESS_UNKNOWN = 0,
	B_TELEPHONY_ACCESS_2G = 1,
	B_TELEPHONY_ACCESS_3G = 2,
	B_TELEPHONY_ACCESS_4G = 3,
	B_TELEPHONY_ACCESS_5G = 4
};


// Call state; numeric values mirror MMCallState.
enum telephony_call_state {
	B_TELEPHONY_CALL_UNKNOWN = 0,
	B_TELEPHONY_CALL_DIALING = 1,
	B_TELEPHONY_CALL_RINGING_OUT = 2,
	B_TELEPHONY_CALL_RINGING_IN = 3,
	B_TELEPHONY_CALL_ACTIVE = 4,
	B_TELEPHONY_CALL_HELD = 5,
	B_TELEPHONY_CALL_WAITING = 6,
	B_TELEPHONY_CALL_TERMINATED = 7
};


// Call direction; numeric values mirror MMCallDirection.
enum telephony_call_direction {
	B_TELEPHONY_DIRECTION_UNKNOWN = 0,
	B_TELEPHONY_DIRECTION_INCOMING = 1,
	B_TELEPHONY_DIRECTION_OUTGOING = 2
};


// SMS state; numeric values mirror MM_SMS_STATE.
enum telephony_sms_state {
	B_TELEPHONY_SMS_UNKNOWN = 0,
	B_TELEPHONY_SMS_STORED = 1,
	B_TELEPHONY_SMS_RECEIVING = 2,
	B_TELEPHONY_SMS_RECEIVED = 3,
	B_TELEPHONY_SMS_SENDING = 4,
	B_TELEPHONY_SMS_SENT = 5
};


class BTelephonyRoster {
public:
	static BTelephonyRoster* Default();

			bool			IsServiceAvailable();

	// Notifications: 'what' is one of the kTelephonyMsg* values above;
	// mask is a OR of the kTelephonyWatch* bits.
	status_t				StartWatching(const BMessenger& target,
									uint32 mask);
	status_t				StopWatching(const BMessenger& target);

	// Modem objects are owned by the roster and live as long as the
	// modem stays on the bus; borrowed pointers only.
	int32					CountModems();
	BModem*					ModemAt(int32 index);
	BModem*					ModemForPath(const char* path);
	const char*				DefaultModemPath();

	// Asks the modem to sample signal every rateSeconds so change
	// notifications keep flowing (Signal.Setup on the modem).
	status_t				SetupSignalRate(const char* modemPath,
									uint32 rateSeconds,
									const BMessenger& replyTo,
									uint32 replyWhat);

	// Video over SIP: starts the camera pipeline for the first
	// active video call and delivers I420 frames (local=true is
	// the self view) to the hook. B_UNSUPPORTED when no SIP line
	// owns a video call.
	status_t				StartVideo(const char* devicePath);
	status_t				StopVideo();
	typedef void			(*VideoFrameHook)(void* cookie,
									const uint8* const* data,
									const int* stride, int width,
									int height, bool local);
	status_t				SetVideoFrameHook(VideoFrameHook hook,
									void* cookie);

	// SIP call audio: PCM hooks the app binds to its PipeWire
	// streams (media kit). pull supplies mic samples to encode,
	// push receives far-end PCM to play.
	typedef size_t			(*AudioPullHook)(void* cookie,
									int16* samples, size_t maxSamples);
	typedef void			(*AudioPushHook)(void* cookie,
									const int16* samples, size_t count);
	status_t				SetAudioHooks(AudioPullHook pull,
									AudioPushHook push, void* cookie);

private:
							BTelephonyRoster();
							~BTelephonyRoster();

	friend class			_TelephonyNotifier;

			void			_SyncModemList();
			void			_OnBackendNotification(uint32 type,
									BMessage& message);
			void			_Dispatch(BMessage& message, uint32 watchBit);

			// Every line (modem or SIP account) is owned by the
			// backend that produced it; fBackendsByPath maps a
			// line path to its backend so BModem operations
			// reach the right implementation.
			std::vector<TelephonyBackend*>	fBackends;
			std::map<BString, TelephonyBackend*>	fBackendsByPath;

			struct Watcher {
				BMessenger	messenger;
				uint32		mask;
			};
			std::vector<Watcher>	fWatchers;

			std::map<BString, BModem*>	fModems;
			BLocker			fLock;
};


#endif // _TELEPHONY_ROSTER_H
