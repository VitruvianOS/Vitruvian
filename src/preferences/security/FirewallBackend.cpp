/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "FirewallBackend.h"

#include "IptablesBackend.h"
#include "NftablesBackend.h"
#include "UfwBackend.h"

#include <String.h>

#include <stdio.h>
#include <string.h>


const char* kFirewallStampDirectory = "/run/vos-firewall";
const char* kFirewallStampPath = "/run/vos-firewall/state";


const char*
firewall_policy_name(int32 policy)
{
	if (policy == FIREWALL_POLICY_DROP)
		return "drop";

	return "accept";
}


bool
firewall_verb_is_read_only(const char* verb)
{
	// Mirrors the status helper's fixed verb table.
	static const char* kReadOnly[] = {
		"nft-list-tables",
		"nft-list-table",
		"ufw-status",
		"ufw-status-numbered",
		"iptables-list",
		"iptables-list-input",
		"iptables-list-output",
	};

	if (verb == NULL)
		return false;

	for (size_t i = 0; i < sizeof(kReadOnly) / sizeof(kReadOnly[0]); i++) {
		if (strcmp(verb, kReadOnly[i]) == 0)
			return true;
	}

	return false;
}


FirewallRunner::~FirewallRunner()
{
}


FirewallBackend::FirewallBackend(FirewallRunner* runner)
	:
	fRunner(runner)
{
}


FirewallBackend::~FirewallBackend()
{
}


static FirewallBackend*
_CreateBackend(const char* name, FirewallRunner* runner)
{
	if (!strcmp(name, "nftables"))
		return new NftablesBackend(runner);
	if (!strcmp(name, "iptables"))
		return new IptablesBackend(runner);
	if (!strcmp(name, "ufw"))
		return new UfwBackend(runner);

	return NULL;
}


FirewallBackend*
create_firewall_backend(FirewallRunner* runner, const char* prefer)
{
	if (runner == NULL)
		return NULL;

	// A stored preference wins outright when it still probes clean.
	if (prefer != NULL && prefer[0] != '\0') {
		FirewallBackend* backend = _CreateBackend(prefer, runner);
		if (backend != NULL) {
			if (backend->Probe() == B_OK)
				return backend;
			delete backend;
		}
	}

	static const char* kRank[] = { "nftables", "ufw", "iptables" };
	for (int32 i = 0; i < 3; i++) {
		if (prefer != NULL && !strcmp(kRank[i], prefer))
			continue;
		FirewallBackend* backend = _CreateBackend(kRank[i], runner);
		if (backend == NULL)
			continue;
		if (backend->Probe() == B_OK)
			return backend;
		delete backend;
	}

	return NULL;
}


const char*
firewall_backend_name(FirewallBackend* backend)
{
	if (backend == NULL)
		return "none";

	return backend->Name();
}


BString
FirewallSanitizeLabel(const char* label)
{
	BString result;
	if (label == NULL)
		return result;

	for (const char* c = label; *c != '\0'; c++) {
		if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
			|| (*c >= '0' && *c <= '9') || *c == '.' || *c == '_'
			|| *c == '-' || *c == '+') {
			result << *c;
		} else if (*c == ' ')
			result << '-';
	}
	return result;
}


bool
FirewallParseStamp(const char* text, bool& enabled, int32& profile)
{
	if (text == NULL)
		return false;

	bool haveEnabled = false;
	bool haveProfile = false;
	for (const char* line = text; *line != '\0';) {
		if (strncmp(line, "enabled=on", 10) == 0) {
			enabled = true;
			haveEnabled = true;
		} else if (strncmp(line, "enabled=off", 11) == 0) {
			enabled = false;
			haveEnabled = true;
		} else if (strncmp(line, "profile=home", 12) == 0) {
			profile = FIREWALL_PROFILE_HOME;
			haveProfile = true;
		} else if (strncmp(line, "profile=public", 14) == 0) {
			profile = FIREWALL_PROFILE_PUBLIC;
			haveProfile = true;
		}
		const char* next = strchr(line, '\n');
		if (next == NULL)
			break;
		line = next + 1;
	}
	return haveEnabled && haveProfile;
}


bool
FirewallReadStamp(bool& enabled, int32& profile)
{
	FILE* file = fopen(kFirewallStampPath, "r");
	if (file == NULL)
		return false;

	char text[128];
	size_t length = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[length] = '\0';
	return FirewallParseStamp(text, enabled, profile);
}


void
FirewallEffectiveState(FirewallState& state)
{
	if (state.profile == FIREWALL_PROFILE_PUBLIC)
		state.rules.clear();
}


status_t
FirewallPublishState(FirewallRunner* runner, const FirewallState& state)
{
	if (runner == NULL)
		return B_BAD_VALUE;

	FirewallCommand command;
	command.verb = "write-state";
	command.params.push_back(BString(state.enabled ? "on" : "off"));
	command.params.push_back(BString(
		state.profile == FIREWALL_PROFILE_PUBLIC ? "public" : "home"));

	BString output;
	int32 exitCode = -1;
	status_t status = runner->Run(command, output, exitCode);
	if (status != B_OK)
		return status;
	return exitCode == 0 ? B_OK : B_ERROR;
}
