/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "session_adapter.h"

#include "logging.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
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

		// WebKit 2.48 keeps cookies in memory unless a persistent
		// storage is set; local storage already persists under the
		// data directory on its own.
		WebKitCookieManager* cookies
			= webkit_network_session_get_cookie_manager(fNetworkSession);
		std::string cookieFile = fProfileDir + "/cookies/cookies.db";
		webkit_cookie_manager_set_persistent_storage(cookies,
			cookieFile.c_str(), WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

		// WebKit 2.48 has no public browsing-history API; the profile
		// keeps its own visit log.
		_LoadHistory();

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
SessionAdapter::RecordVisit(const std::string& url,
	const std::string& title)
{
	if (fEphemeral || url.empty())
		return;

	WebHistoryEntry entry;
	entry.timestamp = (int64)time(NULL);
	entry.url = url;
	// Tabs and newlines would break the one-line-per-visit format.
	entry.title = title;
	for (char& c : entry.title) {
		if (c == '\t' || c == '\n' || c == '\r')
			c = ' ';
	}

	{
		std::lock_guard<std::mutex> lock(fHistoryMutex);
		fHistory.push_back(entry);
		if (fHistory.size() > 1000)
			fHistory.erase(fHistory.begin(),
				fHistory.begin() + (fHistory.size() - 1000));
	}
	_SaveHistory();
}


void
SessionAdapter::ClearHistory()
{
	{
		std::lock_guard<std::mutex> lock(fHistoryMutex);
		fHistory.clear();
	}
	_SaveHistory();
	log_info("SessionAdapter: history cleared");
}


void
SessionAdapter::_LoadHistory()
{
	std::ifstream file(fProfileDir + "/history");
	if (!file.is_open())
		return;

	std::string line;
	while (std::getline(file, line)) {
		size_t firstTab = line.find('\t');
		size_t secondTab = firstTab != std::string::npos
			? line.find('\t', firstTab + 1) : std::string::npos;
		if (secondTab == std::string::npos)
			continue;
		WebHistoryEntry entry;
		entry.timestamp = (int64)atoll(line.c_str());
		entry.title = line.substr(firstTab + 1,
			secondTab - firstTab - 1);
		entry.url = line.substr(secondTab + 1);
		fHistory.push_back(entry);
	}
	if (fHistory.size() > 1000)
		fHistory.erase(fHistory.begin(),
			fHistory.begin() + (fHistory.size() - 1000));
}


void
SessionAdapter::_SaveHistory()
{
	std::string path = fProfileDir + "/history";
	std::ofstream file(path, std::ios::trunc);
	if (!file.is_open()) {
		log_error("SessionAdapter: cannot write %s", path.c_str());
		return;
	}

	std::lock_guard<std::mutex> lock(fHistoryMutex);
	for (const WebHistoryEntry& entry : fHistory)
		file << entry.timestamp << '\t' << entry.title << '\t'
			<< entry.url << '\n';
}
