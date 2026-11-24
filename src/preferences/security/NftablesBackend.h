/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NFTABLES_BACKEND_H
#define _NFTABLES_BACKEND_H


#include "FirewallBackend.h"


// Owns a private table (inet vitruvian) with one base chain per
// direction so it never fights rules another component installed.
class NftablesBackend : public FirewallBackend {
public:
	explicit				NftablesBackend(FirewallRunner* runner);

	virtual	const char*		Name() const;
	virtual	status_t		Probe();
	virtual	status_t		FetchState(FirewallState& state);
	virtual	status_t		ApplyState(const FirewallState& state,
								BString& error);

private:
			status_t		_TableExists(bool& exists);
};


#endif // _NFTABLES_BACKEND_H
