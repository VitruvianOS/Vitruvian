/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "PortWaiter.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>


static const int kPortTag = -1;
static const int kTimerTag = -2;


PortWaiter::PortWaiter()
	:
	fEpoll(-1),
	fPortFd(-1),
	fTimer(-1),
	fArmed(false),
	fLastTag(0)
{
}


PortWaiter::~PortWaiter()
{
	if (fTimer >= 0)
		close(fTimer);
	if (fPortFd >= 0)
		close(fPortFd);
	if (fEpoll >= 0)
		close(fEpoll);
}


bool
PortWaiter::SetPort(port_id port)
{
	if (fEpoll >= 0)
		return true;

	int portFd = port_to_fd(port);
	if (portFd < 0)
		return false;

	int epollFd = epoll_create1(EPOLL_CLOEXEC);
	int timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (epollFd < 0 || timer < 0) {
		if (epollFd >= 0)
			close(epollFd);
		close(portFd);
		return false;
	}

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = kPortTag;
	bool ok = epoll_ctl(epollFd, EPOLL_CTL_ADD, portFd, &event) == 0;
	event.data.fd = kTimerTag;
	ok = ok && epoll_ctl(epollFd, EPOLL_CTL_ADD, timer, &event) == 0;
	if (!ok) {
		close(timer);
		close(epollFd);
		close(portFd);
		return false;
	}

	fEpoll = epollFd;
	fPortFd = portFd;
	fTimer = timer;
	return true;
}


status_t
PortWaiter::AddFd(int fd, int tag)
{
	if (fEpoll < 0)
		return B_NO_INIT;

	struct epoll_event event;
	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = tag;
	return epoll_ctl(fEpoll, EPOLL_CTL_ADD, fd, &event) == 0 ? B_OK : B_ERROR;
}


void
PortWaiter::_Disarm()
{
	if (!fArmed)
		return;
	struct itimerspec spec;
	memset(&spec, 0, sizeof(spec));
	timerfd_settime(fTimer, 0, &spec, NULL);
	fArmed = false;
}


PortWaiter::result
PortWaiter::Wait(bigtime_t deadline)
{
	if (fEpoll < 0)
		return kError;

	if (deadline != B_INFINITE_TIMEOUT) {
		bigtime_t left = deadline - system_time();
		if (left < 1)
			left = 1;
		struct itimerspec spec;
		memset(&spec, 0, sizeof(spec));
		spec.it_value.tv_sec = left / 1000000;
		spec.it_value.tv_nsec = (left % 1000000) * 1000;
		if (timerfd_settime(fTimer, 0, &spec, NULL) != 0)
			return kError;
		fArmed = true;
	} else
		_Disarm();

	while (true) {
		struct epoll_event event;
		int count = epoll_wait(fEpoll, &event, 1, -1);
		if (count < 0) {
			if (errno == EINTR)
				continue;
			return kError;
		}
		if (count == 0)
			continue;

		if (event.data.fd == kTimerTag) {
			uint64_t expirations;
			(void)read(fTimer, &expirations, sizeof(expirations));
			fArmed = false;
			return kDeadline;
		}
		_Disarm();
		if (event.data.fd == kPortTag) {
			// HUP alone: the read reports the deleted port
			return kPortReadable;
		}
		fLastTag = event.data.fd;
		return kFdReadable;
	}
}
