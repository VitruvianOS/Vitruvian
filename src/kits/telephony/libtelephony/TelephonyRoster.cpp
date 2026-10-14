/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "TelephonyRoster.h"

#include <Handler.h>
#include <Looper.h>
#include <Autolock.h>

#include <stdio.h>

#include "Modem.h"
#include "ModemManagerBackend.h"
#include "SipBackend.h"
#include "SipAccounts.h"
#include "TelephonyBackend.h"


// #pragma mark - Notifier


// Hidden handler that receives the backend notification streams on
// its own looper and hands them to the roster; the roster itself is
// not a BHandler so apps can hold the singleton without a looper of
// their own.
class _TelephonyNotifier : public BHandler {
public:
								_TelephonyNotifier(BTelephonyRoster* roster)
									:
									BHandler("telephony_notifier"),
									fRoster(roster)
			{
			}

	virtual	void				MessageReceived(BMessage* message)
			{
				if (fRoster != NULL)
					fRoster->_OnBackendNotification(message->what,
						*message);
				BHandler::MessageReceived(message);
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
{
	// Both line types ride the same seam: the ModemManager backend
	// brings the cellular modems, the SIP backend the VoIP accounts.
	fBackends.push_back(ModemManagerBackend::Instance());

	SipBackend* sip = SipBackend::Instance();
	// Stored accounts come back from the Accounts kit before any
	// watcher attaches, so lines exist on first enumeration.
	BSipAccounts::Default()->LoadIntoBackend(sip);
	fBackends.push_back(sip);

	_TelephonyNotifier* notifier = new _TelephonyNotifier(this);
	BLooper* looper = new BLooper("telephony_notifier");
	looper->AddHandler(notifier);
	looper->Run();

	// One watcher for everyone; roster-side masks filter per target.
	for (size_t i = 0; i < fBackends.size(); i++)
		fBackends[i]->StartWatching(BMessenger(notifier), (uint32)~0);
	_SyncModemList();
}


BTelephonyRoster::~BTelephonyRoster()
{
}


// #pragma mark - Modem list


void
BTelephonyRoster::_SyncModemList()
{
	BAutolock lock(fLock);

	// Drop lines that vanished from every backend.
	for (std::map<BString, BModem*>::iterator it = fModems.begin();
			it != fModems.end();) {
		TelephonyBackend* owner = fBackendsByPath[it->first];
		BMessage modems;
		bool found = false;
		if (owner != NULL && owner->GetModems(&modems) == B_OK) {
			int32 count = 0;
			modems.FindInt32(kMMFieldModemCount, &count);
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
		}
		if (!found) {
			delete it->second;
			fBackendsByPath.erase(it->first);
			fModems.erase(it++);
		} else
			it++;
	}

	// Adopt new lines from every backend.
	for (size_t b = 0; b < fBackends.size(); b++) {
		BMessage modems;
		if (fBackends[b]->GetModems(&modems) != B_OK)
			continue;

		int32 count = 0;
		modems.FindInt32(kMMFieldModemCount, &count);
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

			fModems[path] = new BModem(path.String(), fBackends[b]);
			fBackendsByPath[path] = fBackends[b];
		}
	}
}


int32
BTelephonyRoster::CountModems()
{
	_SyncModemList();
	BAutolock lock(fLock);
	return (int32)fModems.size();
}


BModem*
BTelephonyRoster::ModemAt(int32 index)
{
	_SyncModemList();
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

	_SyncModemList();
	BAutolock lock(fLock);
	std::map<BString, BModem*>::iterator it = fModems.find(path);
	return it != fModems.end() ? it->second : NULL;
}


const char*
BTelephonyRoster::DefaultModemPath()
{
	_SyncModemList();
	BAutolock lock(fLock);
	if (fModems.empty())
		return NULL;
	return fModems.begin()->first.String();
}


bool
BTelephonyRoster::IsServiceAvailable()
{
	for (size_t i = 0; i < fBackends.size(); i++) {
		if (fBackends[i]->IsServiceAvailable())
			return true;
	}
	return false;
}


status_t
BTelephonyRoster::SetupSignalRate(const char* modemPath, uint32 rateSeconds,
	const BMessenger& replyTo, uint32 replyWhat)
{
	// SIP lines have no sampled signal; they answer B_OK themselves.
	TelephonyBackend* backend = NULL;
	{
		BAutolock lock(fLock);
		std::map<BString, TelephonyBackend*>::iterator it
			= fBackendsByPath.find(modemPath);
		if (it != fBackendsByPath.end())
			backend = it->second;
	}
	if (backend == NULL)
		return B_ENTRY_NOT_FOUND;
	return backend->SetupSignalRateAsync(modemPath, rateSeconds, replyTo,
		replyWhat);
}


status_t
BTelephonyRoster::StartVideo(const char* devicePath)
{
	SipBackend* sip = SipBackend::Instance();
	if (sip->VideoCallPath().IsEmpty())
		return B_UNSUPPORTED;

	sip->SetVideoCaptureDevice(devicePath);
	SipBackend_StartVideoCapture(sip, devicePath);
	return B_OK;
}


status_t
BTelephonyRoster::StopVideo()
{
	SipBackend* sip = SipBackend::Instance();
	SipBackend_StopVideoCapture(sip);
	return B_OK;
}


status_t
BTelephonyRoster::SetVideoFrameHook(VideoFrameHook hook, void* cookie)
{
	SipBackend* sip = SipBackend::Instance();
	sip->SetVideoHooks(hook, hook, cookie);
	return B_OK;
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
	typedef TelephonyBackend TB;

	if (type == TB::NOTIFICATION_MODEM_ADDED
			|| type == TB::NOTIFICATION_MODEM_REMOVED)
		_SyncModemList();

	uint32 publicWhat = 0;
	uint32 watchBit = kTelephonyWatchModems;

	switch ((int32)type) {
		case TB::NOTIFICATION_MODEM_ADDED:
			publicWhat = kTelephonyMsgModemAdded;
			break;
		case TB::NOTIFICATION_MODEM_REMOVED:
			publicWhat = kTelephonyMsgModemRemoved;
			break;
		case TB::NOTIFICATION_MODEM_STATE_CHANGED:
		case TB::NOTIFICATION_MODEM_SIGNAL_CHANGED:
		case TB::NOTIFICATION_REGISTRATION_CHANGED:
			publicWhat = kTelephonyMsgModemChanged;
			break;
		case TB::NOTIFICATION_SERVICE_APPEARED:
			publicWhat = kTelephonyMsgServiceAppeared;
			break;
		case TB::NOTIFICATION_SERVICE_VANISHED:
			publicWhat = kTelephonyMsgServiceVanished;
			break;
		case TB::NOTIFICATION_CALL_ADDED:
			publicWhat = kTelephonyMsgCallAdded;
			watchBit = kTelephonyWatchCalls;
			break;
		case TB::NOTIFICATION_CALL_REMOVED:
			publicWhat = kTelephonyMsgCallRemoved;
			watchBit = kTelephonyWatchCalls;
			break;
		case TB::NOTIFICATION_CALL_STATE_CHANGED:
			publicWhat = kTelephonyMsgCallStateChanged;
			watchBit = kTelephonyWatchCalls;
			break;
		case TB::NOTIFICATION_SMS_ADDED:
			publicWhat = kTelephonyMsgSmsAdded;
			watchBit = kTelephonyWatchMessages;
			break;
		case TB::NOTIFICATION_SMS_REMOVED:
			publicWhat = kTelephonyMsgSmsRemoved;
			watchBit = kTelephonyWatchMessages;
			break;
		case TB::NOTIFICATION_SMS_STATE_CHANGED:
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
