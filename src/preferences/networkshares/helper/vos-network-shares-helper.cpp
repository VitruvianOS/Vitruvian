/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Privileged helper for V\OS network shares. Invoked via pkexec; every
 * argument is validated before anything is written or started.
 */

#include "ShareCore.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <String.h>

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>
#include <vector>


extern char** environ;


static const char* kShareDir = "/etc/vos/network-shares";
static const char* kUnitDir = "/etc/systemd/system";
static const char* kHelperPath = "/usr/libexec/vos-network-shares-helper";


static void
Fail(const char* message)
{
	fprintf(stderr, "vos-network-shares-helper: %s\n", message);
	exit(1);
}


static void
Fail(const std::string& message)
{
	fprintf(stderr, "vos-network-shares-helper: %s\n", message.c_str());
	exit(1);
}


static status_t
_RunSystemctl(const std::vector<std::string>& args)
{
	std::vector<char*> argv;
	argv.push_back(const_cast<char*>("systemctl"));
	for (size_t i = 0; i < args.size(); i++)
		argv.push_back(const_cast<char*>(args[i].c_str()));
	argv.push_back(NULL);

	pid_t pid = -1;
	int err = posix_spawnp(&pid, "systemctl", NULL, NULL, &argv[0], environ);
	if (err != 0)
		return -err;
	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return B_ERROR;
	return B_OK;
}


static status_t
_WriteFileAtomic(const std::string& path, const std::string& text,
	mode_t mode)
{
	BPath parent(path.c_str());
	BPath parentPath;
	if (parent.GetParent(&parentPath) != B_OK || parentPath.Path() == NULL)
		return B_BAD_VALUE;
	BDirectory dir(parentPath.Path());
	if (dir.InitCheck() != B_OK)
		return dir.InitCheck();
	std::string tmp = path + ".tmp";
	BFile file(tmp.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();
	if (file.Write(text.c_str(), text.size()) != (ssize_t)text.size())
		return B_ERROR;
	file.Unset();
	if (chmod(tmp.c_str(), mode) != 0)
		return errno;
	if (rename(tmp.c_str(), path.c_str()) != 0)
		return errno;
	return B_OK;
}


static status_t
_RemoveIfExists(const std::string& path)
{
	BEntry entry(path.c_str());
	if (entry.InitCheck() != B_OK)
		return entry.InitCheck();
	if (!entry.Exists())
		return B_OK;
	return entry.Remove();
}


static bool
_LoadShare(const char* id, VosNetworkShareSpec* spec)
{
	std::string path = VosNetworkSharesShareFileName(*spec);
	spec->id = id;
	path = std::string(kShareDir) + "/" + id + ".share";
	BFile file(path.c_str(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;
	off_t size = 0;
	file.GetSize(&size);
	if (size < 0 || size > 64 * 1024)
		return false;
	std::string text((size_t)size, '\0');
	if (size > 0 && file.Read(&text[0], size) != size)
		return false;
	std::string error;
	return VosNetworkSharesParseShareFile(text, spec, &error);
}


// Unit names follow the mount point, so stop and drop them by the spec.
static void
_RemoveUnits(const VosNetworkShareSpec& spec)
{
	const char* const kVerbs[] = { "stop", "disable" };
	for (size_t v = 0; v < 2; v++) {
		for (int automount = 1; automount >= 0; automount--) {
			std::vector<std::string> args;
			args.push_back(kVerbs[v]);
			args.push_back("--quiet");
			args.push_back(VosNetworkSharesUnitName(spec, automount != 0));
			_RunSystemctl(args);
		}
	}
	_RemoveIfExists(std::string(kUnitDir) + "/"
		+ VosNetworkSharesUnitName(spec, false));
	_RemoveIfExists(std::string(kUnitDir) + "/"
		+ VosNetworkSharesUnitName(spec, true));
}


// Two shares on one mount point would write the same unit files.
static bool
_MountPointTaken(const VosNetworkShareSpec& spec)
{
	BDirectory dir(kShareDir);
	if (dir.InitCheck() != B_OK)
		return false;
	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;
		size_t length = strlen(name);
		if (length <= 6 || strcmp(name + length - 6, ".share") != 0)
			continue;
		std::string id(name, length - 6);
		VosNetworkShareSpec other;
		if (id != spec.id && _LoadShare(id.c_str(), &other)
				&& other.mountPoint == spec.mountPoint)
			return true;
	}
	return false;
}


static status_t
_WriteShareFiles(const VosNetworkShareSpec& spec)
{
	BDirectory dir;
	status_t status = dir.SetTo(kShareDir);
	if (status != B_OK) {
		if (mkdir(kShareDir, 0755) != 0 && errno != EEXIST)
			return errno;
		status = dir.SetTo(kShareDir);
		if (status != B_OK)
			return status;
	}

	std::string sharePath = std::string(kShareDir) + "/" + spec.id + ".share";
	std::string shareText = VosNetworkSharesShareFileText(spec);
	status = _WriteFileAtomic(sharePath, shareText, 0644);
	if (status != B_OK)
		return status;

	if (spec.protocol == kShareProtocolSMB && !spec.password.empty()) {
		std::string credsPath = std::string(kShareDir) + "/" + spec.id + ".creds";
		std::string credsText = VosNetworkSharesCredsFileText(spec);
		status = _WriteFileAtomic(credsPath, credsText, 0600);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


static status_t
_WriteUnits(const VosNetworkShareSpec& spec)
{
	std::string credsPath = std::string(kShareDir) + "/" + spec.id + ".creds";
	std::string mountText;
	std::string autoText;
	std::string error;
	const char* useCreds = NULL;
	struct stat st;
	if (stat(credsPath.c_str(), &st) == 0 && spec.protocol == kShareProtocolSMB)
		useCreds = credsPath.c_str();

	if (!VosNetworkSharesWriteUnitText(spec, false, useCreds, &mountText, &error))
		return B_BAD_VALUE;

	std::string mountPath = std::string(kUnitDir) + "/"
		+ VosNetworkSharesUnitName(spec, false);
	status_t status = _WriteFileAtomic(mountPath, mountText, 0644);
	if (status != B_OK)
		return status;

	if (spec.mode == kShareModeAutomount) {
		if (!VosNetworkSharesWriteUnitText(spec, true, NULL, &autoText, &error))
			return B_BAD_VALUE;
		std::string autoPath = std::string(kUnitDir) + "/"
			+ VosNetworkSharesUnitName(spec, true);
		status = _WriteFileAtomic(autoPath, autoText, 0644);
		if (status != B_OK)
			return status;
	} else {
		_RemoveIfExists(std::string(kUnitDir) + "/"
			+ VosNetworkSharesUnitName(spec, true));
	}
	return B_OK;
}


static status_t
_SyncEnableState(const VosNetworkShareSpec& spec)
{
	std::string mountName = VosNetworkSharesUnitName(spec, false);
	std::string autoName = VosNetworkSharesUnitName(spec, true);

	std::vector<std::string> disableArgs;
	disableArgs.push_back("disable");
	disableArgs.push_back("--quiet");
	disableArgs.push_back(mountName);
	_RunSystemctl(disableArgs);

	std::vector<std::string> disableAuto;
	disableAuto.push_back("disable");
	disableAuto.push_back("--quiet");
	disableAuto.push_back(autoName);
	_RunSystemctl(disableAuto);

	if (spec.mode == kShareModeLogin) {
		// Written for the session, not enabled at boot.
		return B_OK;
	}

	std::string unitToEnable = spec.mode == kShareModeAutomount
		? autoName : mountName;
	std::vector<std::string> enableArgs;
	enableArgs.push_back("enable");
	enableArgs.push_back("--quiet");
	enableArgs.push_back(unitToEnable);
	status_t status = _RunSystemctl(enableArgs);
	if (status != B_OK)
		return status;
	std::vector<std::string> reload;
	reload.push_back("daemon-reload");
	_RunSystemctl(reload);
	return B_OK;
}


static status_t
_PrepareMountPoint(const VosNetworkShareSpec& spec)
{
	if (!VosNetworkSharesValidMountPoint(spec.mountPoint.c_str()))
		return B_BAD_VALUE;
	BPath parent(spec.mountPoint.c_str());
	BPath parentPath;
	if (parent.GetParent(&parentPath) != B_OK || parentPath.Path() == NULL)
		return B_BAD_VALUE;
	BDirectory dir(parentPath.Path());
	if (dir.InitCheck() != B_OK) {
		if (mkdir(parentPath.Path(), 0755) != 0 && errno != EEXIST)
			return errno;
	}
	struct stat st;
	if (stat(spec.mountPoint.c_str(), &st) == 0)
		return B_OK;
	if (mkdir(spec.mountPoint.c_str(), 0755) != 0 && errno != EEXIST)
		return errno;
	return B_OK;
}


static status_t
_DaemonReload()
{
	std::vector<std::string> reload;
	reload.push_back("daemon-reload");
	return _RunSystemctl(reload);
}


static int
_CmdAdd(const std::vector<std::string>& args, bool replace)
{
	// add/edit <id> <protocol> <server> <export> <mountpoint> <mode> <options>
	//          [username] [domain]
	// SMB password is read from stdin when present so it stays off argv.
	if (args.size() < 8)
		Fail("usage: add|edit <id> <protocol> <server> <export> <mountpoint> <mode> <options> [username] [domain]");

	VosNetworkShareSpec spec;
	spec.id = args[1];
	bool ok = false;
	spec.protocol = VosNetworkSharesParseProtocol(args[2].c_str(), &ok);
	if (!ok)
		Fail("protocol must be nfs or smb");
	spec.server = args[3];
	spec.exportName = args[4];
	spec.mountPoint = args[5];
	spec.mode = VosNetworkSharesParseMode(args[6].c_str(), &ok);
	if (!ok)
		Fail("mode must be boot, login or automount");
	spec.options = args[7];
	if (args.size() > 8)
		spec.username = args[8];
	if (args.size() > 9)
		spec.domain = args[8 + 1];

	if (spec.protocol == kShareProtocolSMB && spec.password.empty()) {
		char line[512];
		if (fgets(line, sizeof(line), stdin) != NULL) {
			std::string pw(line);
			while (!pw.empty() && (pw[pw.size() - 1] == '\n'
					|| pw[pw.size() - 1] == '\r'))
				pw.erase(pw.size() - 1);
			spec.password = pw;
		}
	}

	std::string error;
	if (!VosNetworkSharesValidate(spec, &error))
		Fail(error);

	if (!replace) {
		struct stat st;
		std::string sharePath = std::string(kShareDir) + "/" + spec.id + ".share";
		if (stat(sharePath.c_str(), &st) == 0)
			Fail("share id already exists");
	}
	if (_MountPointTaken(spec))
		Fail("another share already uses this mount point");

	VosNetworkShareSpec old;
	if (replace && _LoadShare(spec.id.c_str(), &old)
			&& old.mountPoint != spec.mountPoint)
		_RemoveUnits(old);

	status_t status = _PrepareMountPoint(spec);
	if (status != B_OK)
		Fail("could not create mount point");

	status = _WriteShareFiles(spec);
	if (status != B_OK)
		Fail("could not write share configuration");

	status = _WriteUnits(spec);
	if (status != B_OK)
		Fail("could not write systemd units");

	_DaemonReload();
	status = _SyncEnableState(spec);
	if (status != B_OK)
		Fail("could not enable units for this mode");
	return 0;
}


static int
_CmdRemove(const std::vector<std::string>& args)
{
	if (args.size() != 2)
		Fail("usage: remove <id>");
	if (!VosNetworkSharesValidId(args[1].c_str()))
		Fail("bad share id");

	VosNetworkShareSpec spec;
	if (_LoadShare(args[1].c_str(), &spec))
		_RemoveUnits(spec);
	_RemoveIfExists(std::string(kShareDir) + "/" + args[1] + ".share");
	_RemoveIfExists(std::string(kShareDir) + "/" + args[1] + ".creds");
	_DaemonReload();
	return 0;
}


static int
_CmdStart(const std::vector<std::string>& args)
{
	if (args.size() != 2)
		Fail("usage: mount|start <id>");
	if (!VosNetworkSharesValidId(args[1].c_str()))
		Fail("bad share id");

	VosNetworkShareSpec spec;
	if (!_LoadShare(args[1].c_str(), &spec))
		Fail("unknown share id");
	_PrepareMountPoint(spec);

	std::vector<std::string> startArgs;
	startArgs.push_back("start");
	startArgs.push_back(VosNetworkSharesUnitName(spec, false));
	if (_RunSystemctl(startArgs) != B_OK)
		Fail("mount failed; is the server reachable?");
	return 0;
}


static int
_CmdStop(const std::vector<std::string>& args)
{
	if (args.size() != 2)
		Fail("usage: unmount|stop <id>");
	if (!VosNetworkSharesValidId(args[1].c_str()))
		Fail("bad share id");

	VosNetworkShareSpec spec;
	if (!_LoadShare(args[1].c_str(), &spec))
		Fail("unknown share id");
	std::vector<std::string> stopArgs;
	stopArgs.push_back("stop");
	stopArgs.push_back(VosNetworkSharesUnitName(spec, false));
	if (_RunSystemctl(stopArgs) != B_OK)
		Fail("unmount failed");
	return 0;
}


static int
_CmdSetMode(const std::vector<std::string>& args)
{
	if (args.size() != 3)
		Fail("usage: set-mode <id> <boot|login|automount>");
	if (!VosNetworkSharesValidId(args[1].c_str()))
		Fail("bad share id");
	bool ok = false;
	int mode = VosNetworkSharesParseMode(args[2].c_str(), &ok);
	if (!ok)
		Fail("mode must be boot, login or automount");

	VosNetworkShareSpec spec;
	if (!_LoadShare(args[1].c_str(), &spec))
		Fail("unknown share id");
	spec.mode = mode;
	std::string error;
	if (!VosNetworkSharesValidate(spec, &error))
		Fail(error);
	status_t status = _WriteShareFiles(spec);
	if (status != B_OK)
		Fail("could not update share configuration");
	status = _WriteUnits(spec);
	if (status != B_OK)
		Fail("could not rewrite units");
	_DaemonReload();
	status = _SyncEnableState(spec);
	if (status != B_OK)
		Fail("could not update enablement");
	return 0;
}


int
main(int argc, char** argv)
{
	if (geteuid() != 0)
		Fail("must run as root (invoke via pkexec)");

	if (argc < 2)
		Fail("usage: add|edit|remove|mount|unmount|set-mode ...");

	std::vector<std::string> args;
	for (int i = 1; i < argc; i++)
		args.push_back(argv[i]);

	const std::string& cmd = args[0];
	if (cmd == "add")
		return _CmdAdd(args, false);
	if (cmd == "edit")
		return _CmdAdd(args, true);
	if (cmd == "remove")
		return _CmdRemove(args);
	if (cmd == "mount" || cmd == "start")
		return _CmdStart(args);
	if (cmd == "unmount" || cmd == "stop")
		return _CmdStop(args);
	if (cmd == "set-mode")
		return _CmdSetMode(args);
	Fail("unknown command");
	return 1;
}
