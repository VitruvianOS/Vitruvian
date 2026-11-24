/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FIREWALL_SERVICES_H
#define _FIREWALL_SERVICES_H


#include "FirewallModel.h"

#include <vector>


struct FirewallService {
	const char*	label;			// stored in the rule, also the lookup key
	const char*	name;			// untranslated display name
	const char*	protocols;		// "tcp", "udp" or "tcp,udp"
	uint16		ports[2];		// 0 ends the list
};


// The services the Add dialog offers.
const FirewallService* FirewallServiceAt(int32 index);
const FirewallService* FirewallServiceFor(const char* label);

// Rules for the known services that currently listen on this machine.
void FirewallListeningRules(std::vector<FirewallRule>& rules);


#endif // _FIREWALL_SERVICES_H
