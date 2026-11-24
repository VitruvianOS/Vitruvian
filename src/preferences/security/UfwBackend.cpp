/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "UfwBackend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


UfwBackend::UfwBackend(FirewallRunner* runner)
	:
	FirewallBackend(runner)
{
}


const char*
UfwBackend::Name() const
{
	return "ufw";
}


status_t
UfwBackend::Probe()
{
	FirewallCommand command;
	command.verb = "ufw-status";

	BString output;
	int32 exitCode = -1;
	status_t status = fRunner->Run(command, output, exitCode);
	if (status != B_OK)
		return status;
	return exitCode == 0 ? B_OK : B_ERROR;
}


status_t
UfwBackend::FetchState(FirewallState& state)
{
	state.enabled = false;
	state.inPolicy = FIREWALL_POLICY_DROP;
	state.outPolicy = FIREWALL_POLICY_ACCEPT;
	state.logging = false;
	state.rules.clear();

	FirewallCommand command;
	command.verb = "ufw-status-numbered";

	BString output;
	int32 exitCode = -1;
	status_t status = fRunner->Run(command, output, exitCode);
	if (status != B_OK)
		return status;
	if (exitCode != 0) {
		if (strstr(output.String(), "inactive") != NULL)
			return B_OK;
		return B_ERROR;
	}

	if (strstr(output.String(), "Status: active") != NULL)
		state.enabled = true;

	const char* incoming = strstr(output.String(), "Default: deny (incoming)");
	if (incoming != NULL)
		state.inPolicy = FIREWALL_POLICY_DROP;
	else if (strstr(output.String(), "Default: allow (incoming)") != NULL)
		state.inPolicy = FIREWALL_POLICY_ACCEPT;

	const char* outgoing = strstr(output.String(), "Default: allow (outgoing)");
	if (outgoing != NULL)
		state.outPolicy = FIREWALL_POLICY_ACCEPT;
	else if (strstr(output.String(), "Default: deny (outgoing)") != NULL)
		state.outPolicy = FIREWALL_POLICY_DROP;

	if (strstr(output.String(), "Logging: on") != NULL)
		state.logging = true;

	// Rule lines look like: [ 1] 22/tcp ALLOW IN Anywhere
	// v6 duplicates of the same rule carry "(v6)" and are skipped.
	const char* line = output.String();
	while (*line != '\0') {
		const char* end = strchr(line, '\n');
		int32 length = end != NULL ? (int32)(end - line) : (int32)strlen(line);
		BString text(line, length);

		if (text.Length() > 0 && text[0] == '['
			&& text.FindFirst("(v6)") < 0
			&& (text.FindFirst(" ALLOW ") >= 0
				|| text.FindFirst(" DENY ") >= 0)) {
			FirewallRule rule;
			rule.allow = text.FindFirst(" ALLOW ") >= 0;
			rule.port = 0;

			int32 start = text.FindFirst(']');
			BString rest;
			if (start >= 0)
				rest.SetTo(text.String() + start + 1);

			int32 allowAt = rest.FindFirst(rule.allow ? " ALLOW " : " DENY ");
			BString match;
			if (allowAt >= 0)
				match.SetTo(rest.String(), allowAt);
			match.Trim();

			int32 slash = match.FindFirst('/');
			if (slash >= 0) {
				BString port(match.String(), slash);
				rule.port = (uint16)atoi(port.String());
				rule.protocol = match.String() + slash + 1;
			} else if (match == "Anywhere")
				rule.port = 0;
			else {
				char* endPtr = NULL;
				long value = strtol(match.String(), &endPtr, 10);
				if (endPtr != NULL && *endPtr == '\0')
					rule.port = (uint16)value;
			}

			// ufw's numbered listing has no application column; the
			// model label stays empty until a round trip through the
			// preflet fills it.
			state.rules.push_back(rule);
		}

		if (end == NULL)
			break;
		line = end + 1;
	}

	return B_OK;
}


static status_t
_UfwVerb(FirewallRunner* runner, const char* verb,
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
			error = "ufw verb failed";
		return B_ERROR;
	}
	return B_OK;
}


status_t
UfwBackend::ApplyState(const FirewallState& state, BString& error)
{
	// reset clears every rule (asks unless forced) and parks ufw
	// inactive; everything below rebuilds from the model.
	status_t status = _UfwVerb(fRunner, "ufw-reset",
		std::vector<BString>(), error);
	if (status != B_OK)
		return status;

	std::vector<BString> inDefault;
	inDefault.push_back(BString("in"));
	inDefault.push_back(BString(firewall_policy_name(state.inPolicy)));
	status = _UfwVerb(fRunner, "ufw-default", inDefault, error);
	if (status != B_OK)
		return status;

	std::vector<BString> outDefault;
	outDefault.push_back(BString("out"));
	outDefault.push_back(BString(firewall_policy_name(state.outPolicy)));
	status = _UfwVerb(fRunner, "ufw-default", outDefault, error);
	if (status != B_OK)
		return status;

	std::vector<BString> logging;
	logging.push_back(BString(state.logging ? "on" : "off"));
	status = _UfwVerb(fRunner, "ufw-logging", logging, error);
	if (status != B_OK)
		return status;

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

		status = _UfwVerb(fRunner, "ufw-add-rule", params, error);
		if (status != B_OK)
			return status;
	}

	return _UfwVerb(fRunner, state.enabled ? "ufw-enable" : "ufw-disable",
		std::vector<BString>(), error);
}
