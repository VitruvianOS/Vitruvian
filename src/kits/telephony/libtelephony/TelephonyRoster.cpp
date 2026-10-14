/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "TelephonyRoster.h"

#include <Looper.h>
#include <Autolock.h>

#include <stdio.h>

#include "Modem.h"
#include "ModemManagerBackend.h"


// #pragma mark - Notifier


// Hidden looper that receives the backend's notification stream and
// hands it to the roster; the roster itself is not a BHandler so apps
// can hold the singleton without a message loop of their own.
class _TelephonyNotifier : public BLooper {
public:
								_TelephonyNotifier(BTelephonyRoster* roster)
									:
									BLooper("telephony_notifier"),
									fRoster(roster)
			{
			}

	virtual	void				MessageReceived(BMessage* message)
			{
				if (fRoster != NULL)
					fRoster->_OnBackendNotification(message->what,
						*message);
				BLooper::MessageReceived(message);
			}

private:
			BTelephonyRoster*	fRoster;
};


// #pragma mark - Singleton


BTelephonyRoster*
BTelephonyRoster::Default()
{
	// Deliberately leaked; see ModemManagerBackend::Instance().
	static BTelephonyRoster* instance = new BTelephonyRoster();
	return instance;
}


BTelephonyRoster::BTelephonyRoster()
	:
	fBackend(NULL)
{
	fBackend = ModemManagerBackend::Instance();

	_TelephonyNotifier* notifier = new _TelephonyNotifier(this);
	notifier->Run();

	// One watcher for everyone; roster-side masks filter per target.
	fBackend->StartWatching(notifier->Messenger(), (uint32)~0);
	_SyncModemList();
}


BTelephonyRoster::~BTelephonyRoster()
{
}


// #pragma mark - Modem list


void
BTelephonyRoster::_SyncModemList()
{
	BMessage modems;
	fBackend->GetModems(&modems);

	int32 count = 0;
	modems.FindInt32(kMMFieldModemCount, &count);

	BAutolock lock(fLock);

	// Drop modems that vanished.
	for (std::map<BString, BModem*>::iterator it = fModems.begin();
			it != fModems.end();) {
		bool found = false;
		for (int32 i = 0; i < count; i++) {
			char name[32];
			snprintf(name, sizeof(name), "modem_%" B_PRId32, i);
			BMessage info;
			if (modems.FindMessage(name, &info) != B_OK)
				continue;
			BString path;
			if (info.FindString(kMMFieldModemPath, &path) == B_OK
					&& path == it->first) {
				found = true;
				break;
			}
		}
		if (!found) {
			delete it->second;
			fModems.erase(it++);
		} else
			it++;
	}

	for (int32 i = 0; i < count; i++) {
		char name[32];
		snprintf(name, sizeof(name), "modem_%" B_PRId32, i);
		BMessage info;
		if (modems.FindMessage(name, &info) != B_OK)
			continue;

		BString path;
		if (info.FindString(kMMFieldModemPath, &path) != B_OK)
			continue;
		if (fModems.find(path) != fModems.end())
			continue;

		fModems[path] = new BModem(path.String(), fBackend);
	}
}


int32
BTelephonyRoster::CountModems()
{
	BAutolock lock(fLock);
	return (int32)fModems.size();
}


BModem*
BTelephonyRoster::ModemAt(int32 index)
{
	BAutolock lock(fLock);
	if (index < 0 || index >= (int32)fModems.size())
		return NULL;

	std::map<BString, BModem*>::iterator it = fModems.begin();
	for (int32 i = 0; i < index; i++)
		it++;
	return it->second;
}


BModem*
BTelephonyRoster::ModemForPath(const char* path)
{
	if (path == NULL)
		return NULL;

	BAutolock lock(fLock);
	std::map<BString, BModem*>::iterator it = fModems.find(path);
	return it != fModems.end() ? it->second : NULL;
}


const char*
BTelephonyRoster::DefaultModemPath()
{
	BAutolock lock(fLock);
	if (fModems.empty())
		return NULL;
	return fModems.begin()->first.String();
}


bool
BTelephonyRoster::IsServiceAvailable()
{
	return fBackend != NULL && fBackend->IsServiceAvailable();
}


status_t
BTelephonyRoster::SetupSignalRate(const char* modemPath, uint32 rateSeconds,
	const BMessenger& replyTo, uint32 replyWhat)
{
	if (fBackend == NULL)
		return B_NO_INIT;
	return fBackend->SetupSignalRateAsync(modemPath, rateSeconds, replyTo,
		replyWhat);
}


// #pragma mark - Watching


status_t
BTelephonyRoster::StartWatching(const BMessenger& target, uint32 mask)
{
	BAutolock lock(fLock);
	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers[i].mask = mask;
			return B_OK;
		}
	}

	Watcher watcher;
	watcher.messenger = target;
	watcher.mask = mask;
	fWatchers.push_back(watcher);
	return B_OK;
}


status_t
BTelephonyRoster::StopWatching(const BMessenger& target)
{
	BAutolock lock(fLock);
	for (size_t i = 0; i < fWatchers.size(); i++) {
		if (fWatchers[i].messenger == target) {
			fWatchers.erase(fWatchers.begin() + i);
			return B_OK;
		}
	}
	return B_ENTRY_NOT_FOUND;
}


// #pragma mark - Backend notification fan-out


void
BTelephonyRoster::_Dispatch(BMessage& message, uint32 watchBit)
{
	// Caller holds fLock.
	for (size_t i = 0; i < fWatchers.size();) {
		Watcher& watcher = fWatchers[i];
		if (!watcher.messenger.IsValid()) {
			fWatchers.erase(fWatchers.begin() + i);
			continue;
		}
		if ((watcher.mask & watchBit) != 0)
			watcher.messenger.SendMessage(&message);
		i++;
	}
}


void
BTelephonyRoster::_OnBackendNotification(uint32 type, BMessage& message)
{
	using namespace ModemManagerBackend;

	if (type == NOTIFICATION_MODEM_ADDED
			|| type == NOTIFICATION_MODEM_REMOVED)
		_SyncModemList();

	uint32 publicWhat = 0;
	uint32 watchBit = kTelephonyWatchModems;

	switch ((int32)type) {
		case NOTIFICATION_MODEM_ADDED:
			publicWhat = kTelephonyMsgModemAdded;
			break;
		case NOTIFICATION_MODEM_REMOVED:
			publicWhat = kTelephonyMsgModemRemoved;
			break;
		case NOTIFICATION_MODEM_STATE_CHANGED:
		case NOTIFICATION_MODEM_SIGNAL_CHANGED:
		case NOTIFICATION_REGISTRATION_CHANGED:
			publicWhat = kTelephonyMsgModemChanged;
			break;
		case NOTIFICATION_SERVICE_APPEARED:
			publicWhat = kTelephonyMsgServiceAppeared;
			break;
		case NOTIFICATION_SERVICE_VANISHED:
			publicWhat = kTelephonyMsgServiceVanished;
			break;
		case NOTIFICATION_CALL_ADDED:
			publicWhat = kTelephonyMsgCallAdded;
			watchBit = kTelephonyWatchCalls;
			break;
		case NOTIFICATION_CALL_REMOVED:
			publicWhat = kTelephonyMsgCallRemoved;
			watchBit = kTelephonyWatchCalls;
			break;
		case NOTIFICATION_CALL_STATE_CHANGED:
			publicWhat = kTelephonyMsgCallStateChanged;
			watchBit = kTelephonyWatchCalls;
			break;
		case NOTIFICATION_SMS_ADDED:
			publicWhat = kTelephonyMsgSmsAdded;
			watchBit = kTelephonyWatchMessages;
			break;
		case NOTIFICATION_SMS_REMOVED:
			publicWhat = kTelephonyMsgSmsRemoved;
			watchBit = kTelephonyWatchMessages;
			break;
		case NOTIFICATION_SMS_STATE_CHANGED:
			publicWhat = kTelephonyMsgSmsStateChanged;
			watchBit = kTelephonyWatchMessages;
			break;
		default:
			return;
	}

	BMessage out(publicWhat);
	// The backend keys already match the public kTelephonyField*
	// strings for calls and messages; copy the whole payload.
	for (int32 i = 0; ; i++) {
		const char* name = NULL;
		type_code code;
		status_t status = message.GetInfo(B_ANY_TYPE, i, &name, &code);
		if (status != B_OK)
			break;
		if (name == NULL)
			continue;

		switch (code) {
			case B_STRING_TYPE:
			{
				BString value;
				if (message.FindString(name, &value) == B_OK)
					out.AddString(name, value.String());
				break;
			}
			case B_UINT32_TYPE:
			{
				uint32 value = 0;
				if (message.FindUInt32(name, &value) == B_OK)
					out.AddUInt32(name, value);
				break;
			}
			case B_BOOL_TYPE:
			{
				bool value = false;
				if (message.FindBool(name, &value) == B_OK)
					out.AddBool(name, value);
				break;
			}
			case B_INT32_TYPE:
			{
				int32 value = 0;
				if (message.FindInt32(name, &value) == B_OK)
					out.AddInt32(name, value);
				break;
			}
			default:
				break;
		}
	}

	{
		BAutolock lock(fLock);
		_Dispatch(out, watchBit);

		// A modem that now demands its PIN gets its own notification
		// so unlock UI can pop without parsing ModemChanged.
		bool pin = false;
		bool puk = false;
		message.FindBool(kMMFieldSimPinRequired, &pin);
		message.FindBool(kMMFieldSimPukRequired, &puk);
		if (pin || puk) {
			BMessage unlock(kTelephonyMsgSimUnlockRequired);
			BString path;
			if (message.FindString(kMMFieldModemPath, &path) == B_OK)
				unlock.AddString(kTelephonyFieldModemPath,
					path.String());
			_Dispatch(unlock, kTelephonyWatchModems);
		}
	}
}
