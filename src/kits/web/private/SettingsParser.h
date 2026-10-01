/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#pragma once

#include <istream>
#include <map>
#include <ostream>
#include <string>


// Pure key=value parse/serialize — no BeAPI dependency.
std::map<std::string, std::string> ParseSettings(std::istream& in);
void SerializeSettings(std::ostream& out,
	const std::map<std::string, std::string>& settings);
