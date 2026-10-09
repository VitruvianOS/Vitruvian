/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "DisplayResumeGuard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Path.h>
#include <SymLink.h>


// Kernel driver names use underscores; sysfs driver links use hyphens.
// None of these bring the display back after S3 or S4 in the shipped kernel.
static const struct {
	const char*	name;
	bool		guardsSuspend;
	bool		guardsHibernate;
} kBadDisplayDrivers[] = {
	// virtio-gpu has no suspend/resume and no freeze/restore in 6.12.
	// QEMU resets the device on sleep; a kworker then hangs on ctrl I/O.
	{ "virtio_gpu", true, true },
};


static BString
_NormalizeDriver(const BString& name)
{
	BString out(name);
	out.ReplaceAll("-", "_");
	return out;
}


DisplayResumeGuard::DisplayResumeGuard()
	:
	fDriver()
{
}


bool
DisplayResumeGuard::Detect(const char* sysfsRoot)
{
	fDriver = "";
	const char* root = sysfsRoot != NULL ? sysfsRoot : "/sys/class/drm";

	BDirectory drm(root);
	if (drm.InitCheck() != B_OK)
		return false;

	// Preference matches DrmDeviceSelect: connected connector, then
	// boot_vga, then any card that has a bound driver.
	BString bestDriver;
	int bestRank = -1;

	BEntry entry;
	while (drm.GetNextEntry(&entry) == B_OK) {
		char name[64];
		if (entry.GetName(name) != B_OK)
			continue;
		if (strncmp(name, "card", 4) != 0)
			continue;
		const char* p = name + 4;
		if (*p == '\0')
			continue;
		bool allDigits = true;
		for (; *p != '\0'; p++) {
			if (*p < '0' || *p > '9') {
				allDigits = false;
				break;
			}
		}
		if (!allDigits)
			continue;

		char path[256];
		snprintf(path, sizeof(path), "%s/%s", root, name);

		BString driver;
		if (!_ReadDriver(path, driver))
			continue;

		int rank = 0;
		if (_IsBootVga(path))
			rank = 1;
		if (_HasConnectedConnector(path))
			rank = 2;

		if (rank > bestRank) {
			bestRank = rank;
			bestDriver = driver;
		}
	}

	if (bestRank < 0)
		return false;
	fDriver = bestDriver;
	return true;
}


bool
DisplayResumeGuard::GuardsSuspend() const
{
	if (fDriver.IsEmpty())
		return false;
	BString driver = _NormalizeDriver(fDriver);
	for (size_t i = 0; i < sizeof(kBadDisplayDrivers)
			/ sizeof(kBadDisplayDrivers[0]); i++) {
		if (driver == kBadDisplayDrivers[i].name)
			return kBadDisplayDrivers[i].guardsSuspend;
	}
	return false;
}


bool
DisplayResumeGuard::GuardsHibernate() const
{
	if (fDriver.IsEmpty())
		return false;
	BString driver = _NormalizeDriver(fDriver);
	for (size_t i = 0; i < sizeof(kBadDisplayDrivers)
			/ sizeof(kBadDisplayDrivers[0]); i++) {
		if (driver == kBadDisplayDrivers[i].name)
			return kBadDisplayDrivers[i].guardsHibernate;
	}
	return false;
}


bool
DisplayResumeGuard::_ReadDriver(const char* cardPath, BString& driver)
{
	BString linkPath(cardPath);
	linkPath << "/device/driver";
	BSymLink link(linkPath.String());
	if (link.InitCheck() != B_OK)
		return false;
	char target[256];
	if (link.ReadLink(target, sizeof(target)) <= 0)
		return false;
	const char* leaf = strrchr(target, '/');
	leaf = leaf != NULL ? leaf + 1 : target;
	driver = leaf;
	return !driver.IsEmpty();
}


bool
DisplayResumeGuard::_HasConnectedConnector(const char* cardPath)
{
	BPath path(cardPath);
	BPath parent;
	if (path.GetParent(&parent) != B_OK)
		return false;

	// Connectors sit beside the card as cardN-CONNECTOR entries.
	BString prefix(path.Leaf());
	prefix << "-";

	BDirectory dir(parent.Path());
	if (dir.InitCheck() != B_OK)
		return false;

	BEntry entry;
	while (dir.GetNextEntry(&entry) == B_OK) {
		char name[64];
		if (entry.GetName(name) != B_OK)
			continue;
		if (strncmp(name, prefix.String(), prefix.Length()) != 0)
			continue;

		BString statusPath;
		statusPath << parent.Path() << "/" << name << "/status";
		BFile file(statusPath.String(), B_READ_ONLY);
		if (file.InitCheck() != B_OK)
			continue;
		char status[32] = "";
		if (file.Read(status, sizeof(status) - 1) <= 0)
			continue;
		if (strncmp(status, "connected", 9) == 0)
			return true;
	}
	return false;
}


bool
DisplayResumeGuard::_IsBootVga(const char* cardPath)
{
	BString path(cardPath);
	path << "/device/boot_vga";
	BFile file(path.String(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;
	char buf[4] = "";
	if (file.Read(buf, sizeof(buf) - 1) <= 0)
		return false;
	return atoi(buf) == 1;
}
