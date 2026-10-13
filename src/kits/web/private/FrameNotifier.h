/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_FRAME_NOTIFIER_H
#define VOS_FRAME_NOTIFIER_H


#include <fcntl.h>
#include <unistd.h>


struct FrameNotifier {
					FrameNotifier();
					~FrameNotifier();

		// True when this write found the pipe empty: the caller
		// posts the one frame message the drain will consume.
		bool		Notify();

		void		Drain();

		int			ReadFd() const { return fFds[0]; }
		int			WriteFd() const { return fFds[1]; }

private:
		int			fFds[2];
};


#endif // VOS_FRAME_NOTIFIER_H
