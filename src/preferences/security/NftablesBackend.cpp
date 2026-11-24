/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "NftablesBackend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char* kFamily = "inet";
static const char* kTable = "vitruvian";


NftablesBackend::NftablesBackend(FirewallRunner* runner)
	:
	FirewallBackend(runner)
{
}


const char*
NftablesBackend::Name() const
{
	return "nftables";
}


status_t
NftablesBackend::Probe()
{
	FirewallCommand command;
	command.verb = "nft-list-tables";

	BString output;
	int32 exitCode = -1;
	status_t status = fRunner->Run(command, output, exitCode);
	if (status != B_OK)
		return status;
	return exitCode == 0 ? B_OK : B_ERROR;
}


status_t
NftablesBackend::_TableExists(bool& exists)
{
	exists = false;

	FirewallCommand command;
	command.verb = "nft-list-tables";

	BString output;
	int32 exitCode = -1;
	status_t status = fRunner->Run(command, output, exitCode);
	if (status != B_OK)
		return status;

	BString marker("table ");
	marker << kFamily << " " << kTable;
	if (strstr(output.String(), marker.String()) != NULL)
		exists = true;

	return B_OK;
}


static void
_AddRuleParams(FirewallCommand& command, const FirewallRule& rule)
{
	command.params.push_back(BString(rule.allow ? "allow" : "deny"));
	if (rule.protocol == "tcp" || rule.protocol == "udp")
		command.params.push_back(rule.protocol);
	else
		command.params.push_back(BString("any"));

	char port[16];
	snprintf(port, sizeof(port), "%u", (unsigned)rule.port);
	command.params.push_back(BString(port));

	BString label(rule.label);
	if (label.Length() == 0) {
		label = rule.protocol;
		if (rule.port != 0) {
			char portText[16];
			snprintf(portText, sizeof(portText), "%u",
				(unsigned)rule.port);
			label << " " << portText;
		}
		if (label.Length() == 0)
			label = "any";
	}
	command.params.push_back(FirewallSanitizeLabel(label.String()));
}


status_t
NftablesBackend::ApplyState(const FirewallState& state, BString& error)
{
	bool exists = false;
	status_t status = _TableExists(exists);
	if (status != B_OK) {
		error = "cannot list nft tables";
		return status;
	}

	if (!state.enabled) {
		if (!exists)
			return B_OK;

		FirewallCommand command;
		command.verb = "nft-delete-table";

		BString output;
		int32 exitCode = -1;
		status = fRunner->Run(command, output, exitCode);
		if (status != B_OK || exitCode != 0) {
			error = output;
			if (error.Length() == 0)
				error = "nft delete table failed";
			return B_ERROR;
		}
		return B_OK;
	}

	// Recreate the table from scratch so ApplyState is one idempotent
	// write path no matter what the ruleset looked like before.
	if (exists) {
		FirewallCommand command;
		command.verb = "nft-delete-table";

		BString output;
		int32 exitCode = -1;
		status = fRunner->Run(command, output, exitCode);
		if (status != B_OK || exitCode != 0) {
			error = output;
			if (error.Length() == 0)
				error = "nft delete table failed";
			return B_ERROR;
		}
	}

	FirewallCommand table;
	table.verb = "nft-add-table";

	BString output;
	int32 exitCode = -1;
	status = fRunner->Run(table, output, exitCode);
	if (status != B_OK || exitCode != 0) {
		error = output;
		if (error.Length() == 0)
			error = "nft add table failed";
		return B_ERROR;
	}

	const char* sides[2] = { "in", "out" };
	int32 policies[2] = { state.inPolicy, state.outPolicy };
	for (int32 i = 0; i < 2; i++) {
		FirewallCommand chain;
		chain.verb = "nft-add-chain";
		chain.params.push_back(BString(sides[i]));
		chain.params.push_back(BString(firewall_policy_name(policies[i])));

		status = fRunner->Run(chain, output, exitCode);
		if (status != B_OK || exitCode != 0) {
			error = output;
			if (error.Length() == 0)
				error = "nft add chain failed";
			return B_ERROR;
		}
	}

	for (size_t i = 0; i < state.rules.size(); i++) {
		FirewallCommand rule;
		rule.verb = "nft-add-rule";
		_AddRuleParams(rule, state.rules[i]);

		status = fRunner->Run(rule, output, exitCode);
		if (status != B_OK || exitCode != 0) {
			error = output;
			if (error.Length() == 0)
				error = "nft add rule failed";
			return B_ERROR;
		}
	}

	// Last so it only logs packets no earlier rule matched.
	if (state.logging) {
		FirewallCommand log;
		log.verb = "nft-add-log-rule";

		status = fRunner->Run(log, output, exitCode);
		if (status != B_OK || exitCode != 0) {
			error = output;
			if (error.Length() == 0)
				error = "nft add log rule failed";
			return B_ERROR;
		}
	}

	return B_OK;
}


// Extract the n-th whitespace-separated token from |line| into |token|.
static bool
_Token(const char* line, int32 index, BString& token)
{
	const char* scan = line;
	for (int32 i = 0; i <= index; i++) {
		while (*scan == ' ' || *scan == '\t')
			scan++;
		if (*scan == '\0')
			return false;
		const char* end = scan;
		while (*end != '\0' && *end != ' ' && *end != '\t')
			end++;
		if (i == index)
			token.SetTo(scan, (int32)(end - scan));
		scan = end;
	}
	return true;
}


static void
_ParseRuleLine(const char* line, FirewallState& state)
{
	if (strstr(line, "log prefix") != NULL) {
		state.logging = true;
		return;
	}

	FirewallRule rule;
	rule.allow = strstr(line, " accept") != NULL;
	if (!rule.allow && strstr(line, " drop") == NULL)
		return;
	rule.port = 0;

	// nft prints "tcp dport N" or the older "meta l4proto tcp tcp dport N";
	// the protocol is the token before "dport" in both forms.
	BString previous;
	BString token;
	for (int32 index = 0; index < 16 && _Token(line, index, token);
			index++) {
		if (token == "dport") {
			BString port;
			if (_Token(line, index + 1, port))
				rule.port = (uint16)atoi(port.String());
			rule.protocol = previous;
			break;
		}
		previous = token;
	}

	const char* comment = strstr(line, "comment \"");
	if (comment != NULL) {
		comment += strlen("comment \"");
		const char* end = strchr(comment, '"');
		if (end != NULL)
			rule.label.SetTo(comment, (int32)(end - comment));
	}

	const char* handle = strstr(line, "# handle ");
	if (handle != NULL)
		rule.handle = handle + strlen("# handle ");

	state.rules.push_back(rule);
}


status_t
NftablesBackend::FetchState(FirewallState& state)
{
	state.enabled = false;
	state.inPolicy = FIREWALL_POLICY_ACCEPT;
	state.outPolicy = FIREWALL_POLICY_ACCEPT;
	state.logging = false;
	state.rules.clear();

	FirewallCommand command;
	command.verb = "nft-list-table";

	BString output;
	int32 exitCode = -1;
	status_t status = fRunner->Run(command, output, exitCode);
	if (status != B_OK)
		return status;
	if (exitCode != 0) {
		// The only accepted failure is "table absent": that is the
		// disabled state, everything else must surface to the caller.
		if (strstr(output.String(), "No such file or directory") != NULL
			|| strstr(output.String(), "does not exist") != NULL
			|| output.Length() == 0) {
			return B_OK;
		}
		return B_ERROR;
	}

	state.enabled = true;

	const char* line = output.String();
	while (*line != '\0') {
		const char* end = strchr(line, '\n');
		int32 length = end != NULL ? (int32)(end - line) : (int32)strlen(line);

		BString text(line, length);
		if (strstr(text.String(), "chain input {") != NULL)
			; // input chain header, policy comes on the next line
		else if (strstr(text.String(), "policy drop") != NULL
			&& strstr(text.String(), "hook input") != NULL)
			state.inPolicy = FIREWALL_POLICY_DROP;
		else if (strstr(text.String(), "policy accept") != NULL
			&& strstr(text.String(), "hook input") != NULL)
			state.inPolicy = FIREWALL_POLICY_ACCEPT;
		else if (strstr(text.String(), "hook output") != NULL
			&& strstr(text.String(), "policy drop") != NULL)
			state.outPolicy = FIREWALL_POLICY_DROP;
		else if (strstr(text.String(), "hook output") != NULL
			&& strstr(text.String(), "policy accept") != NULL)
			state.outPolicy = FIREWALL_POLICY_ACCEPT;
		else if (strstr(text.String(), "accept") != NULL
			|| strstr(text.String(), "drop") != NULL
			|| strstr(text.String(), "log prefix") != NULL)
			_ParseRuleLine(text.String(), state);

		if (end == NULL)
			break;
		line = end + 1;
	}

	return B_OK;
}
