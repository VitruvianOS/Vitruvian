/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FIREWALL_RUNNER_H
#define _FIREWALL_RUNNER_H


#include "FirewallBackend.h"


// Runs structured verbs through pkexec, the same privilege path the
// User and Administrator preflets use for vos-admin-helper. Read-only
// verbs go through the status helper, whose polkit action lets an
// active session run it without a prompt; both helpers build the tool
// argv themselves and never forward caller arguments.
class PrivilegedFirewallRunner : public FirewallRunner {
public:
	virtual					~PrivilegedFirewallRunner();

	virtual	status_t		Run(const FirewallCommand& command,
								BString& output, int32& exitCode);
};


#endif // _FIREWALL_RUNNER_H
