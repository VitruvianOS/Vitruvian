/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <web/WebExtensions.h>
#include <web/WebDefs.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>


namespace fs = std::filesystem;


//	#pragma mark - Minimal JSON reader


// Enough for extension manifests: objects of string values, string
// arrays and booleans. Full JSON stays in the app's manifest parser.
namespace {


struct JsonReader {
	const char*		at;
	const char*		end;

	void			_SkipSpace()
	{
		while (at < end && (*at == ' ' || *at == '\t'
			|| *at == '\n' || *at == '\r'))
			at++;
	}

	bool			_Match(char c)
	{
		_SkipSpace();
		if (at < end && *at == c) {
			at++;
			return true;
		}
		return false;
	}

	bool			_ReadString(std::string* into)
	{
		_SkipSpace();
		if (at >= end || *at != '"')
			return false;
		at++;
		into->clear();
		while (at < end && *at != '"') {
			if (*at == '\\' && at + 1 < end) {
				at++;
				switch (*at) {
					case 'n': *into += '\n'; break;
					case 't': *into += '\t'; break;
					case 'r': *into += '\r'; break;
					case 'b': *into += '\b'; break;
					case 'f': *into += '\f'; break;
					case 'u':
						// BMP escapes only; manifests stay ASCII.
						if (at + 4 < end) {
							char buffer[5] = {0};
							memcpy(buffer, at + 1, 4);
							unsigned code = 0;
							if (sscanf(buffer, "%x", &code) == 1
								&& code < 0x80)
								*into += (char)code;
						}
						at += 4;
						break;
					default: *into += *at; break;
				}
				at++;
				continue;
			}
			*into += *at++;
		}
		if (at >= end)
			return false;
		at++;
		return true;
	}

	// Finds "key" at the current object level and reads the value.
	bool			GetString(const char* key, std::string* into) const
	{
		JsonReader copy = *this;
		copy._SkipSpace();
		if (copy.at >= copy.end || *copy.at != '{')
			return false;
		copy.at++;
		while (true) {
			std::string name;
			copy._SkipSpace();
			if (copy.at < copy.end && *copy.at == '}')
				return false;
			if (!copy._ReadString(&name))
				return false;
			if (!copy._Match(':'))
				return false;
			if (name == key)
				return copy._ReadString(into);
			if (!_SkipValue(&copy))
				return false;
			copy._Match(',');
		}
	}

	bool			GetStringArray(const char* key,
						std::vector<std::string>* into) const
	{
		JsonReader copy = *this;
		copy._SkipSpace();
		if (copy.at >= copy.end || *copy.at != '{')
			return false;
		copy.at++;
		while (true) {
			std::string name;
			copy._SkipSpace();
			if (copy.at < copy.end && *copy.at == '}')
				return false;
			if (!copy._ReadString(&name))
				return false;
			if (!copy._Match(':'))
				return false;
			if (name == key) {
				into->clear();
				if (!copy._Match('['))
					return false;
				copy._SkipSpace();
				if (copy._Match(']'))
					return true;
				while (true) {
					std::string value;
					if (!copy._ReadString(&value))
						return false;
					into->push_back(value);
					copy._SkipSpace();
					if (copy._Match(','))
						continue;
					return copy._Match(']');
				}
			}
			if (!_SkipValue(&copy))
				return false;
			copy._Match(',');
		}
	}

	bool			GetBool(const char* key, bool* into) const
	{
		JsonReader copy = *this;
		copy._SkipSpace();
		if (copy.at >= copy.end || *copy.at != '{')
			return false;
		copy.at++;
		while (true) {
			std::string name;
			copy._SkipSpace();
			if (copy.at < copy.end && *copy.at == '}')
				return false;
			if (!copy._ReadString(&name))
				return false;
			if (!copy._Match(':'))
				return false;
			if (name == key) {
				copy._SkipSpace();
				if (copy.end - copy.at >= 4
					&& strncmp(copy.at, "true", 4) == 0) {
					*into = true;
					return true;
				}
				if (copy.end - copy.at >= 5
					&& strncmp(copy.at, "false", 5) == 0) {
					*into = false;
					return true;
				}
				return false;
			}
			if (!_SkipValue(&copy))
				return false;
			copy._Match(',');
		}
	}

	static bool		_SkipValue(JsonReader* reader)
	{
		reader->_SkipSpace();
		if (reader->at >= reader->end)
			return false;
		char c = *reader->at;
		if (c == '"') {
			std::string scratch;
			return reader->_ReadString(&scratch);
		}
		if (c == '{' || c == '[') {
			char close = c == '{' ? '}' : ']';
			int depth = 0;
			while (reader->at < reader->end) {
				if (*reader->at == '"') {
					std::string scratch;
					if (!reader->_ReadString(&scratch))
						return false;
					continue;
				}
				if (*reader->at == c)
					depth++;
				else if (*reader->at == close) {
					depth--;
					if (depth == 0) {
						reader->at++;
						return true;
					}
				}
				reader->at++;
			}
			return false;
		}
		// number, bool, null
		while (reader->at < reader->end && *reader->at != ','
			&& *reader->at != '}' && *reader->at != ']'
			&& *reader->at != ' ' && *reader->at != '\n'
			&& *reader->at != '\r' && *reader->at != '\t')
			reader->at++;
		return true;
	}
};


} // anonymous namespace


//	#pragma mark - Match patterns


static bool
_HostMatches(const std::string& patternHost, const std::string& host)
{
	if (patternHost == "*")
		return !host.empty();
	if (patternHost.rfind("*.", 0) == 0) {
		std::string suffix = patternHost.substr(2);
		if (host == suffix)
			return true;
		if (host.size() > suffix.size() + 1) {
			size_t pos = host.size() - suffix.size() - 1;
			if (host[pos] == '.'
				&& host.compare(pos + 1, suffix.size(), suffix) == 0)
				return true;
		}
		return false;
	}
	return host == patternHost;
}


static bool
_PathMatches(const std::string& pattern, const std::string& path)
{
	// Glob with '*'; anchored at both ends.
	size_t pi = 0;
	size_t si = 0;
	size_t star = std::string::npos;
	size_t starSi = 0;
	while (si < path.size()) {
		if (pi < pattern.size() && pattern[pi] == path[si]) {
			pi++;
			si++;
			continue;
		}
		if (pi < pattern.size() && pattern[pi] == '*') {
			star = pi++;
			starSi = si;
			continue;
		}
		if (star != std::string::npos) {
			pi = star + 1;
			si = ++starSi;
			continue;
		}
		return false;
	}
	while (pi < pattern.size() && pattern[pi] == '*')
		pi++;
	return pi == pattern.size();
}


static std::string
_StripPort(const std::string& authority)
{
	size_t colon = authority.rfind(':');
	if (colon == std::string::npos)
		return authority;
	size_t bracket = authority.find(']');
	if (bracket != std::string::npos && bracket < colon)
		return authority.substr(0, colon + 1);
	if (bracket == std::string::npos)
		return authority.substr(0, colon);
	return authority;
}


bool
WebExtensionMatchPattern(const char* pattern, const char* url)
{
	if (pattern == NULL || url == NULL)
		return false;
	std::string p(pattern);
	std::string u(url);
	if (p == "<all_urls>")
		return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0
			|| u.rfind("file://", 0) == 0;

	// scheme://host[:port]/path[*query][#frag]
	size_t schemeEnd = u.find("://");
	if (schemeEnd == std::string::npos)
		return false;
	std::string scheme = u.substr(0, schemeEnd);
	size_t authStart = schemeEnd + 3;
	size_t pathStart = u.find('/', authStart);
	std::string authority = pathStart == std::string::npos
		? u.substr(authStart) : u.substr(authStart, pathStart - authStart);
	std::string path = pathStart == std::string::npos
		? "/" : u.substr(pathStart);
	size_t cut = path.find_first_of("?#");
	if (cut != std::string::npos)
		path = path.substr(0, cut);

	size_t at = authority.rfind('@');
	if (at != std::string::npos)
		authority = authority.substr(at + 1);
	std::string host = _StripPort(authority);

	size_t pSchemeEnd = p.find("://");
	if (pSchemeEnd == std::string::npos)
		return false;
	std::string pScheme = p.substr(0, pSchemeEnd);
	if (pScheme != "*" && pScheme != scheme)
		return false;
	size_t pAuthStart = pSchemeEnd + 3;
	size_t pPathStart = p.find('/', pAuthStart);
	std::string pAuthority = pPathStart == std::string::npos
		? p.substr(pAuthStart) : p.substr(pAuthStart, pPathStart - pAuthStart);
	std::string pPath = pPathStart == std::string::npos
		? "/" : p.substr(pPathStart);
	return _HostMatches(pAuthority, host)
		&& _PathMatches(pPath, path);
}


//	#pragma mark - Extension record


struct WebExtensionRecord {
	WebExtensionInfo			info;
	std::vector<std::string>	match;
	std::string					rulesPath;
	// Script and style bodies are small; load them at scan time.
	std::string					source;
	bool						atDocumentStart = true;
	bool						usesGM = false;
};


static std::string
_ReadFile(const fs::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return std::string();
	std::ostringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}


// Userscript metadata header: // ==UserScript== ... // ==End==.
static void
_ParseUserScriptHeader(const std::string& source,
	WebExtensionRecord* extension)
{
	std::istringstream lines(source);
	std::string line;
	bool inHeader = false;
	while (std::getline(lines, line)) {
		if (!inHeader) {
			if (line.find("// ==UserScript==") == 0)
				inHeader = true;
			continue;
		}
		if (line.find("// ==End==") == 0)
			break;
		if (line.find("// @") != 0)
			continue;
		std::string rest = line.substr(3);
		size_t space = rest.find_first_of(" \t");
		if (space == std::string::npos)
			continue;
		std::string key = rest.substr(0, space);
		std::string value = rest.substr(space + 1);
		while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
			value.erase(0, 1);
		if (key == "match")
			extension->match.push_back(value);
		else if (key == "run-at")
			extension->atDocumentStart
				= value != "document-end" && value != "document-idle";
		else if (key == "name" && extension->info.name.empty())
			extension->info.name = value;
		else if (key == "version" && extension->info.version.empty())
			extension->info.version = value;
		else if (key == "licence" || key == "license") {
			if (extension->info.licence.empty())
				extension->info.licence = value;
		} else if (key == "description"
			&& extension->info.description.empty())
			extension->info.description = value;
		else if (key == "grant") {
			if (value != "none")
				extension->usesGM = true;
		}
	}
}


//	#pragma mark - BWebExtensions


BWebExtensions::BWebExtensions()
{
}


BWebExtensions::~BWebExtensions()
{
}


status_t
BWebExtensions::Load(const char* packagedRoot, const char* userRoot,
	const char* statePath)
{
	std::lock_guard<std::mutex> lock(fMutex);
	fExtensions.clear();
	fDisabledSites.clear();
	fPackagedRoot = packagedRoot != NULL ? packagedRoot : "";
	fUserRoot = userRoot != NULL ? userRoot : "";
	fStatePath = statePath != NULL ? statePath : "";

	if (!fPackagedRoot.empty())
		_ScanRoot(fPackagedRoot, false);
	if (!fUserRoot.empty())
		_ScanRoot(fUserRoot, true);
	_LoadState();

	// Enabled defaults: extensions start on unless the manifest opts
	// out; persisted state overrides both.
	for (auto& extension : fExtensions) {
		if (fEnabled.find(extension.info.id) == fEnabled.end())
			fEnabled[extension.info.id] = extension.info.enabled;
		extension.info.enabled = fEnabled[extension.info.id];
	}
	return B_OK;
}


void
BWebExtensions::Reload()
{
	std::string packaged;
	std::string user;
	std::string state;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		packaged = fPackagedRoot;
		user = fUserRoot;
		state = fStatePath;
	}
	Load(packaged.c_str(), user.c_str(), state.c_str());
}


void
BWebExtensions::_ScanRoot(const std::string& root, bool userOwned)
{
	std::error_code ec;
	fs::directory_iterator it(fs::path(root), ec);
	if (ec)
		return;
	for (const auto& entry : it) {
		if (!entry.is_directory(ec))
			continue;
		fs::path manifest = entry.path() / "manifest.json";
		if (!fs::exists(manifest, ec))
			continue;

		Extension extension;
		// The directory name is the identity: state, install and
		// remove all key on it, a manifest id cannot redirect them.
		extension.info.id = entry.path().filename().string();
		extension.info.path = entry.path().string();
		extension.info.userOwned = userOwned;
		// Default until the manifest or state says otherwise.
		extension.info.enabled = true;

		std::string json = _ReadFile(manifest);
		JsonReader reader = {json.data(), json.data() + json.size()};
		std::string value;
		if (reader.GetString("name", &value))
			extension.info.name = value;
		if (reader.GetString("version", &value))
			extension.info.version = value;
		if (reader.GetString("kind", &value))
			extension.info.kind = value;
		if (reader.GetString("licence", &value))
			extension.info.licence = value;
		if (reader.GetString("description", &value))
			extension.info.description = value;
		reader.GetStringArray("match", &extension.match);
		if (!reader.GetBool("default-enabled", &extension.info.enabled))
			extension.info.enabled = true;
		if (extension.info.name.empty())
			extension.info.name = extension.info.id;

		if (extension.info.kind == "content-blocker") {
			std::string rules = "rules.json";
			reader.GetString("rules", &rules);
			fs::path rulesPath = entry.path() / rules;
			if (!fs::exists(rulesPath, ec))
				continue;
			extension.rulesPath = rulesPath.string();
		} else if (extension.info.kind == "userscript") {
			std::string script = "script.user.js";
			reader.GetString("script", &script);
			extension.source = _ReadFile(entry.path() / script);
			if (extension.source.empty())
				continue;
			// The header fills gaps the manifest left open.
			_ParseUserScriptHeader(extension.source, &extension);
			// A userscript with no match applies wherever it runs.
			if (extension.match.empty())
				extension.match.push_back("<all_urls>");
		} else if (extension.info.kind == "user-style") {
			std::string style = "style.css";
			reader.GetString("style", &style);
			extension.source = _ReadFile(entry.path() / style);
			if (extension.source.empty())
				continue;
			if (extension.match.empty())
				extension.match.push_back("<all_urls>");
		} else
			continue;

		fExtensions.push_back(extension);
	}
}


static std::string
_EscapeStateValue(const std::string& in)
{
	std::string out;
	for (char c : in) {
		if (c == '\n')
			out += "\\n";
		else if (c == '\\')
			out += "\\\\";
		else
			out += c;
	}
	return out;
}


static std::string
_UnescapeStateValue(const std::string& in)
{
	std::string out;
	for (size_t i = 0; i < in.size(); i++) {
		if (in[i] == '\\' && i + 1 < in.size()) {
			i++;
			out += in[i] == 'n' ? '\n' : in[i];
			continue;
		}
		out += in[i];
	}
	return out;
}


void
BWebExtensions::_LoadState()
{
	if (fStatePath.empty())
		return;
	std::ifstream file(fStatePath);
	if (!file)
		return;
	std::string line;
	while (std::getline(file, line)) {
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = line.substr(0, eq);
		std::string value = line.substr(eq + 1);
		while (!key.empty() && key[key.size() - 1] == ' ')
			key.erase(key.size() - 1);
		while (!value.empty() && value[0] == ' ')
			value.erase(0, 1);
		if (key.rfind("enabled.", 0) == 0)
			fEnabled[key.substr(8)] = value == "1" || value == "true";
		else if (key.rfind("disabled_site.", 0) == 0) {
			std::string rest = key.substr(14);
			size_t dot = rest.find('.');
			if (dot != std::string::npos)
				fDisabledSites[rest.substr(0, dot)].push_back(
					rest.substr(dot + 1));
		}
	}
}


void
BWebExtensions::_SaveState() const
{
	if (fStatePath.empty())
		return;
	fs::path path(fStatePath);
	std::error_code ec;
	if (path.has_parent_path())
		fs::create_directories(path.parent_path(), ec);
	std::ofstream file(path, std::ios::trunc);
	if (!file)
		return;
	for (const auto& entry : fEnabled)
		file << "enabled." << entry.first << " = "
			<< (entry.second ? "1" : "0") << "\n";
	for (const auto& entry : fDisabledSites) {
		for (const auto& host : entry.second)
			file << "disabled_site." << entry.first << "." << host
				<< " = 1\n";
	}
}


void
BWebExtensions::_Notify()
{
	if (fTarget.IsValid())
		fTarget.SendMessage(B_WEB_EXTENSIONS_CHANGED);
}


std::vector<WebExtensionInfo>
BWebExtensions::List() const
{
	std::lock_guard<std::mutex> lock(fMutex);
	std::vector<WebExtensionInfo> list;
	list.reserve(fExtensions.size());
	for (const auto& extension : fExtensions)
		list.push_back(extension.info);
	return list;
}


bool
BWebExtensions::Get(const char* id, WebExtensionInfo* into) const
{
	if (id == NULL || into == NULL)
		return false;
	std::lock_guard<std::mutex> lock(fMutex);
	for (const auto& extension : fExtensions) {
		if (extension.info.id == id) {
			*into = extension.info;
			return true;
		}
	}
	return false;
}


status_t
BWebExtensions::SetEnabled(const char* id, bool enabled)
{
	if (id == NULL)
		return B_BAD_VALUE;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		bool found = false;
		for (auto& extension : fExtensions) {
			if (extension.info.id == id) {
				extension.info.enabled = enabled;
				found = true;
				break;
			}
		}
		if (!found)
			return B_ENTRY_NOT_FOUND;
		fEnabled[id] = enabled;
		_SaveState();
	}
	_Notify();
	return B_OK;
}


bool
BWebExtensions::IsEnabled(const char* id) const
{
	if (id == NULL)
		return false;
	std::lock_guard<std::mutex> lock(fMutex);
	auto it = fEnabled.find(id);
	if (it != fEnabled.end())
		return it->second;
	for (const auto& extension : fExtensions) {
		if (extension.info.id == id)
			return extension.info.enabled;
	}
	return false;
}


status_t
BWebExtensions::SetDisabledOnSite(const char* id, const char* host,
	bool disabled)
{
	if (id == NULL || host == NULL || host[0] == '\0')
		return B_BAD_VALUE;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		auto& sites = fDisabledSites[id];
		auto it = std::find(sites.begin(), sites.end(), std::string(host));
		if (disabled) {
			if (it == sites.end())
				sites.push_back(host);
		} else if (it != sites.end())
			sites.erase(it);
		_SaveState();
	}
	_Notify();
	return B_OK;
}


bool
BWebExtensions::IsDisabledOnSite(const char* id, const char* host) const
{
	if (id == NULL || host == NULL)
		return false;
	std::lock_guard<std::mutex> lock(fMutex);
	auto it = fDisabledSites.find(id);
	if (it == fDisabledSites.end())
		return false;
	return std::find(it->second.begin(), it->second.end(),
		std::string(host)) != it->second.end();
}


std::vector<std::string>
BWebExtensions::SitesDisabledOn(const char* id) const
{
	std::vector<std::string> result;
	if (id == NULL)
		return result;
	std::lock_guard<std::mutex> lock(fMutex);
	auto it = fDisabledSites.find(id);
	if (it != fDisabledSites.end())
		result = it->second;
	return result;
}


static std::string
_HostOf(const std::string& url)
{
	size_t schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos)
		return std::string();
	size_t authStart = schemeEnd + 3;
	size_t pathStart = url.find('/', authStart);
	std::string authority = pathStart == std::string::npos
		? url.substr(authStart) : url.substr(authStart, pathStart - authStart);
	size_t at = authority.rfind('@');
	if (at != std::string::npos)
		authority = authority.substr(at + 1);
	return _StripPort(authority);
}


bool
BWebExtensions::_AppliesTo(const Extension& extension,
	const std::string& url) const
{
	if (!extension.info.enabled)
		return false;
	std::string host = _HostOf(url);
	auto it = fDisabledSites.find(extension.info.id);
	if (it != fDisabledSites.end()
		&& std::find(it->second.begin(), it->second.end(), host)
			!= it->second.end())
		return false;
	for (const auto& pattern : extension.match) {
		if (WebExtensionMatchPattern(pattern.c_str(), url.c_str()))
			return true;
	}
	return false;
}


status_t
BWebExtensions::InstallFromFolder(const char* sourcePath,
	std::string* errorInto)
{
	if (sourcePath == NULL || fUserRoot.empty())
		return B_BAD_VALUE;
	fs::path source(sourcePath);
	std::error_code ec;
	if (!fs::is_directory(source, ec)) {
		if (errorInto != NULL)
			*errorInto = "Not a folder";
		return B_BAD_VALUE;
	}
	if (!fs::exists(source / "manifest.json", ec)) {
		if (errorInto != NULL)
			*errorInto = "No manifest.json in the folder";
		return B_BAD_VALUE;
	}
	std::string json = _ReadFile(source / "manifest.json");
	JsonReader reader = {json.data(), json.data() + json.size()};
	std::string id;
	reader.GetString("id", &id);
	if (id.empty())
		id = source.filename().string();
	fs::path target = fs::path(fUserRoot) / id;
	fs::create_directories(fUserRoot, ec);
	fs::remove_all(target, ec);
	fs::copy(source, target, fs::copy_options::recursive, ec);
	if (ec) {
		if (errorInto != NULL)
			*errorInto = ec.message();
		return B_ERROR;
	}
	Reload();
	_Notify();
	return B_OK;
}


status_t
BWebExtensions::RemoveUserExtension(const char* id,
	std::string* errorInto)
{
	if (id == NULL || fUserRoot.empty())
		return B_BAD_VALUE;
	fs::path target = fs::path(fUserRoot) / id;
	std::error_code ec;
	if (!fs::is_directory(target, ec)) {
		if (errorInto != NULL)
			*errorInto = "Not a user extension";
		return B_ENTRY_NOT_FOUND;
	}
	fs::remove_all(target, ec);
	if (ec) {
		if (errorInto != NULL)
			*errorInto = ec.message();
		return B_ERROR;
	}
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fEnabled.erase(id);
		fDisabledSites.erase(id);
		_SaveState();
	}
	Reload();
	_Notify();
	return B_OK;
}


//	#pragma mark - GM storage


static fs::path
_GMValuesPath(const std::string& userRoot, const std::string& id)
{
	return fs::path(userRoot) / id / "gm.values";
}


bool
BWebExtensions::GetValue(const char* id, const char* key,
	std::string* into) const
{
	if (id == NULL || key == NULL || into == NULL)
		return false;
	std::string userRoot;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		userRoot = fUserRoot;
	}
	if (userRoot.empty())
		return false;
	std::ifstream file(_GMValuesPath(userRoot, id));
	if (!file)
		return false;
	std::string line;
	while (std::getline(file, line)) {
		size_t eq = line.find(" = ");
		if (eq == std::string::npos)
			continue;
		if (line.substr(0, eq) == key) {
			*into = _UnescapeStateValue(line.substr(eq + 3));
			return true;
		}
	}
	return false;
}


status_t
BWebExtensions::SetValue(const char* id, const char* key,
	const char* value)
{
	if (id == NULL || key == NULL || value == NULL)
		return B_BAD_VALUE;
	std::string userRoot;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		userRoot = fUserRoot;
	}
	if (userRoot.empty())
		return B_NO_INIT;

	fs::path path = _GMValuesPath(userRoot, id);
	std::error_code ec;
	fs::create_directories(path.parent_path(), ec);

	// Rewrite the file with the one key replaced; values stay small
	// (GM.setValue is for preferences, not data downloads).
	std::vector<std::string> lines;
	std::ifstream inFile(path);
	std::string line;
	while (std::getline(inFile, line)) {
		size_t eq = line.find(" = ");
		if (eq != std::string::npos && line.substr(0, eq) == key)
			continue;
		if (!line.empty())
			lines.push_back(line);
	}
	std::ofstream outFile(path, std::ios::trunc);
	if (!outFile)
		return B_ERROR;
	for (const auto& existing : lines)
		outFile << existing << "\n";
	outFile << key << " = " << _EscapeStateValue(value) << "\n";
	return B_OK;
}


//	#pragma mark - Payloads for a URL


std::vector<WebExtensionScript>
BWebExtensions::ScriptsForURL(const char* url) const
{
	std::vector<WebExtensionScript> result;
	if (url == NULL)
		return result;
	std::lock_guard<std::mutex> lock(fMutex);
	for (const auto& extension : fExtensions) {
		if (extension.info.kind != "userscript"
			|| !_AppliesTo(extension, url))
			continue;
		WebExtensionScript script;
		script.id = extension.info.id;
		script.source = extension.source;
		script.atDocumentStart = extension.atDocumentStart;
		if (extension.usesGM) {
			// Scripts share the page world; the wrapper binds the
			// GM object to this extension's id.
			script.source = "if (window.__netplusGM) (function (GM) {"
				+ extension.source
				+ "\n})(window.__netplusGM.wrap(\""
				+ extension.info.id + "\"));";
		}
		result.push_back(script);
	}
	return result;
}


std::vector<WebExtensionStyle>
BWebExtensions::StylesForURL(const char* url) const
{
	std::vector<WebExtensionStyle> result;
	if (url == NULL)
		return result;
	std::lock_guard<std::mutex> lock(fMutex);
	for (const auto& extension : fExtensions) {
		if (extension.info.kind != "user-style"
			|| !_AppliesTo(extension, url))
			continue;
		WebExtensionStyle style;
		style.id = extension.info.id;
		style.css = extension.source;
		result.push_back(style);
	}
	return result;
}


std::vector<WebExtensionFilter>
BWebExtensions::FiltersForURL(const char* url) const
{
	std::vector<WebExtensionFilter> result;
	if (url == NULL)
		return result;
	std::lock_guard<std::mutex> lock(fMutex);
	for (const auto& extension : fExtensions) {
		if (extension.info.kind != "content-blocker"
			|| !_AppliesTo(extension, url))
			continue;
		WebExtensionFilter filter;
		filter.id = extension.info.id;
		filter.rulesPath = extension.rulesPath;
		result.push_back(filter);
	}
	return result;
}


std::vector<WebExtensionFilter>
BWebExtensions::AllFilters() const
{
	std::vector<WebExtensionFilter> result;
	std::lock_guard<std::mutex> lock(fMutex);
	for (const auto& extension : fExtensions) {
		if (extension.info.kind != "content-blocker"
			|| !extension.info.enabled)
			continue;
		WebExtensionFilter filter;
		filter.id = extension.info.id;
		filter.rulesPath = extension.rulesPath;
		result.push_back(filter);
	}
	return result;
}


bool
BWebExtensions::UsesGM(const char* url) const
{
	if (url == NULL)
		return false;
	std::lock_guard<std::mutex> lock(fMutex);
	for (const auto& extension : fExtensions) {
		if (extension.info.kind == "userscript" && extension.usesGM
			&& _AppliesTo(extension, url))
			return true;
	}
	return false;
}


void
BWebExtensions::SetTarget(const BMessenger& target)
{
	std::lock_guard<std::mutex> lock(fMutex);
	fTarget = target;
}
