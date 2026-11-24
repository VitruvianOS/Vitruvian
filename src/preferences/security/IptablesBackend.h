/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _IPTABLES_BACKEND_H
#define _IPTABLES_BACKEND_H


#include "FirewallBackend.h"


// Owns a private chain pair (VITRUVIAN_IN / VITRUVIAN_OUT) jumped from
// INPUT and OUTPUT by a comment-tagged jump rule. IPv4 only; IPv6 is a
// known gap noted in the report.
class IptablesBackend : public FirewallBackend {
public:
	explicit				IptablesBackend(FirewallRunner* runner);

	virtual	const char*		Name() const;
	virtual	status_t		Probe();
	virtual	status_t		FetchState(FirewallState& state);
	virtual	status_t		ApplyState(const FirewallState& state,
								BString& error);

private:
			status_t		_ListRules(BString& output);
};


#endif // _IPTABLES_BACKEND_H
