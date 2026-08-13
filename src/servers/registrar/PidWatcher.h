/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PID_WATCHER_H
#define _PID_WATCHER_H


#include <Locker.h>
#include <Messenger.h>
#include <OS.h>

#include <HashMap.h>


// Watches registered app teams for exit via pidfd + epoll and delivers the
// same B_SYSTEM_OBJECT_UPDATE/B_TEAM_DELETED message __start_watching_system
// would, so TRoster's existing handling stays untouched. Falls back to
// letting the caller rely on the system watcher if pidfd_open() isn't
// available (kernel too old).
class PidWatcher {
public:
						PidWatcher(const BMessenger& target);
						~PidWatcher();

	status_t			Start();
	void				Stop();

	// True if pidfd_open() is usable on this kernel. When false, the
	// registrar must keep using __start_watching_system for coverage.
	bool				IsAvailable() const { return fAvailable; }

	// Begins watching team for exit. Delivers the death message inline
	// (before returning) if the team is already gone by the time this
	// is called.
	void				Add(team_id team);

	// Stops watching team; no-op if it isn't watched.
	void				Remove(team_id team);

private:
	static int32		_ThreadEntry(void* self);
	int32				_ThreadLoop();

	void				_DeliverDeath(team_id team);

	BMessenger			fTarget;
	int					fEpollFd;
	int					fWakeFd;		// eventfd, breaks epoll_wait on Stop()
	thread_id			fThread;
	bool				fRunning;
	bool				fAvailable;

	BLocker				fLock;
	HashMap<HashKey32<team_id>, int>	fPidFds;	// team -> pidfd
};


#endif	// _PID_WATCHER_H
