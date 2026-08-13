/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PidWatcher.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <Autolock.h>
#include <Message.h>
#include <system_info.h>


#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif


static int
pidfd_open(pid_t pid)
{
	return (int)syscall(SYS_pidfd_open, pid, 0);
}


PidWatcher::PidWatcher(const BMessenger& target)
	:
	fTarget(target),
	fEpollFd(-1),
	fWakeFd(-1),
	fThread(-1),
	fRunning(false),
	fAvailable(false),
	fLock("pid watcher")
{
}


PidWatcher::~PidWatcher()
{
	Stop();

	for (HashMap<HashKey32<team_id>, int>::Iterator it = fPidFds.GetIterator();
			it.HasNext();) {
		close(it.Next().value);
	}

	if (fEpollFd >= 0)
		close(fEpollFd);
	if (fWakeFd >= 0)
		close(fWakeFd);
}


status_t
PidWatcher::Start()
{
	// Availability probe: pidfd_open not always present; falls back to __start_watching_system.
	int probe = pidfd_open(getpid());
	if (probe < 0) {
		fAvailable = false;
		return B_UNSUPPORTED;
	}
	close(probe);

	fEpollFd = epoll_create1(EPOLL_CLOEXEC);
	if (fEpollFd < 0)
		return errno == 0 ? B_ERROR : (status_t)-errno;

	fWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (fWakeFd < 0) {
		close(fEpollFd);
		fEpollFd = -1;
		return -errno;
	}

	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.u64 = (uint64)-1;	// sentinel: wake-up eventfd
	if (epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fWakeFd, &ev) < 0) {
		close(fEpollFd);
		close(fWakeFd);
		fEpollFd = fWakeFd = -1;
		return -errno;
	}

	fAvailable = true;
	fRunning = true;
	fThread = spawn_thread(_ThreadEntry, "pid_watcher", B_NORMAL_PRIORITY, this);
	if (fThread < 0) {
		fRunning = false;
		close(fEpollFd);
		close(fWakeFd);
		fEpollFd = fWakeFd = -1;
		return fThread;
	}
	resume_thread(fThread);
	return B_OK;
}


void
PidWatcher::Stop()
{
	if (!fRunning)
		return;
	fRunning = false;
	if (fWakeFd >= 0) {
		uint64 one = 1;
		if (write(fWakeFd, &one, sizeof(one)) < 0) { /* thread will time out */ }
	}
	if (fThread >= 0) {
		status_t exit;
		wait_for_thread(fThread, &exit);
		fThread = -1;
	}
}


void
PidWatcher::Add(team_id team)
{
	if (!fAvailable || team < 0)
		return;

	BAutolock _(fLock);

	// Skip if already watching this team.
	int* existing = NULL;
	if (fPidFds.Get(team, existing) && existing != NULL)
		return;

	int fd = pidfd_open((pid_t)team);
	if (fd < 0) {
		// Only ESRCH proves the team is gone. Any other failure says nothing
		// about it, and reporting a death would drop a live app's registration.
		if (errno == ESRCH) {
			_DeliverDeath(team);
			return;
		}
		fprintf(stderr, "PidWatcher: pidfd_open(team=%" B_PRId32 "): %s\n",
			team, strerror(errno));
		return;
	}

	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.u64 = (uint64)(uint32)team;
	if (epoll_ctl(fEpollFd, EPOLL_CTL_ADD, fd, &ev) < 0) {
		fprintf(stderr, "PidWatcher: epoll_ctl(ADD, team=%" B_PRId32 "): %s\n",
			team, strerror(errno));
		close(fd);
		return;
	}
	fPidFds.Put(team, fd);
}


void
PidWatcher::Remove(team_id team)
{
	if (!fAvailable)
		return;

	BAutolock _(fLock);
	int* fd = NULL;
	if (!fPidFds.Get(team, fd) || fd == NULL)
		return;
	epoll_ctl(fEpollFd, EPOLL_CTL_DEL, *fd, NULL);
	close(*fd);
	fPidFds.Remove(team);
}


void
PidWatcher::_DeliverDeath(team_id team)
{
	BMessage message(B_SYSTEM_OBJECT_UPDATE);
	message.AddInt32("opcode", B_TEAM_DELETED);
	message.AddInt32("team", team);
	fTarget.SendMessage(&message);
}


int32
PidWatcher::_ThreadEntry(void* self)
{
	return ((PidWatcher*)self)->_ThreadLoop();
}


int32
PidWatcher::_ThreadLoop()
{
	struct epoll_event events[16];

	while (fRunning) {
		int n = epoll_wait(fEpollFd, events, 16, -1);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}

		for (int i = 0; i < n; i++) {
			uint64 tag = events[i].data.u64;
			if (tag == (uint64)-1) {
				// Wake-up eventfd — just re-check fRunning.
				uint64 val;
				while (read(fWakeFd, &val, sizeof(val)) > 0) {}
				continue;
			}

			team_id team = (team_id)(int32)tag;

			fLock.Lock();
			int* fd = NULL;
			if (fPidFds.Get(team, fd) && fd != NULL) {
				epoll_ctl(fEpollFd, EPOLL_CTL_DEL, *fd, NULL);
				close(*fd);
				fPidFds.Remove(team);
			}
			fLock.Unlock();

			_DeliverDeath(team);
		}
	}
	return 0;
}
