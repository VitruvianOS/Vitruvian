/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#ifndef _TELEPHONY_MODEM_H
#define _TELEPHONY_MODEM_H


#include <Messenger.h>
#include <String.h>

#include <Message.h>

#include "TelephonyRoster.h"


class ModemManagerBackend;


// One cellular modem. Instances are owned by BTelephonyRoster; every
// accessor reads the roster backend's snapshot cache and never blocks on
// D-Bus. Mutating operations are asynchronous: the outcome arrives as a
// reply message with "status" (int32) and, on failure, "reason".
class BModem {
public:
								~BModem();

			const char*			Path() const
									{ return fPath.String(); }

			status_t			Sync();

			// Identity
			const char*			EquipmentId();
			const char*			Manufacturer();
			const char*			Model();

			// Network
			const char*			OperatorName();
			const char*			OperatorCode();
			uint32				RegistrationState();
									// telephony_registration_state
			const char*			RegistrationStateName();
			bool				IsRegisteredHome();
			bool				IsRoaming();
			uint32				AccessType();
									// B_TELEPHONY_ACCESS_*
			const char*			AccessTypeString();
									// "2G".."5G" or "unknown"
			uint32				SignalQuality(bool* recent = NULL);
			uint32				SignalBars();
									// 0-4
			uint32				State();
			bool				IsEnabled();

			// SIM
			bool				IsSimPresent();
			bool				IsSimLocked();
			bool				IsSimPinRequired();
			bool				IsSimPukRequired();
			uint32				SimUnlockRetries();
			status_t			UnlockSim(const char* pin,
									const BMessenger& replyTo,
									uint32 replyWhat);
			status_t			UnlockSimPuk(const char* puk,
									const char* newPin,
									const BMessenger& replyTo,
									uint32 replyWhat);

			// Radio power; false is the per-modem airplane switch.
			status_t			SetEnabled(bool enabled,
									const BMessenger& replyTo,
									uint32 replyWhat);

			// Voice calls
			status_t			Dial(const char* number,
									const BMessenger& replyTo,
									uint32 replyWhat);
			status_t			Answer(const BMessenger& replyTo,
									uint32 replyWhat);
			status_t			HangUp(const BMessenger& replyTo,
									uint32 replyWhat);
			status_t			Hold(const BMessenger& replyTo,
									uint32 replyWhat);
			status_t			SendDtmf(const char* digits,
									const BMessenger& replyTo,
									uint32 replyWhat);

			// Cached state of this modem's most interesting call:
			// the ringing one if there is one, else the active one.
			uint32				CallState();
									// telephony_call_state
			const char*			CallStateName();
			const char*			CallNumber();
			uint32				CallDirection();
									// telephony_call_direction
			const char*			CallPath();

			// SMS. Send replies immediately after the modem accepted
			// the message; delivery progress arrives as
			// kTelephonyMsgSmsStateChanged notifications.
			status_t			SendMessage(const char* number,
									const char* text,
									const BMessenger& replyTo,
									uint32 replyWhat);
			status_t			DeleteMessage(const char* smsPath,
									const BMessenger& replyTo,
									uint32 replyWhat);

			int32				CountMessages();
								// Fills one flat message entry with the
								// kTelephonyFieldSms* fields.
			status_t			MessageAt(int32 index, BMessage* outInfo);
			status_t			FindMessage(const char* smsPath,
									BMessage* outInfo);

private:
	friend class				BTelephonyRoster;

								BModem(const char* path,
									ModemManagerBackend* backend);

			const char*			_String(const char* key);
			uint32				_UInt32(const char* key);
			bool				_Bool(const char* key);
			void				_PickInterestingCall();

			BString				fPath;
			ModemManagerBackend* fBackend;
			BMessage			fInfo;
			BMessage			fCallInfo;
			BString				fStringScratch;
			BString				fCallScratch;
};


#endif // _TELEPHONY_MODEM_H
