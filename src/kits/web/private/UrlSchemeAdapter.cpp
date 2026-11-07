/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "url_scheme_adapter.h"

#include "logging.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <wpe/webkit.h>


namespace {


std::map<std::string, std::string>&
schemeDirectories()
{
	static std::map<std::string, std::string> sDirectories;
	return sDirectories;
}


std::string
resolvePath(const std::string& base, const std::string& uriPath)
{
	// Strip the scheme and any slashes: "vos-mail:/ui/index.html"
	// maps to "<base>/ui/index.html".
	std::string relative = uriPath;
	size_t schemeEnd = relative.find(':');
	if (schemeEnd != std::string::npos)
		relative = relative.substr(schemeEnd + 1);
	while (!relative.empty() && relative[0] == '/')
		relative.erase(0, 1);

	// Reject traversal and empty paths.
	if (relative.empty())
		return std::string();
	std::stringstream stream(relative);
	std::string component;
	while (std::getline(stream, component, '/')) {
		if (component == ".." || component == ".")
			return std::string();
	}

	std::error_code ec;
	std::filesystem::path baseDir(base);
	std::filesystem::path full = baseDir / relative;
	std::filesystem::path canonical
		= std::filesystem::weakly_canonical(full, ec);
	if (ec)
		return std::string();
	std::filesystem::path canonicalBase
		= std::filesystem::weakly_canonical(baseDir, ec);
	if (ec)
		return std::string();
	std::string canonicalStr = canonical.string();
	std::string baseStr = canonicalBase.string();
	if (canonicalStr.size() <= baseStr.size()
		|| canonicalStr.compare(0, baseStr.size(), baseStr) != 0
		|| canonicalStr[baseStr.size()] != '/') {
		return std::string();
	}
	return canonicalStr;
}


} // namespace


void
SetUriSchemeDirectory(const char* scheme, const char* path)
{
	if (scheme == NULL)
		return;
	schemeDirectories()[scheme] = path != NULL ? path : "";
}


UrlSchemeAdapter::UrlSchemeAdapter()
{
}


UrlSchemeAdapter::~UrlSchemeAdapter()
{
}


void
UrlSchemeAdapter::RegisterSchemes(WebKitWebContext* context)
{
	webkit_web_context_register_uri_scheme(context, "vos-error",
		_HandleVosError, this, nullptr);
	webkit_web_context_register_uri_scheme(context, "vos-about",
		_HandleVosAbout, this, nullptr);

	// Mark custom schemes as secure so mixed-content rules don't block them.
	WebKitSecurityManager* sm =
		webkit_web_context_get_security_manager(context);
	webkit_security_manager_register_uri_scheme_as_secure(sm, "vos-error");
	webkit_security_manager_register_uri_scheme_as_secure(sm, "vos-about");
	webkit_security_manager_register_uri_scheme_as_local(sm, "vos-about");

	// Directory-backed schemes (vos-mail for WebMail's bundled UI).
	for (const auto& entry : schemeDirectories()) {
		if (entry.second.empty())
			continue;
		webkit_web_context_register_uri_scheme(context,
			entry.first.c_str(), _HandleDirectoryScheme, this, nullptr);
		webkit_security_manager_register_uri_scheme_as_secure(
			sm, entry.first.c_str());
		webkit_security_manager_register_uri_scheme_as_local(
			sm, entry.first.c_str());
		webkit_security_manager_register_uri_scheme_as_cors_enabled(
			sm, entry.first.c_str());
	}

	log_info("UrlSchemeAdapter: registered vos-error, vos-about");
}


void
UrlSchemeAdapter::_FinishWithHTML(WebKitURISchemeRequest* request,
	const char* html)
{
	gsize len = (gsize)strlen(html);
	GInputStream* stream = g_memory_input_stream_new_from_data(
		g_memdup2(html, len), len, g_free);
	webkit_uri_scheme_request_finish(request, stream, (gint64)len, "text/html");
	g_object_unref(stream);
}


void
UrlSchemeAdapter::_HandleVosError(WebKitURISchemeRequest* request, gpointer)
{
	_FinishWithHTML(request,
		"<!DOCTYPE html><html><head><title>Error</title></head>"
		"<body style='font-family:sans-serif;text-align:center;padding:2em'>"
		"<h1 style='color:#c00'>Page Load Error</h1>"
		"<p>The page could not be loaded.</p></body></html>");
}


void
UrlSchemeAdapter::_HandleVosAbout(WebKitURISchemeRequest* request, gpointer)
{
	// The search box reads NETPLUS_SEARCH_TEMPLATE, which NetPlus
	// injects as a user script so the page follows the engine from
	// the settings.
	_FinishWithHTML(request,
		"<!DOCTYPE html><html><head><meta charset='utf-8'><title>About NetPlus</title>"
		"<meta name='viewport' content='width=device-width'>"
		"</head>"
		"<body style='font-family:sans-serif;text-align:center;"
		"padding:3em'>"
		"<h1>NetPlus Browser</h1>"
		"<p>Version 0.5 \xc2\xa9 2026 V\\OS</p>"
		"<form style='margin:2em auto;max-width:28em' "
		"onsubmit=\"event.preventDefault();"
		"var t = window.NETPLUS_SEARCH_TEMPLATE"
		" || 'https://duckduckgo.com/?q=%s';"
		"var q = encodeURIComponent(this.q.value);"
		"if (!q) return;"
		"location.href = t.indexOf('%s') >= 0"
		" ? t.replace('%s', q) : t + q;\">"
		"<input name='q' placeholder='Search the web' "
		"style='width:70%;padding:.4em'>"
		"<button style='padding:.4em .8em'>Search</button>"
		"</form>"
		"</body></html>");
}


const char*
UrlSchemeAdapter::_MimeTypeForPath(const char* path)
{
	const char* extension = strrchr(path, '.');
	if (extension == NULL)
		return "application/octet-stream";
	if (strcmp(extension, ".html") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(extension, ".htm") == 0)
		return "text/html; charset=utf-8";
	if (strcmp(extension, ".js") == 0)
		return "text/javascript; charset=utf-8";
	if (strcmp(extension, ".css") == 0)
		return "text/css; charset=utf-8";
	if (strcmp(extension, ".json") == 0)
		return "application/json; charset=utf-8";
	if (strcmp(extension, ".svg") == 0)
		return "image/svg+xml";
	if (strcmp(extension, ".png") == 0)
		return "image/png";
	if (strcmp(extension, ".jpg") == 0)
		return "image/jpeg";
	if (strcmp(extension, ".jpeg") == 0)
		return "image/jpeg";
	if (strcmp(extension, ".ico") == 0)
		return "image/vnd.microsoft.icon";
	if (strcmp(extension, ".txt") == 0)
		return "text/plain; charset=utf-8";
	if (strcmp(extension, ".woff2") == 0)
		return "font/woff2";
	return "application/octet-stream";
}


void
UrlSchemeAdapter::_FinishWithError(WebKitURISchemeRequest* request)
{
	GError* error = g_error_new(G_FILE_ERROR, G_FILE_ERROR_NOENT,
		"Resource not found");
	webkit_uri_scheme_request_finish_error(request, error);
	g_error_free(error);
}


void
UrlSchemeAdapter::_HandleDirectoryScheme(WebKitURISchemeRequest* request,
	gpointer)
{
	const char* uri = webkit_uri_scheme_request_get_uri(request);
	if (uri == NULL) {
		_FinishWithError(request);
		return;
	}

	// Find the directory registered for this request's scheme.
	std::string uriStr(uri);
	size_t schemeEnd = uriStr.find(':');
	std::string scheme
		= schemeEnd != std::string::npos ? uriStr.substr(0, schemeEnd) : "";
	auto it = schemeDirectories().find(scheme);
	if (it == schemeDirectories().end() || it->second.empty()) {
		_FinishWithError(request);
		return;
	}

	std::string path = resolvePath(it->second, uriStr);
	if (path.empty()) {
		_FinishWithError(request);
		return;
	}

	std::ifstream file(path.c_str(), std::ios::binary);
	if (!file.is_open()) {
		_FinishWithError(request);
		return;
	}
	std::ostringstream buffer;
	buffer << file.rdbuf();
	std::string data = buffer.str();

	gsize length = (gsize)data.size();
	GInputStream* stream = g_memory_input_stream_new_from_data(
		g_memdup2(data.data(), length), length, g_free);
	webkit_uri_scheme_request_finish(request, stream, (gint64)length,
		_MimeTypeForPath(path.c_str()));
	g_object_unref(stream);
}
