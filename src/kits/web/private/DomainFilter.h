/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#pragma once

#include <string>
#include <vector>


// Returns true if `host` matches any entry in `blocklist`.
// An entry "example.com" blocks "example.com" and "sub.example.com"
// but not "notexample.com" or "example.com.evil.com".
bool IsBlockedDomain(const std::string& host,
	const std::vector<std::string>& blocklist);
