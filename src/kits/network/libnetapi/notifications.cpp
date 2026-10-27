/*
 * Copyright 2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	The notifications API uses the generic syscall interface of the
	network's stack notification module.
*/

#include <net_notifications.h>

#include <MessengerPrivate.h>
#include <generic_syscall_defs.h>
#ifndef __VOS__
#include <syscalls.h>
#else
#include <Autolock.h>
#include <Locker.h>
#include <Looper.h>
#include <Message.h>

#include <vector>

#include "NMBackend.h"
#endif


#ifdef __VOS__

// Turns NMBackend's notifications into the legacy B_NETWORK_MONITOR messages.
class NetworkMonitorBridge : public BLooper {
public:
	NetworkMonitorBridge()
		:
		BLooper("network monitor bridge"),
		fWatching(false)
	{
	}

	// The bridge is locked by the caller.
	status_t SetTarget(const BMessenger& target, uint32 flags)
	{
		for (size_t i = 0; i < fTargets.size(); i++) {
			if (fTargets[i].messenger != target)
				continue;

			if (flags != 0)
				fTargets[i].flags = flags;
			else
				fTargets.erase(fTargets.begin() + i);
			return _Update();
		}

		if (flags == 0)
			return B_OK;

		Target entry;
		entry.messenger = target;
		entry.flags = flags;
		fTargets.push_back(entry);
		return _Update();
	}

	virtual void MessageReceived(BMessage* message)
	{
		int32 opcode;
		switch (message->what) {
			case NMBackend::NOTIFICATION_DEVICE_ADDED:
				opcode = B_NETWORK_INTERFACE_ADDED;
				break;
			case NMBackend::NOTIFICATION_DEVICE_REMOVED:
				opcode = B_NETWORK_INTERFACE_REMOVED;
				break;
			case NMBackend::NOTIFICATION_DEVICE_IP_CHANGED:
				opcode = B_NETWORK_INTERFACE_CHANGED;
				break;
			case NMBackend::NOTIFICATION_DEVICE_STATE_CHANGED:
				opcode = B_NETWORK_DEVICE_LINK_CHANGED;
				break;

			default:
				BLooper::MessageReceived(message);
				return;
		}

		const char* name;
		if (message->FindString(kNMFieldInterface, &name) != B_OK)
			return;

		BMessage event(B_NETWORK_MONITOR);
		event.AddInt32("opcode", opcode);
		if (opcode == B_NETWORK_DEVICE_LINK_CHANGED) {
			event.AddString("device", name);
			event.AddInt32("media", 0);
			event.AddInt64("link speed", 0);
			event.AddInt32("link quality", 0);
		} else
			event.AddString("interface", name);

		uint32 group = (opcode & B_WATCH_NETWORK_INTERFACE_CHANGES) != 0
			? B_WATCH_NETWORK_INTERFACE_CHANGES : B_WATCH_NETWORK_LINK_CHANGES;
		for (size_t i = 0; i < fTargets.size(); i++) {
			if ((fTargets[i].flags & group) != 0)
				fTargets[i].messenger.SendMessage(&event);
		}
	}

private:
	status_t _Update()
	{
		NMBackend* backend = NMBackend::Instance();
		if (backend == NULL)
			return B_ERROR;

		if (!fTargets.empty() && !fWatching) {
			status_t status = backend->StartWatching(BMessenger(this),
				~(uint32)0);
			fWatching = status == B_OK;
			return status;
		}

		if (fTargets.empty() && fWatching) {
			backend->StopWatching(BMessenger(this));
			fWatching = false;
		}
		return B_OK;
	}

	struct Target {
		BMessenger	messenger;
		uint32		flags;
	};

	std::vector<Target>	fTargets;
	bool				fWatching;
};


static NetworkMonitorBridge*
network_monitor_bridge()
{
	static BLocker sLock;
	static NetworkMonitorBridge* sBridge = NULL;

	BAutolock locker(sLock);
	if (sBridge == NULL) {
		sBridge = new NetworkMonitorBridge;
		sBridge->Run();
	}
	return sBridge;
}

#endif	// __VOS__


static status_t
check_for_notifications_syscall(void)
{
#ifdef __VOS__
	// No generic-syscall interface under __VOS__: the NM-backed bridge above
	// delivers the notifications.
	return B_OK;
#else
	uint32 version = 0;
	return _kern_generic_syscall(NET_NOTIFICATIONS_SYSCALLS, B_SYSCALL_INFO,
		&version, sizeof(version));
#endif
}


//	#pragma mark -


status_t
start_watching_network(uint32 flags, const BMessenger& target)
{
	if (check_for_notifications_syscall() != B_OK)
		return B_NOT_SUPPORTED;

#ifdef __VOS__
	NetworkMonitorBridge* bridge = network_monitor_bridge();
	BAutolock locker(bridge);
	return bridge->SetTarget(target, flags);
#else
	BMessenger::Private targetPrivate(const_cast<BMessenger&>(target));
	net_notifications_control control;
	control.flags = flags;
	control.port = targetPrivate.Port();
	control.token = targetPrivate.Token();

	return _kern_generic_syscall(NET_NOTIFICATIONS_SYSCALLS,
		NET_NOTIFICATIONS_CONTROL_WATCHING, &control,
		sizeof(net_notifications_control));
#endif
}


status_t
start_watching_network(uint32 flags, const BHandler* handler,
	const BLooper* looper)
{
	const BMessenger target(handler, looper);
	return start_watching_network(flags, target);
}

status_t
stop_watching_network(const BMessenger& target)
{
	return start_watching_network(0, target);
		// start_watching_network() without flags just stops everything
}


status_t
stop_watching_network(const BHandler* handler, const BLooper* looper)
{
	const BMessenger target(handler, looper);
	return stop_watching_network(target);
}
