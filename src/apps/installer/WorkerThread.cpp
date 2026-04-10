/*
 * Copyright 2024, Vitruvian OS.
 * Based on Haiku Installer, Copyright 2005-2009, various authors.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "WorkerThread.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <set>
#include <string>
#include <strings.h>

#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Directory.h>
#include <DiskDeviceVisitor.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <String.h>
#include <VolumeRoster.h>

#include "CopyEngine.h"
#include "InstallerDefs.h"
#include "PartitionMenuItem.h"
#include "ProgressReporter.h"
#include "StringForSize.h"


#define B_TRANSLATION_CONTEXT "InstallProgress"


const char BOOT_PATH[] = "/";

const uint32 MSG_START_INSTALLING = 'eSRT';

static const char* kLinuxFSTypes[] = {
	"Ext2",
	"XFS",
	"Btrfs",
	NULL
};

static bool
_IsLinuxFS(const char* contentType)
{
	if (contentType == NULL)
		return false;
	for (int i = 0; kLinuxFSTypes[i] != NULL; i++) {
		if (strcmp(contentType, kLinuxFSTypes[i]) == 0)
			return true;
	}
	return false;
}


class SourceVisitor : public BDiskDeviceVisitor {
public:
	SourceVisitor(BMenu* menu);
	virtual bool Visit(BDiskDevice* device);
	virtual bool Visit(BPartition* partition, int32 level);

private:
	BMenu* fMenu;
};


class TargetVisitor : public BDiskDeviceVisitor {
public:
	TargetVisitor(BMenu* menu);
	virtual bool Visit(BDiskDevice* device);
	virtual bool Visit(BPartition* partition, int32 level);

private:
	BMenu* fMenu;
};


class WorkerThread::EntryFilter : public CopyEngine::EntryFilter {
public:
	EntryFilter()
		:
		fIgnorePaths()
	{
		try {
			fIgnorePaths.insert("proc");
			fIgnorePaths.insert("sys");
			fIgnorePaths.insert("dev");
			fIgnorePaths.insert("run");
			fIgnorePaths.insert("tmp");
			fIgnorePaths.insert("var/cache");
			fIgnorePaths.insert("var/tmp");
		} catch (std::bad_alloc&) {
		}
	}

	virtual bool ShouldCopyEntry(const BEntry& entry, const char* path,
		const struct stat& statInfo) const
	{
		if (S_ISBLK(statInfo.st_mode) || S_ISCHR(statInfo.st_mode)
				|| S_ISFIFO(statInfo.st_mode) || S_ISSOCK(statInfo.st_mode)) {
			return false;
		}

		if (fIgnorePaths.find(path) != fIgnorePaths.end())
			return false;

		return true;
	}

private:
	typedef std::set<std::string> StringSet;
	StringSet fIgnorePaths;
};


// #pragma mark - WorkerThread


WorkerThread::WorkerThread(const BMessenger& owner)
	:
	BLooper("copy_engine"),
	fOwner(owner),
	fCancelSemaphore(-1)
{
	Run();
}


void
WorkerThread::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_START_INSTALLING:
			_PerformInstall(message->GetInt32("source", -1),
				message->GetInt32("target", -1));
			break;

		default:
			BLooper::MessageReceived(message);
	}
}


void
WorkerThread::ScanDisksPartitions(BMenu* srcMenu, BMenu* targetMenu)
{
	SourceVisitor srcVisitor(srcMenu);
	BDiskDevice device;
	BPartition* partition = NULL;
	fDDRoster.VisitEachMountedPartition(&srcVisitor, &device, &partition);

	TargetVisitor targetVisitor(targetMenu);
	fDDRoster.VisitEachPartition(&targetVisitor, &device, &partition);
}


void
WorkerThread::StartInstall(partition_id sourcePartitionID,
	partition_id targetPartitionID)
{
	BMessage message(MSG_START_INSTALLING);
	message.AddInt32("source", sourcePartitionID);
	message.AddInt32("target", targetPartitionID);

	PostMessage(&message, this);
}


// #pragma mark -


status_t
WorkerThread::_PerformInstall(partition_id sourcePartitionID,
	partition_id targetPartitionID)
{
	BPath targetDirectory;
	BDiskDevice device;
	BPartition* partition;
	BVolume targetVolume;
	status_t err = B_OK;
	const char* mountError = B_TRANSLATE("The partition can't be mounted. Please "
		"choose a different partition.");

	if (targetPartitionID < 0) {
		return _InstallationError(B_BAD_VALUE);
	}

	if (fDDRoster.GetPartitionWithID(targetPartitionID, &device,
			&partition) == B_OK) {
		if (!partition->IsMounted()) {
			if ((err = partition->Mount()) < B_OK) {
				_SetStatusMessage(mountError);
				return _InstallationError(err);
			}
		}
		if ((err = partition->GetVolume(&targetVolume)) != B_OK)
			return _InstallationError(err);
		if ((err = partition->GetMountPoint(&targetDirectory)) != B_OK)
			return _InstallationError(err);
	} else
		return _InstallationError(B_ERROR);

	// check target has enough space (at least 2 GB free)
	if (targetVolume.FreeBytes() < (off_t)2 * 1024 * 1024 * 1024) {
		BAlert* alert = new BAlert("", B_TRANSLATE("The destination partition may "
			"not have enough space. Try choosing a different partition."),
			B_TRANSLATE("Try installing anyway"), B_TRANSLATE("Cancel"), 0,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->SetShortcut(1, B_ESCAPE);
		if (alert->Go() != 0)
			return _InstallationError(B_CANCELED);
	}

	_SetStatusMessage(B_TRANSLATE("Copying files" B_UTF8_ELLIPSIS));

	// Use rsync to copy the running system to the target partition
	// This handles all Linux paths correctly and preserves permissions
	BString rsyncCmd;
	rsyncCmd.SetToFormat(
		"rsync -aAXv --exclude=/proc --exclude=/sys --exclude=/dev "
		"--exclude=/run --exclude=/tmp --exclude=/var/cache "
		"--exclude=/var/tmp --exclude=/mnt --exclude=/media "
		"--exclude=/lost+found "
		"/ \"%s/\" > /dev/null 2>&1",
		targetDirectory.Path());

	int rsyncResult = system(rsyncCmd.String());
	if (rsyncResult != 0) {
		_SetStatusMessage(B_TRANSLATE("Error copying files to target partition."));
		return _InstallationError(B_ERROR);
	}

	// Get the partition's UUID and filesystem type for fstab and GRUB
	BString partitionUUID;
	BPath partitionPath;
	partition->GetPath(&partitionPath);

	BString blkidCmd;
	blkidCmd.SetToFormat("blkid -s UUID -o value %s 2>/dev/null",
		partitionPath.Path());
	FILE* fp = popen(blkidCmd.String(), "r");
	if (fp != NULL) {
		char uuidBuf[256];
		if (fgets(uuidBuf, sizeof(uuidBuf), fp) != NULL) {
			// Strip trailing newline
			size_t len = strlen(uuidBuf);
			if (len > 0 && uuidBuf[len - 1] == '\n')
				uuidBuf[len - 1] = '\0';
			partitionUUID = uuidBuf;
		}
		pclose(fp);
	}

	// Determine filesystem type name for fstab
	const char* fsType = "auto";
	const char* contentType = partition->ContentType();
	if (contentType != NULL) {
		if (strcmp(contentType, "Ext2") == 0)
			fsType = "ext4";
		else if (strcmp(contentType, "XFS") == 0)
			fsType = "xfs";
		else if (strcmp(contentType, "Btrfs") == 0)
			fsType = "btrfs";
	}

	// Generate /etc/fstab
	_SetStatusMessage(B_TRANSLATE("Generating system configuration" B_UTF8_ELLIPSIS));
	err = _GenerateFstab(targetDirectory, partitionUUID.String(), fsType);
	if (err != B_OK)
		return _InstallationError(err);

	// Configure system (hostname, machine-id)
	err = _ConfigureSystem(targetDirectory);
	if (err != B_OK)
		return _InstallationError(err);

	// Install GRUB bootloader
	_SetStatusMessage(B_TRANSLATE("Installing GRUB bootloader" B_UTF8_ELLIPSIS));

	// Find the parent disk device for GRUB install
	BPath diskPath;
	BPartition* parentPartition = partition->Parent();
	if (parentPartition != NULL) {
		parentPartition->GetPath(&diskPath);
	} else {
		// The partition is on the device itself
		diskPath = partitionPath;
	}

	err = _InstallGRUB(targetDirectory, diskPath.Path());
	if (err != B_OK) {
		_SetStatusMessage(B_TRANSLATE("GRUB installation failed."));
		return _InstallationError(err);
	}

	_SetStatusMessage(B_TRANSLATE("Installation completed."));
	fOwner.SendMessage(MSG_INSTALL_FINISHED);
	return B_OK;
}


status_t
WorkerThread::_GenerateFstab(const BPath& targetPath, const char* rootUUID,
	const char* rootFSType)
{
	BPath fstabPath(targetPath.Path(), "etc/fstab");
	BFile file;
	status_t err = file.SetTo(fstabPath.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK)
		return err;

	BString fstab;
	if (rootUUID != NULL && strlen(rootUUID) > 0) {
		fstab.SetToFormat("# /etc/fstab: static file system information.\n"
			"UUID=%s / %s defaults 0 1\n"
			"tmpfs /tmp tmpfs defaults,nosuid,nodev 0 0\n",
			rootUUID, rootFSType);
	} else {
		fstab.SetToFormat("# /etc/fstab: static file system information.\n"
			"/ / %s defaults 0 1\n"
			"tmpfs /tmp tmpfs defaults,nosuid,nodev 0 0\n",
			rootFSType);
	}

	ssize_t written = file.Write(fstab.String(), fstab.Length());
	return written == fstab.Length() ? B_OK : B_ERROR;
}


status_t
WorkerThread::_InstallGRUB(const BPath& targetPath, const char* diskDevice)
{
	// Detect kernel version
	BString kernelVersion;
	DIR* bootDir = opendir("/boot");
	if (bootDir != NULL) {
		struct dirent* entry;
		while ((entry = readdir(bootDir)) != NULL) {
			if (strncmp(entry->d_name, "vmlinuz-", 8) == 0) {
				kernelVersion = entry->d_name + 8;
				break;
			}
		}
		closedir(bootDir);
	}

	// Install GRUB to the disk
	BString grubInstallCmd;
	grubInstallCmd.SetToFormat(
		"grub-install --boot-directory=%s/boot %s > /dev/null 2>&1",
		targetPath.Path(), diskDevice);

	int result = system(grubInstallCmd.String());
	if (result != 0)
		return B_ERROR;

	// Generate grub.cfg
	BPath grubCfgPath(targetPath.Path(), "boot/grub/grub.cfg");
	BPath grubDir(targetPath.Path(), "boot/grub");
	create_directory(grubDir.Path(), 0755);

	BFile grubCfg;
	status_t err = grubCfg.SetTo(grubCfgPath.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (err != B_OK)
		return err;

	BString config;
	if (kernelVersion.Length() > 0) {
		config.SetToFormat(
			"set default=0\n"
			"set timeout=5\n"
			"\n"
			"menuentry \"VitruvianOS\" {\n"
			"	linux /boot/vmlinuz-%s root=UUID=$(blkid -s UUID -o value $(findmnt -n -o SOURCE /)) ro quiet splash\n"
			"	initrd /boot/initrd.img-%s\n"
			"}\n",
			kernelVersion.String(), kernelVersion.String());
	} else {
		config.SetToFormat(
			"set default=0\n"
			"set timeout=5\n"
			"\n"
			"menuentry \"VitruvianOS\" {\n"
			"	linux /boot/vmlinuz root=UUID=$(blkid -s UUID -o value $(findmnt -n -o SOURCE /)) ro quiet splash\n"
			"	initrd /boot/initrd.img\n"
			"}\n");
	}

	ssize_t written = grubCfg.Write(config.String(), config.Length());
	return written == config.Length() ? B_OK : B_ERROR;
}


status_t
WorkerThread::_ConfigureSystem(const BPath& targetPath)
{
	// Set hostname
	BPath hostnamePath(targetPath.Path(), "etc/hostname");
	BFile hostnameFile;
	if (hostnameFile.SetTo(hostnamePath.Path(),
			B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE) == B_OK) {
		hostnameFile.Write("vitruvian\n", 9);
	}

	// Reset machine-id for systemd
	BPath machineIdPath(targetPath.Path(), "etc/machine-id");
	BFile machineFile;
	if (machineFile.SetTo(machineIdPath.Path(),
			B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE) == B_OK) {
		machineFile.Write("\n", 1);
	}

	// Ensure essential directories exist
	BString mkdirCmd;
	mkdirCmd.SetToFormat(
		"mkdir -p %s/proc %s/sys %s/dev %s/run %s/tmp "
		"%s/var/cache %s/var/tmp 2>/dev/null",
		targetPath.Path(), targetPath.Path(), targetPath.Path(),
		targetPath.Path(), targetPath.Path(), targetPath.Path(),
		targetPath.Path());
	system(mkdirCmd.String());

	return B_OK;
}


status_t
WorkerThread::_InstallationError(status_t error)
{
	BMessage statusMessage(MSG_RESET);
	if (error == B_CANCELED)
		_SetStatusMessage(B_TRANSLATE("Installation canceled."));
	else
		statusMessage.AddInt32("error", error);
	fOwner.SendMessage(&statusMessage);
	return error;
}


void
WorkerThread::_SetStatusMessage(const char *status)
{
	BMessage msg(MSG_STATUS_MESSAGE);
	msg.AddString("status", status);
	fOwner.SendMessage(&msg);
}


// #pragma mark - SourceVisitor


SourceVisitor::SourceVisitor(BMenu* menu)
	: fMenu(menu)
{
}

bool
SourceVisitor::Visit(BDiskDevice* device)
{
	return Visit(device, 0);
}


bool
SourceVisitor::Visit(BPartition* partition, int32 level)
{
	if (!partition->IsMounted())
		return false;

	BPath mountPoint;
	if (partition->GetMountPoint(&mountPoint) != B_OK)
		return false;

	// Only accept the root partition as source
	if (strcmp(mountPoint.Path(), "/") != 0)
		return false;

	char size[20];
	string_for_size(partition->Size(), size, sizeof(size));

	char label[255];
	sprintf(label, "This system (%s)", size);

	PartitionMenuItem* item = new PartitionMenuItem("",
		label, label, new BMessage(SOURCE_PARTITION), partition->ID());
	item->SetMarked(true);
	fMenu->AddItem(item);
	return false;
}


// #pragma mark - TargetVisitor


TargetVisitor::TargetVisitor(BMenu* menu)
	: fMenu(menu)
{
}


bool
TargetVisitor::Visit(BDiskDevice* device)
{
	if (device->IsReadOnlyMedia())
		return false;
	return Visit(device, 0);
}


bool
TargetVisitor::Visit(BPartition* partition, int32 level)
{
	if (partition->ContentSize() < 2LL * 1024 * 1024 * 1024) {
		return false;
	}

	if (partition->CountChildren() > 0) {
		return false;
	}

	const char* contentType = partition->ContentType();
	if (contentType == NULL)
		return false;

	bool isLinuxFS = _IsLinuxFS(contentType);

	bool isRootPartition = false;
	if (partition->IsMounted()) {
		BPath mountPoint;
		partition->GetMountPoint(&mountPoint);
		isRootPartition = strcmp(mountPoint.Path(), "/") == 0;
	}

	bool isValidTarget = isLinuxFS && !isRootPartition
		&& !partition->IsReadOnly();

	BPath path;
	partition->GetPath(&path);

	char size[20];
	string_for_size(partition->Size(), size, sizeof(size));

	char label[255];
	if (isLinuxFS) {
		sprintf(label, "%s - %s [%s]", partition->ContentName().String(),
			size, path.Path());
	} else {
		sprintf(label, "%s - %s [%s] (%s)", partition->ContentName().String(),
			size, path.Path(), contentType);
	}

	char menuLabel[255];
	sprintf(menuLabel, "%s - %s", partition->ContentName().String(), size);

	PartitionMenuItem* item = new PartitionMenuItem(
		partition->ContentName().String(),
		label, menuLabel, new BMessage(TARGET_PARTITION), partition->ID());

	item->SetIsValidTarget(isValidTarget);
	fMenu->AddItem(item);
	return false;
}
