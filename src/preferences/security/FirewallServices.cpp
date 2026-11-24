/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "FirewallServices.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const FirewallService kServices[] = {
	{ "ssh", "Remote login", "tcp", { 22, 0 } },
	{ "cups", "Printer sharing", "tcp,udp", { 631, 0 } },
	{ "saned", "Scanner sharing", "tcp", { 6566, 0 } },
	{ "mdns", "Local network discovery", "udp", { 5353, 0 } },
	{ "web", "Web server", "tcp", { 80, 443 } },
};

static const int32 kServiceCount = sizeof(kServices) / sizeof(kServices[0]);


const FirewallService*
FirewallServiceAt(int32 index)
{
	if (index < 0 || index >= kServiceCount)
		return NULL;
	return &kServices[index];
}


const FirewallService*
FirewallServiceFor(const char* label)
{
	for (int32 i = 0; i < kServiceCount; i++) {
		if (strcmp(kServices[i].label, label) == 0)
			return &kServices[i];
	}
	return NULL;
}


static bool
_Listening(const char* file, bool tcp, uint16 port)
{
	FILE* f = fopen(file, "r");
	if (f == NULL)
		return false;

	bool found = false;
	char line[256];
	if (fgets(line, sizeof(line), f) == NULL) {
		fclose(f);
		return false;
	}
	while (!found && fgets(line, sizeof(line), f) != NULL) {
		unsigned local = 0;
		unsigned state = 0;
		char address[64];
		if (sscanf(line, " %*d: %63[0-9A-Fa-f]:%x %*[0-9A-Fa-f]:%*x %x",
				address, &local, &state) != 3) {
			continue;
		}
		// TCP must be in LISTEN (0A), UDP sockets are bound (07).
		if (local == port && state == (tcp ? 0x0A : 0x07))
			found = true;
	}
	fclose(f);
	return found;
}


static bool
_ListeningOn(uint16 port, bool tcp)
{
	if (tcp)
		return _Listening("/proc/net/tcp", true, port)
			|| _Listening("/proc/net/tcp6", true, port);
	return _Listening("/proc/net/udp", false, port)
		|| _Listening("/proc/net/udp6", false, port);
}


void
FirewallListeningRules(std::vector<FirewallRule>& rules)
{
	for (int32 i = 0; i < kServiceCount; i++) {
		const FirewallService& service = kServices[i];
		for (int32 p = 0; p < 2 && service.ports[p] != 0; p++) {
			bool wantTcp = strstr(service.protocols, "tcp") != NULL;
			bool wantUdp = strstr(service.protocols, "udp") != NULL;
			if (wantTcp && _ListeningOn(service.ports[p], true)) {
				FirewallRule rule;
				rule.allow = true;
				rule.protocol = "tcp";
				rule.port = service.ports[p];
				rule.label = service.label;
				rules.push_back(rule);
			}
			if (wantUdp && _ListeningOn(service.ports[p], false)) {
				FirewallRule rule;
				rule.allow = true;
				rule.protocol = "udp";
				rule.port = service.ports[p];
				rule.label = service.label;
				rules.push_back(rule);
			}
		}
	}
}
