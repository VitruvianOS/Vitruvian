/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "Modem.h"

#include <stdio.h>
#include <string.h>

#include "TelephonyBackend.h"


// #pragma mark - Construction


BModem::BModem(const char* path, TelephonyBackend* backend)
	:
	fPath(path != NULL ? path : ""),
	fBackend(backend)
{
	fInfo.AddString(kMMFieldModemPath, fPath.String());
}


BModem::~BModem()
{
}


// #pragma mark - Snapshot plumbing


const char*
BModem::_String(const char* key)
{
	if (fInfo.FindString(key, &fStringScratch) != B_OK)
		fStringScratch = "";
	return fStringScratch.String();
}


uint32
BModem::_UInt32(const char* key)
{
	uint32 value = 0;
	fInfo.FindUInt32(key, &value);
	return value;
}


bool
BModem::_Bool(const char* key)
{
	bool value = false;
	fInfo.FindBool(key, &value);
	return value;
}


// Picks the call the UI cares about: a ringing one wins, then active,
// then waiting; terminated calls never surface here.
void
BModem::_PickInterestingCall()
{
	fCallInfo.MakeEmpty();

	BMessage calls;
	if (fBackend->GetCalls(fPath.String(), &calls) != B_OK)
		return;

	int32 count = 0;
	calls.FindInt32(kMMFieldCallCount, &count);

	uint32 bestRank = 100;
	for (int32 i = 0; i < count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "call_%" B_PRId32, i);

		BMessage call;
		if (calls.FindMessage(name, &call) != B_OK)
			continue;

		uint32 state = 0;
		call.FindUInt32(kMMFieldCallState, &state);
		if (state == kMMCallStateTerminated
				|| state == kMMCallStateUnknown)
			continue;

		uint32 rank = 3;
		if (state == kMMCallStateRingingIn || state == kMMCallStateWaiting)
			rank = 0;
		else if (state == kMMCallStateActive)
			rank = 1;
		else if (state == kMMCallStateHeld)
			rank = 2;

		if (rank < bestRank) {
			bestRank = rank;
			fCallInfo = call;
		}
	}
}


status_t
BModem::Sync()
{
	if (fBackend == NULL)
		return B_NO_INIT;

	status_t status = fBackend->GetModemInfo(fPath.String(), &fInfo);
	_PickInterestingCall();

	// A modem that vanished reads as entry-not-found; keep the old info
	// so UI can still show the last known state until ModemRemoved.
	return status == B_ENTRY_NOT_FOUND ? B_OK : status;
}


// #pragma mark - Identity / network


const char*
BModem::EquipmentId()
{
	Sync();
	return _String(kMMFieldModemEquipmentId);
}


const char*
BModem::Manufacturer()
{
	Sync();
	return _String(kMMFieldModemManufacturer);
}


const char*
BModem::Model()
{
	Sync();
	return _String(kMMFieldModemModel);
}


const char*
BModem::OperatorName()
{
	Sync();
	return _String(kMMFieldModemOperatorName);
}


const char*
BModem::OperatorCode()
{
	Sync();
	return _String(kMMFieldModemOperatorCode);
}


uint32
BModem::RegistrationState()
{
	Sync();
	return _UInt32(kMMFieldModemRegistrationState);
}


const char*
BModem::RegistrationStateName()
{
	Sync();
	return _String(kMMFieldModemRegistrationName);
}


bool
BModem::IsRegisteredHome()
{
	Sync();
	uint32 state = _UInt32(kMMFieldModemRegistrationState);
	return state == kMMRegHome || state == kMMRegHomeSmsOnly
		|| state == kMMRegHomeCsfbNotPreferred;
}


bool
BModem::IsRoaming()
{
	Sync();
	return _Bool(kMMFieldModemIsRoaming);
}


uint32
BModem::AccessType()
{
	Sync();
	const char* type = _String(kMMFieldModemAccessType);
	if (strcmp(type, "5G") == 0)
		return B_TELEPHONY_ACCESS_5G;
	if (strcmp(type, "4G") == 0)
		return B_TELEPHONY_ACCESS_4G;
	if (strcmp(type, "3G") == 0)
		return B_TELEPHONY_ACCESS_3G;
	if (strcmp(type, "2G") == 0)
		return B_TELEPHONY_ACCESS_2G;
	return B_TELEPHONY_ACCESS_UNKNOWN;
}


const char*
BModem::AccessTypeString()
{
	Sync();
	return _String(kMMFieldModemAccessType);
}


uint32
BModem::SignalQuality(bool* recent)
{
	Sync();
	if (recent != NULL)
		*recent = true;
	return _UInt32(kMMFieldModemSignalQuality);
}


uint32
BModem::SignalBars()
{
	Sync();
	return _UInt32(kMMFieldModemSignalBars);
}


uint32
BModem::State()
{
	Sync();
	int32 state = 0;
	fInfo.FindInt32(kMMFieldModemState, &state);
	return (uint32)state;
}


bool
BModem::IsEnabled()
{
	Sync();
	return _Bool(kMMFieldModemEnabled);
}


// #pragma mark - SIM


bool
BModem::IsSimPresent()
{
	Sync();
	BString simPath;
	fInfo.FindString(kMMFieldModemSimPath, &simPath);
	return !simPath.IsEmpty();
}


bool
BModem::IsSimLocked()
{
	Sync();
	return _Bool(kMMFieldSimLocked);
}


bool
BModem::IsSimPinRequired()
{
	Sync();
	return _Bool(kMMFieldSimPinRequired);
}


bool
BModem::IsSimPukRequired()
{
	Sync();
	return _Bool(kMMFieldSimPukRequired);
}


uint32
BModem::SimUnlockRetries()
{
	Sync();
	return _UInt32(kMMFieldSimUnlockRetries);
}


status_t
BModem::UnlockSim(const char* pin, const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;
	return fBackend->UnlockSimAsync(fPath.String(), pin, replyTo,
		replyWhat);
}


status_t
BModem::UnlockSimPuk(const char* puk, const char* newPin,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;
	return fBackend->UnlockSimPukAsync(fPath.String(), puk, newPin,
		replyTo, replyWhat);
}


status_t
BModem::SetEnabled(bool enabled, const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;
	return fBackend->EnableModemAsync(fPath.String(), enabled, replyTo,
		replyWhat);
}


// #pragma mark - Voice


status_t
BModem::Dial(const char* number, const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;
	return fBackend->DialAsync(fPath.String(), number, replyTo, replyWhat);
}


status_t
BModem::Answer(const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;

	Sync();
	const char* callPath = _String(kMMFieldCallPath);
	if (callPath[0] == '\0')
		return B_ENTRY_NOT_FOUND;
	return fBackend->AnswerAsync(callPath, replyTo, replyWhat);
}


status_t
BModem::HangUp(const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;

	Sync();
	const char* callPath = _String(kMMFieldCallPath);
	return fBackend->HangupAsync(fPath.String(),
		callPath[0] != '\0' ? callPath : NULL, replyTo, replyWhat);
}


status_t
BModem::Hold(const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;
	return fBackend->HoldAsync(fPath.String(), replyTo, replyWhat);
}


status_t
BModem::SendDtmf(const char* digits, const BMessenger& replyTo,
	uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;

	Sync();
	const char* callPath = _String(kMMFieldCallPath);
	if (callPath[0] == '\0')
		return B_ENTRY_NOT_FOUND;
	return fBackend->SendDtmfAsync(callPath, digits, replyTo, replyWhat);
}


uint32
BModem::CallState()
{
	uint32 state = 0;
	fCallInfo.FindUInt32(kMMFieldCallState, &state);
	return state;
}


const char*
BModem::CallStateName()
{
	if (fCallInfo.FindString(kMMFieldCallStateName, &fCallScratch) != B_OK)
		fCallScratch = "unknown";
	return fCallScratch.String();
}


const char*
BModem::CallNumber()
{
	if (fCallInfo.FindString(kMMFieldCallNumber, &fCallScratch) != B_OK)
		fCallScratch = "";
	return fCallScratch.String();
}


uint32
BModem::CallDirection()
{
	uint32 direction = 0;
	fCallInfo.FindUInt32(kMMFieldCallDirection, &direction);
	return direction;
}


const char*
BModem::CallPath()
{
	if (fCallInfo.FindString(kMMFieldCallPath, &fCallScratch) != B_OK)
		fCallScratch = "";
	return fCallScratch.String();
}


// #pragma mark - SMS


status_t
BModem::SendMessage(const char* number, const char* text,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;
	return fBackend->SendMessageAsync(fPath.String(), number, text,
		replyTo, replyWhat);
}


status_t
BModem::DeleteMessage(const char* smsPath, const BMessenger& replyTo,
	uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;
	return fBackend->DeleteSmsAsync(fPath.String(), smsPath, replyTo,
		replyWhat);
}


int32
BModem::CountMessages()
{
	if (fBackend == NULL)
		return 0;

	BMessage messages;
	if (fBackend->GetSmsMessages(fPath.String(), &messages) != B_OK)
		return 0;

	int32 count = 0;
	messages.FindInt32(kMMFieldSmsCount, &count);
	return count;
}


status_t
BModem::MessageAt(int32 index, BMessage* outInfo)
{
	if (fBackend == NULL || outInfo == NULL)
		return B_BAD_VALUE;

	BMessage messages;
	status_t status = fBackend->GetSmsMessages(fPath.String(), &messages);
	if (status != B_OK)
		return status;

	char name[32];
	snprintf(name, sizeof(name), "sms_%" B_PRId32, index);
	if (messages.FindMessage(name, outInfo) != B_OK)
		return B_ENTRY_NOT_FOUND;
	return B_OK;
}


status_t
BModem::FindMessage(const char* smsPath, BMessage* outInfo)
{
	if (fBackend == NULL || smsPath == NULL || outInfo == NULL)
		return B_BAD_VALUE;

	BMessage messages;
	status_t status = fBackend->GetSmsMessages(fPath.String(), &messages);
	if (status != B_OK)
		return status;

	int32 count = 0;
	messages.FindInt32(kMMFieldSmsCount, &count);
	for (int32 i = 0; i < count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "sms_%" B_PRId32, i);

		BMessage entry;
		if (messages.FindMessage(name, &entry) != B_OK)
			continue;

		BString path;
		if (entry.FindString(kMMFieldSmsPath, &path) == B_OK
				&& strcmp(path.String(), smsPath) == 0) {
			*outInfo = entry;
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}
