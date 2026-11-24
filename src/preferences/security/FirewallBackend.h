/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FIREWALL_BACKEND_H
#define _FIREWALL_BACKEND_H


#include "FirewallModel.h"

#include <String.h>
#include <SupportDefs.h>

#include <vector>


// One privileged operation. A short verb plus validated parameters;
// the pkexec helpers own the tool argv, so no raw command option ever
// crosses the privilege boundary.
struct FirewallCommand {
	BString					verb;
	std::vector<BString>	params;
};


// True for verbs served by the read-only status helper.
bool firewall_verb_is_read_only(const char* verb);


// Abstraction over running the tool so host tests can fake it and the
// live test can exec directly instead of going through pkexec.
class FirewallRunner {
public:
	virtual					~FirewallRunner();

	virtual	status_t		Run(const FirewallCommand& command,
								BString& output, int32& exitCode) = 0;
};


class FirewallBackend {
public:
	explicit				FirewallBackend(FirewallRunner* runner);
	virtual					~FirewallBackend();

	virtual	const char*		Name() const = 0;

	// Can this backend really talk to the system right now?
	virtual	status_t		Probe() = 0;

	virtual	status_t		FetchState(FirewallState& state) = 0;

	// Single write path: full sync of the system to |state|.
	virtual	status_t		ApplyState(const FirewallState& state,
								BString& error) = 0;

protected:
			FirewallRunner*	fRunner;
};


// Probe, rank, return the first backend that works: nftables, then
// ufw, then iptables. |prefer|, when non-empty, is tried first
// (a stored settings value). NULL when no backend can run.
FirewallBackend* create_firewall_backend(FirewallRunner* runner,
	const char* prefer);

// The world-readable stamp the helper rewrites after every change, so
// the preflet and the Deskbar shield see each other's toggles.
extern const char* kFirewallStampDirectory;
extern const char* kFirewallStampPath;

bool FirewallParseStamp(const char* text, bool& enabled, int32& profile);
bool FirewallReadStamp(bool& enabled, int32& profile);

// Public blocks everything incoming: the list is not applied.
void FirewallEffectiveState(FirewallState& state);

// Rewrite the stamp through the helper; call after a successful apply.
status_t FirewallPublishState(FirewallRunner* runner,
	const FirewallState& state);

// NULL-safe helper for display strings.
const char* firewall_backend_name(FirewallBackend* backend);

// Labels reach nft comments and iptables comments; quotes and spaces
// would break the parsers, so they are folded to safe characters.
BString FirewallSanitizeLabel(const char* label);


#endif // _FIREWALL_BACKEND_H
