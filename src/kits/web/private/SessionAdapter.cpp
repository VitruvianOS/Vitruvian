/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "session_adapter.h"

#include "logging.h"

#include <cstdlib>
#include <filesystem>
#include <wpe/webkit.h>


SessionAdapter::SessionAdapter(const std::string& profileDir, bool ephemeral)
	:
	fProfileDir(profileDir),
	fEphemeral(ephemeral),
	fNetworkSession(nullptr)
{
	if (fEphemeral) {
		fNetworkSession = webkit_network_session_new_ephemeral();
		log_info("SessionAdapter: ephemeral session");
	} else {
		if (fProfileDir.empty()) {
			const char* home = getenv("HOME");
			fProfileDir = home
				? std::string(home) + "/.local/share/netplus/default"
				: "/tmp/netplus-default";
		}

		_EnsureProfileDir();

		std::string cacheDir = fProfileDir + "/cache";
		fNetworkSession = webkit_network_session_new(
			fProfileDir.c_str(), cacheDir.c_str());

		log_info("SessionAdapter: persistent session at %s", fProfileDir.c_str());
	}
}


SessionAdapter::~SessionAdapter()
{
	if (fNetworkSession)
		g_object_unref(fNetworkSession);
}


void
SessionAdapter::_EnsureProfileDir()
{
	namespace fs = std::filesystem;
	try {
		fs::create_directories(fProfileDir + "/cookies");
		fs::create_directories(fProfileDir + "/cache");
		fs::create_directories(fProfileDir + "/localStorage");
		fs::create_directories(fProfileDir + "/widevine");
	} catch (const fs::filesystem_error& e) {
		log_error("SessionAdapter: failed to create profile dir: %s", e.what());
	}
}


void
SessionAdapter::ClearCookies()
{
	if (!fNetworkSession) return;

	// Clear via website data manager — clears all cookie data.
	WebKitWebsiteDataManager* dm =
		webkit_network_session_get_website_data_manager(fNetworkSession);
	webkit_website_data_manager_clear(dm,
		WEBKIT_WEBSITE_DATA_COOKIES, 0, nullptr, nullptr, nullptr);
	log_info("SessionAdapter: cookies cleared");
}


void
SessionAdapter::ClearCache()
{
	if (fEphemeral) {
		log_info("SessionAdapter::ClearCache: ephemeral, nothing to clear");
		return;
	}

	WebKitWebsiteDataManager* dm =
		webkit_network_session_get_website_data_manager(fNetworkSession);
	webkit_website_data_manager_clear(dm,
		(WebKitWebsiteDataTypes)(WEBKIT_WEBSITE_DATA_DISK_CACHE
			| WEBKIT_WEBSITE_DATA_MEMORY_CACHE),
		0, nullptr, nullptr, nullptr);
	log_info("SessionAdapter: cache cleared");
}


void
SessionAdapter::DeleteCookiesForDomain(const std::string& domain)
{
	if (!fNetworkSession) return;
	// Per-domain cookie deletion requires enumerating and deleting matching
	// cookies. For v1.0, clear all cookies as a fallback.
	(void)domain;
	ClearCookies();
	log_info("SessionAdapter: cookies cleared for domain %s (all cleared)",
		domain.c_str());
}
