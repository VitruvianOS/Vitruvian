/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _UFW_BACKEND_H
#define _UFW_BACKEND_H


#include "FirewallBackend.h"


// Thin translation over the ufw CLI. Ranked first when installed: ufw
// then owns the ruleset and the preflet never writes iptables itself.
class UfwBackend : public FirewallBackend {
public:
	explicit				UfwBackend(FirewallRunner* runner);

	virtual	const char*		Name() const;
	virtual	status_t		Probe();
	virtual	status_t		FetchState(FirewallState& state);
	virtual	status_t		ApplyState(const FirewallState& state,
								BString& error);
};


#endif // _UFW_BACKEND_H
