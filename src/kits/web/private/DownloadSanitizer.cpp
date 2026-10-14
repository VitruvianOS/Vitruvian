/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "DownloadSanitizer.h"

#include <cstdlib>
#include <filesystem>


std::string
SanitizeDownloadFilename(const std::string& name)
{
	if (name.empty())
		return "download";
	if (name.find('/') != std::string::npos
		|| name.find('\\') != std::string::npos
		|| name.find("..") != std::string::npos
		|| name[0] == '.')
		return "download";

	std::string safe;
	safe.reserve(name.size());
	for (unsigned char c : name) {
		if (c < 0x20 || c == 0x7f)
			continue;
		safe += (char)c;
	}
	return safe.empty() ? "download" : safe;
}


std::string
UniqueDownloadDestination(const std::string& dir,
	const std::string& filename)
{
	std::string path = dir + "/" + filename;
	if (!std::filesystem::exists(path))
		return path;

	std::string base = filename, ext;
	auto dot = filename.rfind('.');
	if (dot != std::string::npos) {
		base = filename.substr(0, dot);
		ext  = filename.substr(dot);
	}
	for (int i = 1; i < 1000; i++) {
		std::string candidate =
			dir + "/" + base + " (" + std::to_string(i) + ")" + ext;
		if (!std::filesystem::exists(candidate))
			return candidate;
	}
	return path;
}


std::string
SafeDownloadDestination(const std::string& dir,
	const std::string& suggestedName)
{
	std::string filename = SanitizeDownloadFilename(
		suggestedName.empty() ? "download" : suggestedName);
	std::string dest = UniqueDownloadDestination(dir, filename);

	// Path traversal guard.
	auto canon = std::filesystem::weakly_canonical(dest);
	auto base  = std::filesystem::weakly_canonical(dir);
	auto rel   = canon.lexically_relative(base);
	if (rel.empty() || rel.native()[0] == '.')
		dest = dir + "/download";
	return dest;
}
