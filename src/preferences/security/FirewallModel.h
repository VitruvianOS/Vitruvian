/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FIREWALL_MODEL_H
#define _FIREWALL_MODEL_H


#include <String.h>
#include <SupportDefs.h>

#include <vector>


// Rules always filter incoming traffic; outgoing traffic is governed by
// the default outgoing policy alone. This mirrors what ufw exposes and
// keeps the model small enough to translate into nft and iptables.
enum {
	FIREWALL_POLICY_ACCEPT = 0,
	FIREWALL_POLICY_DROP,
};


// Home applies the allowed list; Public blocks all incoming traffic and
// keeps the list for the way back.
enum {
	FIREWALL_PROFILE_HOME = 0,
	FIREWALL_PROFILE_PUBLIC,
};


struct FirewallRule {
	bool		allow;
	BString		protocol;	// "tcp", "udp", or empty for any
	uint16		port;		// 0 for any
	BString		label;		// application name, display only
	BString		handle;		// backend rule handle when known
};


struct FirewallState {
	FirewallState()
		:
		enabled(false),
		inPolicy(FIREWALL_POLICY_ACCEPT),
		outPolicy(FIREWALL_POLICY_ACCEPT),
		logging(false),
		profile(FIREWALL_PROFILE_HOME)
	{
	}

	bool						enabled;
	int32						inPolicy;	// FIREWALL_POLICY_*
	int32						outPolicy;	// FIREWALL_POLICY_*
	bool						logging;
	int32						profile;	// FIREWALL_PROFILE_*
	std::vector<FirewallRule>	rules;
};


const char* firewall_policy_name(int32 policy);


#endif // _FIREWALL_MODEL_H
