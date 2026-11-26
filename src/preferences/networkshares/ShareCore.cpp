/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ShareCore.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>


// Mount options the preflet may put in a unit. Anything else is refused so
// a crafted share cannot smuggle systemd mount directives through Options=.
static const char* const kAllowedOptions[] = {
	"_netdev",
	"auto",
	"noauto",
	"noexec",
	"nosuid",
	"nodev",
	"ro",
	"rw",
	"soft",
	"hard",
	"lookupcache=none",
	"lookupcache=pos",
	"lookupcache=fsc",
	"lookupcache=ac",
	"lookupcache=all",
	"nconnect=4",
	"vers=2",
	"vers=3",
	"vers=4",
	"vers=4.0",
	"vers=4.1",
	"vers=4.2",
	"guest",
	"noserverino",
	"noperm",
	"nofail",
	"defaults",
	"acl",
	"noacl",
	"mfsymlinks",
	"serverino",
	"actimeo=1",
	"actimeo=60",
	"retry=2",
	"retry=30",
	"timeo=30",
	"timeo=10",
	"retrans=2",
	"retrans=5",
	"seal",
};


static bool
_AllowListedOption(const std::string& token)
{
	for (size_t i = 0; i < sizeof(kAllowedOptions) / sizeof(kAllowedOptions[0]); i++) {
		if (token == kAllowedOptions[i])
			return true;
	}
	return false;
}


static bool
_OnlyAllowedChars(const char* s, const char* allowed)
{
	if (s == NULL)
		return false;
	for (const char* p = s; *p != '\0'; p++) {
		if (strchr(allowed, *p) == NULL)
			return false;
	}
	return true;
}


const char*
VosNetworkSharesDefaultMountRoot()
{
	return "/mnt/network";
}


const char*
VosNetworkSharesDefaultNFSServer()
{
	return "127.0.0.1";
}


int
VosNetworkSharesDefaultNFSExport()
{
	return 1;
}


bool
VosNetworkSharesValidId(const char* id)
{
	if (id == NULL || id[0] == '\0' || strlen(id) > 64)
		return false;
	if (!std::isalnum((unsigned char)id[0]))
		return false;
	for (const char* p = id + 1; *p != '\0'; p++) {
		if (!std::isalnum((unsigned char)*p) && *p != '_' && *p != '-')
			return false;
	}
	return true;
}


bool
VosNetworkSharesValidServer(const char* server)
{
	if (server == NULL || server[0] == '\0' || strlen(server) > 253)
		return false;
	if (!_OnlyAllowedChars(server, "abcdefghijklmnopqrstuvwxyz"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.:-[]"))
		return false;
	if (server[0] == '.' || server[strlen(server) - 1] == '.')
		return false;
	return true;
}


bool
VosNetworkSharesValidExport(int protocol, const char* exportName)
{
	if (exportName == NULL || exportName[0] == '\0')
		return false;
	if (strlen(exportName) > 256)
		return false;
	if (strchr(exportName, '"') != NULL || strchr(exportName, '\n') != NULL
			|| strchr(exportName, '\r') != NULL || strchr(exportName, '\\') != NULL)
		return false;
	if (strstr(exportName, "..") != NULL)
		return false;

	if (protocol == kShareProtocolNFS) {
		if (exportName[0] != '/')
			return false;
		return true;
	}

	if (strchr(exportName, '/') != NULL || strchr(exportName, '\\') != NULL)
		return false;
	if (exportName[0] == ' ' || exportName[strlen(exportName) - 1] == ' ')
		return false;
	return true;
}


bool
VosNetworkSharesValidMountPoint(const char* mountPoint)
{
	if (mountPoint == NULL || mountPoint[0] != '/')
		return false;
	if (strlen(mountPoint) > 256)
		return false;
	if (strstr(mountPoint, "..") != NULL)
		return false;
	if (strchr(mountPoint, '\n') != NULL || strchr(mountPoint, '\r') != NULL
			|| strchr(mountPoint, '"') != NULL)
		return false;
	const char* root = VosNetworkSharesDefaultMountRoot();
	size_t rootLen = strlen(root);
	if (strncmp(mountPoint, root, rootLen) != 0)
		return false;
	if (mountPoint[rootLen] != '/' && mountPoint[rootLen] != '\0')
		return false;
	if (mountPoint[rootLen] == '\0')
		return false;	// refuse the root itself
	return true;
}


int
VosNetworkSharesParseMode(const char* mode, bool* ok)
{
	if (ok != NULL)
		*ok = false;
	if (mode == NULL)
		return -1;
	std::string m = mode;
	for (size_t i = 0; i < m.size(); i++)
		m[i] = (char)std::tolower((unsigned char)m[i]);
	if (m == "boot") {
		if (ok != NULL)
			*ok = true;
		return kShareModeBoot;
	}
	if (m == "login") {
		if (ok != NULL)
			*ok = true;
		return kShareModeLogin;
	}
	if (m == "automount") {
		if (ok != NULL)
			*ok = true;
		return kShareModeAutomount;
	}
	return -1;
}


const char*
VosNetworkSharesModeName(int mode)
{
	switch (mode) {
		case kShareModeBoot:
			return "boot";
		case kShareModeLogin:
			return "login";
		case kShareModeAutomount:
			return "automount";
		default:
			return "boot";
	}
}


int
VosNetworkSharesParseProtocol(const char* protocol, bool* ok)
{
	if (ok != NULL)
		*ok = false;
	if (protocol == NULL)
		return -1;
	std::string p = protocol;
	for (size_t i = 0; i < p.size(); i++)
		p[i] = (char)std::tolower((unsigned char)p[i]);
	if (p == "nfs" || p == "nfs4") {
		if (ok != NULL)
			*ok = true;
		return kShareProtocolNFS;
	}
	if (p == "smb" || p == "cifs" || p == "smbfs") {
		if (ok != NULL)
			*ok = true;
		return kShareProtocolSMB;
	}
	return -1;
}


const char*
VosNetworkSharesProtocolName(int protocol)
{
	return protocol == kShareProtocolSMB ? "smb" : "nfs";
}


const char*
VosNetworkSharesSystemdType(int protocol)
{
	return protocol == kShareProtocolSMB ? "cifs" : "nfs";
}


bool
VosNetworkSharesValidOptions(const char* options)
{
	if (options == NULL || options[0] == '\0')
		return true;
	if (strlen(options) > 512)
		return false;
	if (strchr(options, '\n') != NULL || strchr(options, '\r') != NULL
			|| strchr(options, '"') != NULL || strchr(options, '\\') != NULL)
		return false;
	// A newline-separated list would let Options= span unit lines.
	std::string text(options);
	std::stringstream ss(text);
	std::string token;
	while (std::getline(ss, token, ',')) {
		while (!token.empty() && token[0] == ' ')
			token.erase(0, 1);
		while (!token.empty() && token[token.size() - 1] == ' ')
			token.erase(token.size() - 1);
		if (token.empty())
			continue;
		if (!_AllowListedOption(token))
			return false;
	}
	return true;
}


bool
VosNetworkSharesValidate(const VosNetworkShareSpec& spec, std::string* error)
{
	std::string fail;
	if (!VosNetworkSharesValidId(spec.id.c_str()))
		fail = "id must match [A-Za-z0-9][A-Za-z0-9_-]{0,63}";
	if (fail.empty() && spec.protocol != kShareProtocolNFS
			&& spec.protocol != kShareProtocolSMB)
		fail = "protocol must be nfs or smb";
	if (fail.empty() && !VosNetworkSharesValidServer(spec.server.c_str()))
		fail = "server must be a hostname or IP address";
	if (fail.empty() && !VosNetworkSharesValidExport(spec.protocol, spec.exportName.c_str()))
		fail = spec.protocol == kShareProtocolNFS
			? "export must be an absolute path without .."
			: "share name must not contain path separators";
	if (fail.empty() && !VosNetworkSharesValidMountPoint(spec.mountPoint.c_str()))
		fail = "mount point must be under /mnt/network and not be the root";
	if (fail.empty() && spec.mode != kShareModeBoot && spec.mode != kShareModeLogin
			&& spec.mode != kShareModeAutomount)
		fail = "mode must be boot, login or automount";
	if (fail.empty() && !VosNetworkSharesValidOptions(spec.options.c_str()))
		fail = "options contain a value outside the allowlist";
	if (fail.empty() && spec.password.find('\n') != std::string::npos)
		fail = "password must not contain a newline";
	if (!fail.empty()) {
		if (error != NULL)
			*error = fail;
		return false;
	}
	return true;
}


// systemd only accepts a mount unit named after its escaped Where= path,
// as systemd-escape --path builds it.
static std::string
_EscapeUnitPath(const std::string& path)
{
	static const char kHex[] = "0123456789abcdef";
	std::string name;
	bool slash = true;
	for (size_t i = 0; i < path.size(); i++) {
		unsigned char c = (unsigned char)path[i];
		if (c == '/') {
			slash = true;
			continue;
		}
		if (slash && !name.empty())
			name += '-';
		bool plain = std::isalnum(c) || c == ':' || c == '_'
			|| (c == '.' && !name.empty());
		if (plain)
			name += (char)c;
		else {
			name += "\\x";
			name += kHex[c >> 4];
			name += kHex[c & 0xf];
		}
		slash = false;
	}
	return name;
}


std::string
VosNetworkSharesUnitName(const VosNetworkShareSpec& spec, bool automount)
{
	return _EscapeUnitPath(spec.mountPoint)
		+ (automount ? ".automount" : ".mount");
}


std::string
VosNetworkSharesCredsFileName(const VosNetworkShareSpec& spec)
{
	return "/etc/vos/network-shares/" + spec.id + ".creds";
}


std::string
VosNetworkSharesShareFileName(const VosNetworkShareSpec& spec)
{
	return "/etc/vos/network-shares/" + spec.id + ".share";
}


std::string
VosNetworkSharesWhat(const VosNetworkShareSpec& spec)
{
	if (spec.protocol == kShareProtocolSMB) {
		std::string what = "//";
		what += spec.server;
		what += "/";
		what += spec.exportName;
		return what;
	}
	std::string what = spec.server;
	what += ":";
	what += spec.exportName;
	return what;
}


bool
VosNetworkSharesWriteUnitText(const VosNetworkShareSpec& spec, bool automount,
	const char* credsPath, std::string* unitText, std::string* error)
{
	if (!VosNetworkSharesValidate(spec, error))
		return false;
	if (unitText == NULL) {
		if (error != NULL)
			*error = "unitText is null";
		return false;
	}

	std::ostringstream out;
	out << "# Generated by vos-network-shares-helper. Do not edit.\n";
	out << "[Unit]\n";
	out << "Description=V\\OS network share " << spec.server << ":" << spec.exportName << "\n";
	// network-online, never local-fs: a dead share must not block local disks.
	out << "After=network-online.target\n";
	out << "Wants=network-online.target\n";
	out << "Documentation=man:systemd.mount(5)\n";
	if (automount) {
		out << "\n[Automount]\n";
		out << "Where=" << spec.mountPoint << "\n";
		// Idle timeout 0: stay automounted once touched; no busy re-trigger.
		out << "TimeoutIdleSec=0\n";
	} else {
		out << "\n[Mount]\n";
		out << "What=" << VosNetworkSharesWhat(spec) << "\n";
		out << "Where=" << spec.mountPoint << "\n";
		out << "Type=" << VosNetworkSharesSystemdType(spec.protocol) << "\n";
		std::string options = spec.options;
		// nofail + short timeout: boot continues even if the server is down.
		if (!options.empty())
			options += ",";
		options += "nofail";
		// cifs reads the credentials file; the password never appears in the unit.
		if (credsPath != NULL && credsPath[0] != '\0'
				&& spec.protocol == kShareProtocolSMB) {
			options += ",credentials=";
			options += credsPath;
		}
		out << "Options=" << options << "\n";
		out << "TimeoutSec=15\n";
	}
	// WantedBy multi-user only: never local-fs / sysroot, so a failing
	// share cannot delay or fail the root filesystem transaction.
	out << "\n[Install]\n";
	out << "WantedBy=multi-user.target\n";

	*unitText = out.str();
	return true;
}


std::string
VosNetworkSharesShareFileText(const VosNetworkShareSpec& spec)
{
	std::ostringstream out;
	out << "# V\\OS network share definition. Password lives in the .creds file.\n";
	out << "id=" << spec.id << "\n";
	out << "protocol=" << VosNetworkSharesProtocolName(spec.protocol) << "\n";
	out << "server=" << spec.server << "\n";
	out << "export=" << spec.exportName << "\n";
	out << "mountpoint=" << spec.mountPoint << "\n";
	out << "mode=" << VosNetworkSharesModeName(spec.mode) << "\n";
	out << "options=" << spec.options << "\n";
	return out.str();
}


bool
VosNetworkSharesParseShareFile(const std::string& text, VosNetworkShareSpec* spec,
	std::string* error)
{
	if (spec == NULL)
		return false;
	VosNetworkShareSpec parsed;
	std::istringstream ss(text);
	std::string line;
	while (std::getline(ss, line)) {
		line = VosNetworkSharesTrim(line);
		if (line.empty() || line[0] == '#')
			continue;
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = VosNetworkSharesTrim(line.substr(0, eq));
		std::string value = VosNetworkSharesTrim(line.substr(eq + 1));
		if (key == "id")
			parsed.id = value;
		else if (key == "protocol") {
			bool ok = false;
			parsed.protocol = VosNetworkSharesParseProtocol(value.c_str(), &ok);
			if (!ok) {
				if (error != NULL)
					*error = "bad protocol in share file";
				return false;
			}
		} else if (key == "server")
			parsed.server = value;
		else if (key == "export")
			parsed.exportName = value;
		else if (key == "mountpoint")
			parsed.mountPoint = value;
		else if (key == "mode") {
			bool ok = false;
			parsed.mode = VosNetworkSharesParseMode(value.c_str(), &ok);
			if (!ok) {
				if (error != NULL)
					*error = "bad mode in share file";
				return false;
			}
		} else if (key == "options")
			parsed.options = value;
	}
	if (!VosNetworkSharesValidate(parsed, error))
		return false;
	*spec = parsed;
	return true;
}


std::string
VosNetworkSharesCredsFileText(const VosNetworkShareSpec& spec)
{
	std::ostringstream out;
	out << "username=" << spec.username << "\n";
	if (!spec.domain.empty())
		out << "domain=" << spec.domain << "\n";
	out << "password=" << spec.password << "\n";
	return out.str();
}


std::string
VosNetworkSharesTrim(const std::string& value)
{
	size_t start = 0;
	size_t end = value.size();
	while (start < end && (value[start] == ' ' || value[start] == '\t'))
		start++;
	while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t'
			|| value[end - 1] == '\r' || value[end - 1] == '\n'))
		end--;
	return value.substr(start, end - start);
}
