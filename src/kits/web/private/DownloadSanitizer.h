/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef VOS_DOWNLOAD_SANITIZER_H
#define VOS_DOWNLOAD_SANITIZER_H


#include <string>


// Pure download-name helpers, free of WebKit types so they can be
// fuzzed on the host (src/tests/fuzz/web).
std::string SanitizeDownloadFilename(const std::string& name);
std::string UniqueDownloadDestination(const std::string& dir,
	const std::string& filename);
std::string SafeDownloadDestination(const std::string& dir,
	const std::string& suggestedName);


#endif // VOS_DOWNLOAD_SANITIZER_H
