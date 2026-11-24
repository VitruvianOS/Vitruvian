/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "IptablesBackend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char* kInChain = "VITRUVIAN_IN";
static const char* kOutChain = "VITRUVIAN_OUT";


IptablesBackend::IptablesBackend(FirewallRunner* runner)
	:
	FirewallBackend(runner)
{
}


const char*
IptablesBackend::Name() const
{
	return "iptables";
}


status_t
IptablesBackend::Probe()
{
	FirewallCommand command;
	command.verb = "iptables-list";

	BString output;
	int32 exitCode = -1;
	status_t status = fRunner->Run(command, output, exitCode);
	if (status != B_OK)
		return status;
	return exitCode == 0 ? B_OK : B_ERROR;
}


status_t
IptablesBackend::_ListRules(BString& output)
{
	FirewallCommand command;
	command.verb = "iptables-list";

	int32 exitCode = -1;
	status_t status = fRunner->Run(command, output, exitCode);
	if (status != B_OK)
		return status;
	if (exitCode != 0)
		return B_ERROR;

	return B_OK;
}


static bool
_HasChain(const BString& list, const char* chain)
{
	BString marker("-N ");
	marker << chain;
	return strstr(list.String(), marker.String()) != NULL;
}


static bool
_HasJump(const BString& list, const char* parent, const char* chain)
{
	// The jump is matched by our comment tag, never by position.
	BString marker("-A ");
	marker << parent;
	const char* scan = strstr(list.String(), marker.String());
	while (scan != NULL) {
		const char* end = strchr(scan, '\n');
		int32 length = end != NULL ? (int32)(end - scan) : (int32)strlen(scan);
		BString line(scan, length);
		if (line.FindFirst(chain) >= 0
			&& line.FindFirst("vitruvian-fw") >= 0) {
			return true;
		}
		if (end == NULL)
			break;
		scan = strstr(end + 1, marker.String());
	}
	return false;
}


static status_t
_RunVerb(FirewallRunner* runner, const char* verb,
	const std::vector<BString>& params, BString& error)
{
	FirewallCommand command;
	command.verb = verb;
	command.params = params;

	BString output;
	int32 exitCode = -1;
	status_t status = runner->Run(command, output, exitCode);
	if (status != B_OK || exitCode != 0) {
		error = output;
		if (error.Length() == 0)
			error = "iptables verb failed";
		return B_ERROR;
	}
	return B_OK;
}


static status_t
_SideVerb(FirewallRunner* runner, const char* verb, bool in,
	BString& error)
{
	std::vector<BString> params;
	params.push_back(BString(in ? "in" : "out"));
	return _RunVerb(runner, verb, params, error);
}


status_t
IptablesBackend::ApplyState(const FirewallState& state, BString& error)
{
	BString list;
	status_t status = _ListRules(list);
	if (status != B_OK) {
		error = "cannot list iptables rules";
		return status;
	}

	// Rebuild from zero: drop the jumps first so the chains are unused,
	// then delete and recreate them. The helper deletes a jump only
	// when it exists, so both calls are safe on a fresh system.
	status = _SideVerb(fRunner, "iptables-delete-jump", true, error);
	if (status != B_OK)
		return status;
	status = _SideVerb(fRunner, "iptables-delete-jump", false, error);
	if (status != B_OK)
		return status;

	const char* chains[2] = { kInChain, kOutChain };
	bool sides[2] = { true, false };
	for (int32 i = 0; i < 2; i++) {
		if (!_HasChain(list, chains[i]))
			continue;
		status = _SideVerb(fRunner, "iptables-flush-chain", sides[i],
			error);
		if (status != B_OK)
			return status;
		status = _SideVerb(fRunner, "iptables-delete-chain", sides[i],
			error);
		if (status != B_OK)
			return status;
	}

	if (!state.enabled)
		return B_OK;

	for (int32 i = 0; i < 2; i++) {
		status = _SideVerb(fRunner, "iptables-new-chain", sides[i],
			error);
		if (status != B_OK)
			return status;
	}

	for (size_t i = 0; i < state.rules.size(); i++) {
		const FirewallRule& rule = state.rules[i];
		std::vector<BString> params;
		params.push_back(BString(rule.allow ? "allow" : "deny"));
		if (rule.protocol == "tcp" || rule.protocol == "udp")
			params.push_back(rule.protocol);
		else
			params.push_back(BString("any"));

		char port[16];
		snprintf(port, sizeof(port), "%u", (unsigned)rule.port);
		params.push_back(BString(port));

		BString label(rule.label);
		if (label.Length() == 0)
			label = "rule";
		params.push_back(FirewallSanitizeLabel(label.String()));

		status = _RunVerb(fRunner, "iptables-add-rule", params, error);
		if (status != B_OK)
			return status;
	}

	// Last so it only logs packets no earlier rule matched.
	if (state.logging) {
		status = _RunVerb(fRunner, "iptables-add-log-rule",
			std::vector<BString>(), error);
		if (status != B_OK)
			return status;
	}

	int32 policies[2] = { state.inPolicy, state.outPolicy };
	for (int32 i = 0; i < 2; i++) {
		std::vector<BString> params;
		params.push_back(BString(sides[i] ? "in" : "out"));
		params.push_back(BString(firewall_policy_name(policies[i])));
		status = _RunVerb(fRunner, "iptables-add-terminal", params,
			error);
		if (status != B_OK)
			return status;
	}

	for (int32 i = 0; i < 2; i++) {
		status = _SideVerb(fRunner, "iptables-insert-jump", sides[i],
			error);
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


static bool
_Token(const char* line, int32 index, BString& token)
{
	const char* scan = line;
	for (int32 i = 0; i <= index; i++) {
		while (*scan == ' ')
			scan++;
		if (*scan == '\0')
			return false;
		const char* end = scan;
		while (*end != '\0' && *end != ' ')
			end++;
		if (i == index)
			token.SetTo(scan, (int32)(end - scan));
		scan = end;
	}
	return true;
}


static void
_ParseChainLine(const char* line, const char* chain, FirewallState& state,
	bool& sawTerminal)
{
	BString prefix("-A ");
	prefix << chain;
	if (strncmp(line, prefix.String(), prefix.Length()) != 0)
		return;

	int32 count = 0;
	BString token;
	BString protocol;
	BString target;
	bool isLog = false;
	bool hasDport = false;
	uint16 port = 0;
	BString label;

	while (_Token(line, count, token)) {
		if (token == "-p" && _Token(line, count + 1, protocol))
			count += 1;
		else if (token == "--dport" && _Token(line, count + 1, token)) {
			port = (uint16)atoi(token.String());
			hasDport = true;
		} else if (token == "--comment" && _Token(line, count + 1, label))
			count += 1;
		else if (token == "--log-prefix" && _Token(line, count + 1, token))
			isLog = true;
		else if (token == "-j" && _Token(line, count + 1, target))
			count += 1;
		count += 1;
	}

	if (isLog) {
		state.logging = true;
		return;
	}
	if (target.Length() == 0)
		return;

	if (target == "RETURN" || target == "ACCEPT" || target == "DROP") {
		// A bare terminal carries the policy; the same targets with a
		// match or comment are ordinary allow/deny rules.
		bool bare = label.Length() == 0 && protocol.Length() == 0
			&& !hasDport;
		if (bare) {
			if (strcmp(chain, kInChain) == 0)
				state.inPolicy = target == "DROP"
					? FIREWALL_POLICY_DROP : FIREWALL_POLICY_ACCEPT;
			else
				state.outPolicy = target == "DROP"
					? FIREWALL_POLICY_DROP : FIREWALL_POLICY_ACCEPT;
			sawTerminal = true;
			return;
		}
	}

	FirewallRule rule;
	rule.allow = target == "ACCEPT";
	rule.port = hasDport ? port : 0;
	if (protocol == "tcp" || protocol == "udp")
		rule.protocol = protocol;
	rule.label = label;
	state.rules.push_back(rule);
}


status_t
IptablesBackend::FetchState(FirewallState& state)
{
	state.enabled = false;
	state.inPolicy = FIREWALL_POLICY_ACCEPT;
	state.outPolicy = FIREWALL_POLICY_ACCEPT;
	state.logging = false;
	state.rules.clear();

	BString list;
	status_t status = _ListRules(list);
	if (status != B_OK)
		return status;

	if (!_HasChain(list, kInChain))
		return B_OK;

	state.enabled = _HasJump(list, "INPUT", kInChain);

	bool inTerminal = false;
	bool outTerminal = false;
	const char* line = list.String();
	while (*line != '\0') {
		const char* end = strchr(line, '\n');
		int32 length = end != NULL ? (int32)(end - line) : (int32)strlen(line);
		BString text(line, length);
		_ParseChainLine(text.String(), kInChain, state, inTerminal);
		_ParseChainLine(text.String(), kOutChain, state, outTerminal);
		if (end == NULL)
			break;
		line = end + 1;
	}

	return B_OK;
}
