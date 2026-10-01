/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "domain_filter.h"


bool
IsBlockedDomain(const std::string& host,
	const std::vector<std::string>& blocklist)
{
	if (host.empty())
		return false;

	for (const auto& entry : blocklist) {
		if (entry.empty())
			continue;
		if (host == entry)
			return true;
		if (host.size() > entry.size() + 1) {
			size_t pos = host.size() - entry.size() - 1;
			if (host[pos] == '.'
				&& host.compare(pos + 1, entry.size(), entry) == 0)
				return true;
		}
	}
	return false;
}
