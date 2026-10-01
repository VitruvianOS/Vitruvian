/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "frame_notifier.h"

#include "logging.h"

#include <cstdint>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


//	#pragma mark - Constructor/Destructor


FrameNotifier::FrameNotifier()
{
	fFds[0] = -1;
	fFds[1] = -1;

	int ret = pipe(fFds);
	if (ret != 0) {
		log_error("FrameNotifier: pipe() failed: %s", strerror(errno));
		return;
	}

	fcntl(fFds[0], F_SETFL, O_NONBLOCK);
	fcntl(fFds[1], F_SETFL, O_NONBLOCK);

	log_info("FrameNotifier: created pipe r=%d w=%d", fFds[0], fFds[1]);
}


FrameNotifier::~FrameNotifier()
{
	if (fFds[0] >= 0)
		close(fFds[0]);
	if (fFds[1] >= 0)
		close(fFds[1]);
}


//	#pragma mark - Public


void
FrameNotifier::Notify()
{
	if (fFds[1] < 0)
		return;

	// Non-blocking write: if the pipe is full, a wakeup is already pending
	// and the drop is intentional — the reader drains all bytes and posts
	// exactly one MSG_NEW_FRAME, so coalescing is correct by design.
	uint8_t byte = 1;
	ssize_t ret = write(fFds[1], &byte, 1);
	if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		log_error("FrameNotifier::Notify write failed: %s", strerror(errno));
}


void
FrameNotifier::Drain()
{
	if (fFds[0] < 0)
		return;

	uint8_t buf[32];
	while (read(fFds[0], buf, sizeof(buf)) > 0) {}
}
