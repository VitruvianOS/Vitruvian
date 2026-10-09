/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "ProxySettings.h"

#include <FindDirectory.h>
#include <Path.h>
#include <String.h>

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


extern char** environ;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ProxySettings"


static const char* kAptHelper = "/usr/libexec/vos-set-proxy";


static bool
_Spawn(const char* path, char* const argv[], BString* errorMessage)
{
	pid_t pid = -1;
	int status = 0;
	int err = posix_spawnp(&pid, path, NULL, NULL, argv, environ);
	if (err != 0) {
		if (errorMessage != NULL)
			*errorMessage = strerror(err);
		return false;
	}
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		if (errorMessage != NULL) {
			*errorMessage = BString(path) << " failed";
			if (WIFEXITED(status))
				*errorMessage << " (exit " << WEXITSTATUS(status) << ")";
		}
		return false;
	}
	return true;
}


ProxySettings::ProxySettings()
	:
	fSettingsMessage(B_USER_SETTINGS_DIRECTORY, "network_proxy"),
	fMode(MODE_NONE),
	fHTTPPort(0),
	fHTTPSPort(0),
	fFTPPort(0),
	fSOCKSPort(0)
{
	fIgnoreHosts = DefaultIgnoreHosts();
	Load();
}


/*static*/ const char*
ProxySettings::DefaultIgnoreHosts()
{
	return "localhost,127.0.0.0/8,::1";
}


void
ProxySettings::Load()
{
	fMode = fSettingsMessage.GetValue("Mode", (uint32)MODE_NONE);
	if (fMode > MODE_AUTOMATIC)
		fMode = MODE_NONE;

	fHTTPHost = fSettingsMessage.GetValue("HTTPHost", BString());
	fHTTPPort = fSettingsMessage.GetValue("HTTPPort", (uint16)0);
	fHTTPSHost = fSettingsMessage.GetValue("HTTPSHost", BString());
	fHTTPSPort = fSettingsMessage.GetValue("HTTPSPort", (uint16)0);
	fFTPHost = fSettingsMessage.GetValue("FTPHost", BString());
	fFTPPort = fSettingsMessage.GetValue("FTPPort", (uint16)0);
	fSOCKSHost = fSettingsMessage.GetValue("SOCKSHost", BString());
	fSOCKSPort = fSettingsMessage.GetValue("SOCKSPort", (uint16)0);
	fIgnoreHosts = fSettingsMessage.GetValue("IgnoreHosts",
		BString(DefaultIgnoreHosts()));
	if (fIgnoreHosts.IsEmpty())
		fIgnoreHosts = DefaultIgnoreHosts();
	fPACURL = fSettingsMessage.GetValue("PACURL", BString());
}


void
ProxySettings::Save()
{
	fSettingsMessage.SetValue("Mode", fMode);
	fSettingsMessage.SetValue("HTTPHost", fHTTPHost);
	fSettingsMessage.SetValue("HTTPPort", fHTTPPort);
	fSettingsMessage.SetValue("HTTPSHost", fHTTPSHost);
	fSettingsMessage.SetValue("HTTPSPort", fHTTPSPort);
	fSettingsMessage.SetValue("FTPHost", fFTPHost);
	fSettingsMessage.SetValue("FTPPort", fFTPPort);
	fSettingsMessage.SetValue("SOCKSHost", fSOCKSHost);
	fSettingsMessage.SetValue("SOCKSPort", fSOCKSPort);
	fSettingsMessage.SetValue("IgnoreHosts", fIgnoreHosts);
	fSettingsMessage.SetValue("PACURL", fPACURL);
	fSettingsMessage.Save();
}


void
ProxySettings::_FormatHostPort(const BString& host, uint16 port,
	BString& out) const
{
	if (host.IsEmpty() || port == 0) {
		out = "";
		return;
	}
	out = host;
	out << ":" << port;
}


bool
ProxySettings::Apply(BString* errorMessage) const
{
	BString http;
	BString https;
	BString ftp;
	BString socks;
	BString ignore = fIgnoreHosts;

	if (fMode == MODE_MANUAL) {
		_FormatHostPort(fHTTPHost, fHTTPPort, http);
		_FormatHostPort(fHTTPSHost, fHTTPSPort, https);
		_FormatHostPort(fFTPHost, fFTPPort, ftp);
		_FormatHostPort(fSOCKSHost, fSOCKSPort, socks);
	}

	return ApplyWith(fMode, http, https, ftp, socks, ignore, fPACURL,
		errorMessage);
}


/*static*/ bool
ProxySettings::ApplyWith(uint32 mode, const BString& http, const BString& https,
	const BString& ftp, const BString& socks, const BString& ignore,
	const BString& pacURL, BString* errorMessage)
{
	// The session reads the store at login; Apply only syncs apt's drop-in.
	// apt cannot honour PAC, so pacURL stays in the settings file.
	(void)pacURL;
	BString socksArg = socks;
	if (!socksArg.IsEmpty() && socksArg.FindFirst("://") < 0)
		socksArg.Prepend("socks5://");

	if (mode == MODE_NONE) {
		char* aptArgv[] = { (char*)"pkexec", (char*)kAptHelper,
			(char*)"none", NULL };
		return _Spawn("pkexec", aptArgv, errorMessage);
	}

	if (mode == MODE_AUTOMATIC) {
		char* aptArgv[] = { (char*)"pkexec", (char*)kAptHelper,
			(char*)"automatic", NULL };
		return _Spawn("pkexec", aptArgv, errorMessage);
	}

	char* aptArgv[] = { (char*)"pkexec", (char*)kAptHelper, (char*)"manual",
		const_cast<char*>(http.String()),
		const_cast<char*>(https.String()),
		const_cast<char*>(ftp.String()),
		const_cast<char*>(socksArg.String()),
		const_cast<char*>(ignore.String()), NULL };
	return _Spawn("pkexec", aptArgv, errorMessage);
}
