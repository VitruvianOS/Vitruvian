/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "settings_parser.h"


std::map<std::string, std::string>
ParseSettings(std::istream& in)
{
	std::map<std::string, std::string> result;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		result[line.substr(0, eq)] = line.substr(eq + 1);
	}
	return result;
}


void
SerializeSettings(std::ostream& out,
	const std::map<std::string, std::string>& settings)
{
	for (const auto& kv : settings)
		out << kv.first << "=" << kv.second << "\n";
}
