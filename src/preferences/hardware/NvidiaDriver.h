/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NVIDIA_DRIVER_H
#define _NVIDIA_DRIVER_H


#include <Messenger.h>
#include <SupportDefs.h>


// Posted when the helper run finishes: "ok" bool, "text" string (the tail
// of the helper output). A dismissed authentication prompt is not an error:
// "cancelled" is true and "ok" false.
enum {
	kMsgNvidiaDriverResult = 'HWnv'
};


// True when this machine may use the proprietary driver, or already does.
bool	NvidiaDriverAvailable();

// True when the proprietary lane is configured.
bool	NvidiaDriverEnabled();

// True on a PREEMPT_RT kernel, which NVIDIA does not support.
bool	NvidiaKernelIsRealtime();

// Runs vos-nvidia-enable through pkexec on a worker thread; acceptRealtime
// maps to --accept-rt and is ignored when disabling.
status_t	NvidiaDriverApplyAsync(bool enable, bool acceptRealtime,
				const BMessenger& replyTo);

// Forces the next GL client to probe the renderers again.
void	NvidiaDriverForgetRendererSelection();


#endif	// _NVIDIA_DRIVER_H
