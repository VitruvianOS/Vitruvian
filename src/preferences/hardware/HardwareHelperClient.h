/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _HARDWARE_HELPER_CLIENT_H
#define _HARDWARE_HELPER_CLIENT_H


#include <Messenger.h>
#include <SupportDefs.h>


// Runs vos-hardware-helper through pkexec on a worker thread (the prompt blocks)
// and posts kMsgHelperResult; a denied prompt just yields "ok" false.


status_t
HardwareHelperRunAsync(const BMessenger& replyTo);


#endif	// _HARDWARE_HELPER_CLIENT_H
