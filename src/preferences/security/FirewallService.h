/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FIREWALL_SERVICE_H
#define _FIREWALL_SERVICE_H


#include "FirewallModel.h"

#include <Message.h>
#include <Messenger.h>
#include <SupportDefs.h>


// Firewall work runs on its own thread so a polkit prompt cannot freeze
// the preflet (same shape as the Time preflet's timedated worker). The
// result message carries "op", "status" and, on failure, "error".
enum {
	kFirewallOpFetch = 'fwfs',
	kFirewallOpApply = 'fwap',
	kFirewallOpUnlock = 'fwun',
};

const uint32 kFirewallResultWhat = 'fwrs';

// Unlock asks the admin helper once so later edits do not prompt again.
// args: "op"; plus "prefer" (backend name override); when applying, the
// state fields written by FirewallPutState().
status_t FirewallAsyncRun(int32 op, const BMessage& args,
	const BMessenger& replyTo);

// State <-> message helpers shared by the preflet and the replicant.
void FirewallPutState(BMessage& message, const FirewallState& state);
void FirewallPutRule(BMessage& message, const FirewallRule& rule);
void FirewallGetState(const BMessage& message, FirewallState& state);
bool FirewallGetRule(const BMessage& message, int32 index,
	FirewallRule& rule);
int32 FirewallCountRules(const BMessage& message);

// Per user settings file (state, profile and the kept list).
void FirewallLoadSettings(FirewallState& state, bool& seeded);
void FirewallSaveSettings(const FirewallState& state, bool seeded);

// Node monitor messages for the shared state stamp go to |target|.
void FirewallWatchStamp(const BMessenger& target);
void FirewallStopWatchingStamp(const BMessenger& target);


#endif // _FIREWALL_SERVICE_H
