/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Share definition validation and systemd unit text for V\OS network shares.
 */
#ifndef _NETWORK_SHARES_CORE_H
#define _NETWORK_SHARES_CORE_H


#include <string>


// Mount modes. boot: enable .mount at boot (short nofail timeout).
// login: unit written but not enabled at boot; started from the session.
// automount: enable .automount only; first access starts the mount.
enum {
	kShareModeBoot		= 0,
	kShareModeLogin		= 1,
	kShareModeAutomount	= 2,
};


enum {
	kShareProtocolNFS	= 0,
	kShareProtocolSMB	= 1,
};


const char*	VosNetworkSharesDefaultMountRoot();
const char*	VosNetworkSharesDefaultNFSServer();
int		VosNetworkSharesDefaultNFSExport();

bool	VosNetworkSharesValidId(const char* id);

bool	VosNetworkSharesValidServer(const char* server);

bool	VosNetworkSharesValidExport(int protocol, const char* exportName);

bool	VosNetworkSharesValidMountPoint(const char* mountPoint);

int	VosNetworkSharesParseMode(const char* mode, bool* ok);
const char* VosNetworkSharesModeName(int mode);

int	VosNetworkSharesParseProtocol(const char* protocol, bool* ok);
const char* VosNetworkSharesProtocolName(int protocol);
const char* VosNetworkSharesSystemdType(int protocol);

bool	VosNetworkSharesValidOptions(const char* options);

struct VosNetworkShareSpec {
	std::string	id;
	int		protocol;
	std::string	server;
	std::string	exportName;
	std::string	mountPoint;
	int		mode;
	std::string	options;
	// SMB only; empty for NFS. Never written into the unit file.
	std::string	username;
	std::string	password;
	std::string	domain;

	VosNetworkShareSpec()
		:
		protocol(kShareProtocolNFS),
		mode(kShareModeBoot)
	{
	}
};

bool	VosNetworkSharesValidate(const VosNetworkShareSpec& spec, std::string* error);

std::string	VosNetworkSharesUnitName(const VosNetworkShareSpec& spec, bool automount);
std::string	VosNetworkSharesCredsFileName(const VosNetworkShareSpec& spec);
std::string	VosNetworkSharesShareFileName(const VosNetworkShareSpec& spec);
std::string	VosNetworkSharesWhat(const VosNetworkShareSpec& spec);

bool	VosNetworkSharesWriteUnitText(const VosNetworkShareSpec& spec, bool automount,
	const char* credsPath, std::string* unitText, std::string* error);

// Serialize/deserialize the share conf the helper stores under
// /etc/vos/network-shares/<id>.share. Password is never stored here.
std::string	VosNetworkSharesShareFileText(const VosNetworkShareSpec& spec);
bool	VosNetworkSharesParseShareFile(const std::string& text, VosNetworkShareSpec* spec,
	std::string* error);

// Credentials file text written by the helper (root-only).
std::string	VosNetworkSharesCredsFileText(const VosNetworkShareSpec& spec);

std::string	VosNetworkSharesTrim(const std::string& value);

#endif	// _NETWORK_SHARES_CORE_H
