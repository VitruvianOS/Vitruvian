/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <web/WebSettings.h>
#include <web/UrlNormalize.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "settings_parser.h"


static const char* kKeyHomepage			= "homepage";
static const char* kKeySearchEngine		= "search_engine";
static const char* kKeySearchTemplate	= "search_url_template";
static const char* kKeyDownloadDir		= "download_dir";
static const char* kKeyAskDownload		= "ask_download_location";
static const char* kKeyJavaScript		= "javascript_enabled";
static const char* kKeyImages			= "images_enabled";
static const char* kKeyUserAgentSuffix	= "user_agent_suffix";
static const char* kKeyBlockedDomains	= "blocked_domains";
static const char* kKeyPrivateBrowsing	= "private_browsing";
static const char* kKeyZoomDefault		= "zoom_default";
static const char* kKeyRestoreSession	= "restore_session";


BWebSettings::BWebSettings()
{
}


BWebSettings::~BWebSettings()
{
}


status_t
BWebSettings::Load(const char* path)
{
	fPath = path != NULL ? path : "";

	std::ifstream in(fPath.c_str());
	if (!in.good()) {
		// Missing file means defaults; it is created on first save.
		fValues.clear();
		return B_ENTRY_NOT_FOUND;
	}

	fValues = ParseSettings(in);
	return B_OK;
}


status_t
BWebSettings::Save() const
{
	if (fPath.empty())
		return B_BAD_VALUE;

	std::ofstream out(fPath.c_str(), std::ios::trunc);
	if (!out.good())
		return B_ERROR;

	SerializeSettings(out, fValues);
	return B_OK;
}


std::string
BWebSettings::_Get(const char* key) const
{
	auto it = fValues.find(key);
	return it != fValues.end() ? it->second : std::string();
}


void
BWebSettings::_Set(const char* key, const std::string& value)
{
	fValues[key] = value;
}


bool
BWebSettings::_ToBool(const std::string& value, bool fallback)
{
	if (value.empty())
		return fallback;
	return value == "1" || value == "true" || value == "yes"
		|| value == "on";
}


const char*
BWebSettings::_GetString(const char* key, const char* fallback) const
{
	auto it = fValues.find(key);
	if (it == fValues.end() || it->second.empty())
		return fallback;
	return it->second.c_str();
}


const char*
BWebSettings::Homepage() const
{
	return _GetString(kKeyHomepage, "vos-about:");
}


void
BWebSettings::SetHomepage(const char* value)
{
	_Set(kKeyHomepage, value != NULL ? value : "");
}


const char*
BWebSettings::SearchEngine() const
{
	return _GetString(kKeySearchEngine, "DuckDuckGo");
}


void
BWebSettings::SetSearchEngine(const char* value)
{
	_Set(kKeySearchEngine, value != NULL ? value : "");
}


const char*
BWebSettings::SearchURLTemplate() const
{
	return _GetString(kKeySearchTemplate, kWebSearchDuckDuckGo);
}


void
BWebSettings::SetSearchURLTemplate(const char* value)
{
	_Set(kKeySearchTemplate, value != NULL ? value : "");
}


const char*
BWebSettings::DownloadDir() const
{
	return _GetString(kKeyDownloadDir, NULL);
}


void
BWebSettings::SetDownloadDir(const char* value)
{
	_Set(kKeyDownloadDir, value != NULL ? value : "");
}


bool
BWebSettings::AskDownloadLocation() const
{
	return _ToBool(_Get(kKeyAskDownload), false);
}


void
BWebSettings::SetAskDownloadLocation(bool ask)
{
	_Set(kKeyAskDownload, ask ? "true" : "false");
}


bool
BWebSettings::JavaScriptEnabled() const
{
	return _ToBool(_Get(kKeyJavaScript), true);
}


void
BWebSettings::SetJavaScriptEnabled(bool enabled)
{
	_Set(kKeyJavaScript, enabled ? "true" : "false");
}


bool
BWebSettings::ImagesEnabled() const
{
	return _ToBool(_Get(kKeyImages), true);
}


void
BWebSettings::SetImagesEnabled(bool enabled)
{
	_Set(kKeyImages, enabled ? "true" : "false");
}


const char*
BWebSettings::UserAgentSuffix() const
{
	return _GetString(kKeyUserAgentSuffix, NULL);
}


void
BWebSettings::SetUserAgentSuffix(const char* value)
{
	_Set(kKeyUserAgentSuffix, value != NULL ? value : "");
}


const char*
BWebSettings::BlockedDomains() const
{
	return _GetString(kKeyBlockedDomains, NULL);
}


void
BWebSettings::SetBlockedDomains(const char* value)
{
	_Set(kKeyBlockedDomains, value != NULL ? value : "");
}


bool
BWebSettings::PrivateBrowsing() const
{
	return _ToBool(_Get(kKeyPrivateBrowsing), false);
}


void
BWebSettings::SetPrivateBrowsing(bool priv)
{
	_Set(kKeyPrivateBrowsing, priv ? "true" : "false");
}


double
BWebSettings::ZoomDefault() const
{
	std::string value = _Get(kKeyZoomDefault);
	if (value.empty())
		return 1.0;
	return strtod(value.c_str(), NULL);
}


void
BWebSettings::SetZoomDefault(double zoom)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%.2f", zoom);
	_Set(kKeyZoomDefault, buffer);
}


bool
BWebSettings::RestoreSession() const
{
	return _ToBool(_Get(kKeyRestoreSession), false);
}


void
BWebSettings::SetRestoreSession(bool restore)
{
	_Set(kKeyRestoreSession, restore ? "true" : "false");
}


void
BWebSettings::SetValue(const char* key, const char* value)
{
	_Set(key, value != NULL ? value : "");
}
