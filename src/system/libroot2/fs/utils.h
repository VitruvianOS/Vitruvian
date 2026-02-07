/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef _VOS_FS_UTILS_H
#define _VOS_FS_UTILS_H

#include <OS.h>
#include <fs_volume.h>

#include <blkid/blkid.h>
#include <libudev.h>
#include <mntent.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/statvfs.h>


namespace BKernelPrivate {


#define SYS_BLOCK_PATH      "/sys/block"
#define PROC_MOUNTS         "/proc/mounts"
#define ETC_MTAB            "/etc/mtab"
#define ETC_FSTAB           "/etc/fstab"
#define DEV_LOOP_CONTROL    "/dev/loop-control"
#define DEFAULT_MOUNT_BASE  "/media"


static const char* const kLinuxFilesystems[] = {
	"ext2", "ext3", "ext4",
	"xfs", "btrfs", "f2fs",
	"vfat", "exfat", "msdos",
	"ntfs", "ntfs3", "fuseblk",
	"iso9660", "udf",
	"squashfs", "erofs",
	"hfsplus", "hfs",
	"jfs", "reiserfs",
	"tmpfs", "ramfs", "devtmpfs",
	"proc", "sysfs", "devpts", "cgroup", "cgroup2",
	"nfs", "nfs4", "cifs", "smbfs", "fuse",
	"overlay", "aufs",
	"bfs",
	NULL
};


typedef struct {
	const char* legacy;
	const char* linux_fs;
} FsNameMapping;


static const FsNameMapping kLegacyToLinuxFs[] = {
	{ "Be File System", "bfs" },
	{ "EXT2 File System", "ext2" },
	{ "EXT3 File System", "ext3" },
	{ "EXT4 File System", "ext4" },
	{ "NTFS File System", "ntfs3" },
	{ "FAT File System", "vfat" },
	{ "FAT32 File System", "vfat" },
	{ "FAT16 File System", "vfat" },
	{ "exFAT File System", "exfat" },
	{ "ISO9660 File System", "iso9660" },
	{ "UDF File System", "udf" },
	{ "HFS+ File System", "hfsplus" },
	{ "XFS File System", "xfs" },
	{ "Btrfs File System", "btrfs" },
	{ "SquashFS File System", "squashfs" },
	{ "F2FS File System", "f2fs" },
	{ "EROFS File System", "erofs" },
	{ "RAM File System", "tmpfs" },
	{ "fat", "vfat" },
	{ "ntfs", "ntfs3" },
	{ "hfs", "hfsplus" },
	{ "smbfs", "cifs" },
	{ NULL, NULL }
};


static const FsNameMapping kLinuxToHaikuFs[] = {
	{ "ext2", "EXT2 File System" },
	{ "ext3", "EXT3 File System" },
	{ "ext4", "EXT4 File System" },
	{ "xfs", "XFS File System" },
	{ "btrfs", "Btrfs File System" },
	{ "vfat", "FAT32 File System" },
	{ "fat", "FAT File System" },
	{ "msdos", "FAT File System" },
	{ "ntfs", "NTFS File System" },
	{ "ntfs3", "NTFS File System" },
	{ "ntfs-3g", "NTFS File System" },
	{ "fuseblk", "NTFS File System" },
	{ "iso9660", "ISO9660 File System" },
	{ "udf", "UDF File System" },
	{ "hfsplus", "HFS+ File System" },
	{ "exfat", "exFAT File System" },
	{ "bfs", "Be File System" },
	{ "squashfs", "SquashFS File System" },
	{ "f2fs", "F2FS File System" },
	{ "erofs", "EROFS File System" },
	{ "tmpfs", "RAM File System" },
	{ NULL, NULL }
};


static const char* const kReadOnlyFilesystems[] = {
	"squashfs", "iso9660", "udf", "erofs", NULL
};


static inline bool
file_exists(const char* path)
{
	struct stat st;
	return stat(path, &st) == 0;
}


static inline bool
is_regular_file(const char* path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}


static inline bool
is_block_device(const char* path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISBLK(st.st_mode);
}


static inline bool
is_linux_filesystem(const char* name)
{
	if (name == NULL)
		return false;

	for (int i = 0; kLinuxFilesystems[i] != NULL; i++) {
		if (strcmp(name, kLinuxFilesystems[i]) == 0)
			return true;
	}
	return false;
}


static inline bool
is_readonly_filesystem(const char* fsType)
{
	if (fsType == NULL)
		return false;

	for (int i = 0; kReadOnlyFilesystems[i] != NULL; i++) {
		if (strcasecmp(fsType, kReadOnlyFilesystems[i]) == 0)
			return true;
	}
	return false;
}


static inline const char*
translate_fs_to_linux(const char* fsName)
{
	if (fsName == NULL || fsName[0] == '\0')
		return NULL;

	if (is_linux_filesystem(fsName))
		return fsName;

	for (int i = 0; kLegacyToLinuxFs[i].legacy != NULL; i++) {
		if (strcasecmp(fsName, kLegacyToLinuxFs[i].legacy) == 0)
			return kLegacyToLinuxFs[i].linux_fs;
	}

	return fsName;
}


static inline const char*
translate_fs_to_haiku(const char* linuxFs)
{
	if (linuxFs == NULL || linuxFs[0] == '\0')
		return "";

	for (int i = 0; kLinuxToHaikuFs[i].linux_fs != NULL; i++) {
		if (strcasecmp(linuxFs, kLinuxToHaikuFs[i].linux_fs) == 0)
			return kLinuxToHaikuFs[i].legacy;
	}

	return linuxFs;
}


static inline bool
get_mount_info_by_device(const char* devPath, 
	char* mountPoint, size_t mpSize,
	char* fsType, size_t fsSize)
{
	FILE* mounts = setmntent(PROC_MOUNTS, "r");
	if (!mounts)
		mounts = setmntent(ETC_MTAB, "r");
	if (!mounts)
		return false;

	char realDev[PATH_MAX];
	if (!realpath(devPath, realDev))
		strlcpy(realDev, devPath, sizeof(realDev));

	struct mntent* entry;
	bool found = false;

	while ((entry = getmntent(mounts)) != NULL) {
		char realMntDev[PATH_MAX];
		if (!realpath(entry->mnt_fsname, realMntDev))
			strlcpy(realMntDev, entry->mnt_fsname, sizeof(realMntDev));

		if (strcmp(realDev, realMntDev) == 0) {
			if (mountPoint && mpSize > 0)
				strlcpy(mountPoint, entry->mnt_dir, mpSize);
			if (fsType && fsSize > 0)
				strlcpy(fsType, entry->mnt_type, fsSize);
			found = true;
			break;
		}
	}

	endmntent(mounts);
	return found;
}


static inline bool
is_mount_point(const char* path)
{
	FILE* mounts = setmntent(PROC_MOUNTS, "r");
	if (!mounts)
		return false;

	struct mntent* entry;
	while ((entry = getmntent(mounts)) != NULL) {
		if (strcmp(entry->mnt_dir, path) == 0) {
			endmntent(mounts);
			return true;
		}
	}
	endmntent(mounts);
	return false;
}


static inline bool
detect_filesystem(const char* devPath, char* fsType, size_t fsSize)
{
	if (get_mount_info_by_device(devPath, NULL, 0, fsType, fsSize))
		return true;

	int fd = open(devPath, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	blkid_probe pr = blkid_new_probe();
	if (!pr) {
		close(fd);
		return false;
	}

	if (blkid_probe_set_device(pr, fd, 0, 0) < 0) {
		blkid_free_probe(pr);
		close(fd);
		return false;
	}

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_TYPE);

	bool found = false;
	if (blkid_do_safeprobe(pr) == 0) {
		const char* type = NULL;
		if (blkid_probe_lookup_value(pr, "TYPE", &type, NULL) == 0 && type) {
			strlcpy(fsType, type, fsSize);
			found = true;
		}
	}

	blkid_free_probe(pr);
	close(fd);
	return found;
}


static inline bool
get_volume_label(const char* devPath, char* label, size_t labelSize)
{
	int fd = open(devPath, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	blkid_probe pr = blkid_new_probe();
	if (!pr) {
		close(fd);
		return false;
	}

	if (blkid_probe_set_device(pr, fd, 0, 0) < 0) {
		blkid_free_probe(pr);
		close(fd);
		return false;
	}

	blkid_probe_enable_superblocks(pr, 1);
	blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_LABEL);

	bool found = false;
	if (blkid_do_safeprobe(pr) == 0) {
		const char* value = NULL;
		if (blkid_probe_lookup_value(pr, "LABEL", &value, NULL) == 0 && value) {
			strlcpy(label, value, labelSize);
			found = true;
		}
	}

	blkid_free_probe(pr);
	close(fd);
	return found;
}


static inline bool
get_partition_table_type(const char* devPath, char* ptType, size_t ptSize)
{
	int fd = open(devPath, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	blkid_probe pr = blkid_new_probe();
	if (!pr) {
		close(fd);
		return false;
	}

	if (blkid_probe_set_device(pr, fd, 0, 0) < 0) {
		blkid_free_probe(pr);
		close(fd);
		return false;
	}

	blkid_probe_enable_partitions(pr, 1);
	blkid_probe_set_partitions_flags(pr, BLKID_PARTS_ENTRY_DETAILS);

	bool found = false;
	if (blkid_do_fullprobe(pr) == 0) {
		const char* value = NULL;
		if (blkid_probe_lookup_value(pr, "PTTYPE", &value, NULL) == 0 && value) {
			strlcpy(ptType, value, ptSize);
			found = true;
		}
	}

	blkid_free_probe(pr);
	close(fd);
	return found;
}


static inline bool
is_removable_device(const char* devName)
{
	struct udev* udev = udev_new();
	if (!udev)
		return false;

	struct udev_device* dev = udev_device_new_from_subsystem_sysname(
		udev, "block", devName);
	if (!dev) {
		udev_unref(udev);
		return false;
	}

	bool removable = false;
	const char* val = udev_device_get_sysattr_value(dev, "removable");
	if (val && val[0] == '1')
		removable = true;

	if (!removable) {
		struct udev_device* parent = udev_device_get_parent_with_subsystem_devtype(
			dev, "usb", "usb_device");
		if (parent)
			removable = true;
	}

	udev_device_unref(dev);
	udev_unref(udev);
	return removable;
}


static inline bool
is_readonly_device(const char* devName)
{
	struct udev* udev = udev_new();
	if (!udev)
		return false;

	struct udev_device* dev = udev_device_new_from_subsystem_sysname(
		udev, "block", devName);
	if (!dev) {
		udev_unref(udev);
		return false;
	}

	bool readonly = false;
	const char* val = udev_device_get_sysattr_value(dev, "ro");
	if (val && val[0] == '1')
		readonly = true;

	udev_device_unref(dev);
	udev_unref(udev);
	return readonly;
}


static inline bool
is_whole_disk(const char* devName)
{
	struct udev* udev = udev_new();
	if (!udev)
		return false;

	struct udev_device* dev = udev_device_new_from_subsystem_sysname(
		udev, "block", devName);
	if (!dev) {
		udev_unref(udev);
		return false;
	}

	const char* partnum = udev_device_get_sysattr_value(dev, "partition");
	bool isDisk = (partnum == NULL);

	udev_device_unref(dev);
	udev_unref(udev);
	return isDisk;
}


static inline off_t
get_device_size(const char* devName)
{
	struct udev* udev = udev_new();
	if (!udev)
		return 0;

	struct udev_device* dev = udev_device_new_from_subsystem_sysname(
		udev, "block", devName);
	if (!dev) {
		udev_unref(udev);
		return 0;
	}

	off_t size = 0;
	const char* val = udev_device_get_sysattr_value(dev, "size");
	if (val)
		size = strtoull(val, NULL, 10) * 512;

	udev_device_unref(dev);
	udev_unref(udev);
	return size;
}


static inline int
count_partitions(const char* devName)
{
	struct udev* udev = udev_new();
	if (!udev)
		return 0;

	struct udev_enumerate* en = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(en, "block");
	udev_enumerate_add_match_parent(en, 
		udev_device_new_from_subsystem_sysname(udev, "block", devName));
	udev_enumerate_scan_devices(en);

	struct udev_list_entry* devices = udev_enumerate_get_list_entry(en);
	struct udev_list_entry* entry;
	int count = 0;

	udev_list_entry_foreach(entry, devices) {
		const char* path = udev_list_entry_get_name(entry);
		struct udev_device* dev = udev_device_new_from_syspath(udev, path);
		if (!dev)
			continue;

		const char* partnum = udev_device_get_sysattr_value(dev, "partition");
		if (partnum)
			count++;

		udev_device_unref(dev);
	}

	udev_enumerate_unref(en);
	udev_unref(udev);
	return count;
}


static inline bool
get_partition_name(const char* devName, int index, char* partName, size_t partNameSize)
{
	struct udev* udev = udev_new();
	if (!udev)
		return false;

	struct udev_device* parent = udev_device_new_from_subsystem_sysname(
		udev, "block", devName);
	if (!parent) {
		udev_unref(udev);
		return false;
	}

	struct udev_enumerate* en = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(en, "block");
	udev_enumerate_add_match_parent(en, parent);
	udev_enumerate_scan_devices(en);

	struct udev_list_entry* devices = udev_enumerate_get_list_entry(en);
	struct udev_list_entry* entry;
	int current = 0;
	bool found = false;

	udev_list_entry_foreach(entry, devices) {
		const char* path = udev_list_entry_get_name(entry);
		struct udev_device* dev = udev_device_new_from_syspath(udev, path);
		if (!dev)
			continue;

		const char* partnum = udev_device_get_sysattr_value(dev, "partition");
		if (partnum) {
			if (current == index) {
				const char* sysname = udev_device_get_sysname(dev);
				if (sysname) {
					strlcpy(partName, sysname, partNameSize);
					found = true;
				}
				udev_device_unref(dev);
				break;
			}
			current++;
		}
		udev_device_unref(dev);
	}

	udev_enumerate_unref(en);
	udev_device_unref(parent);
	udev_unref(udev);
	return found;
}


static inline void
build_mount_options(const char* fsType, const char* userOptions,
	char* options, size_t optionsSize)
{
	options[0] = '\0';

	if (userOptions && userOptions[0] != '\0')
		strlcpy(options, userOptions, optionsSize);

	if (strcmp(fsType, "ntfs3") == 0 || strcmp(fsType, "ntfs") == 0 ||
		strcmp(fsType, "vfat") == 0 || strcmp(fsType, "exfat") == 0) {
		if (options[0] != '\0')
			strlcat(options, ",", optionsSize);

		char uidgid[64];
		snprintf(uidgid, sizeof(uidgid), "uid=%d,gid=%d,dmask=022,fmask=133",
			getuid(), getgid());
		strlcat(options, uidgid, optionsSize);
	}
}


} // namespace BKernelPrivate


#endif // _VOS_FS_UTILS_H
