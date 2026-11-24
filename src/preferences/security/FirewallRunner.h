/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FIREWALL_RUNNER_H
#define _FIREWALL_RUNNER_H


#include "FirewallBackend.h"


// Runs structured verbs through pkexec, the same privilege path the
// Users preflet uses for vos-admin-helper. Read-only
// verbs go through the status helper, whose polkit action lets an
// active session run it without a prompt; both helpers build the tool
// argv themselves and never forward caller arguments.
class PrivilegedFirewallRunner : public FirewallRunner {
public:
						PrivilegedFirewallRunner() : fCanceled(false) {}
	virtual					~PrivilegedFirewallRunner();

	virtual	status_t		Run(const FirewallCommand& command,
								BString& output, int32& exitCode);

			// True when the last Run() ended at a dismissed prompt.
			bool			Canceled() const { return fCanceled; }

private:
			bool			fCanceled;
};


#endif // _FIREWALL_RUNNER_H
