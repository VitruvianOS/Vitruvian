/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "DownloadSanitizer.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>


std::string
SanitizeDownloadFilename(const std::string& name)
{
	// Strip control characters before validating: stripping after the
	// checks could re-introduce separators, e.g. "\x01/a" became "/a".
	std::string safe;
	safe.reserve(name.size());
	for (unsigned char c : name) {
		if (c < 0x20 || c == 0x7f)
			continue;
		safe += (char)c;
	}
	if (safe.empty() || safe.find('/') != std::string::npos
		|| safe.find('\\') != std::string::npos
		|| safe.find("..") != std::string::npos
		|| safe[0] == '.')
		return "download";

	return safe;
}


std::string
UniqueDownloadDestination(const std::string& dir,
	const std::string& filename)
{
	// The non-throwing overloads matter: an over-long untrusted name
	// raises ENAMETOOLONG, and the throwing form aborts the process.
	std::error_code error;
	std::string path = dir + "/" + filename;
	if (!std::filesystem::exists(path, error))
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
		if (!std::filesystem::exists(candidate, error))
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
	std::error_code error;
	auto canon = std::filesystem::weakly_canonical(dest, error);
	if (error)
		canon = std::filesystem::path(dest);
	auto base = std::filesystem::weakly_canonical(dir, error);
	if (error)
		base = std::filesystem::path(dir);
	auto rel = canon.lexically_relative(base);
	if (rel.empty() || rel.native()[0] == '.')
		dest = dir + "/download";
	return dest;
}
