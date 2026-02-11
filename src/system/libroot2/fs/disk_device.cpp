/*
 *  Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include "disk_device.h"

#include <dirent.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <syscalls.h>
#include <sys/ioctl.h>
#include <sys/mount.h>

#include <atomic>
#include <map>
#include <mutex>
#include <thread>

#include "KernelDebug.h"

#define MOUNT_OPTIONS_SIZE 256


struct DiskSystemInfo {
	disk_system_id	id;
	char			name[B_FILE_NAME_LENGTH];
	char			shortName[B_FILE_NAME_LENGTH];
	char			prettyName[B_FILE_NAME_LENGTH];
	uint32			flags;
	bool			isFileSystem;
	bool			isReadOnly;
};

static DiskSystemInfo gDiskSystems[] = {
	{ 0, "intel", "intel", "Intel Partition Map", B_DISK_SYSTEM_SUPPORTS_RESIZING | B_DISK_SYSTEM_SUPPORTS_MOVING | B_DISK_SYSTEM_SUPPORTS_SETTING_TYPE, false, false },
	{ 1, "gpt", "gpt", "GUID Partition Table", B_DISK_SYSTEM_SUPPORTS_RESIZING | B_DISK_SYSTEM_SUPPORTS_MOVING | B_DISK_SYSTEM_SUPPORTS_SETTING_TYPE, false, false },
	{ 2, "apple", "apple", "Apple Partition Map", B_DISK_SYSTEM_SUPPORTS_RESIZING | B_DISK_SYSTEM_SUPPORTS_MOVING | B_DISK_SYSTEM_SUPPORTS_SETTING_TYPE, false, false },

	{ 3, "bfs", "bfs", "Be File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 4, "ext2", "ext2", "EXT2 File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 5, "ext3", "ext3", "EXT3 File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 6, "ext4", "ext4", "EXT4 File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 7, "ntfs", "ntfs", "NTFS File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 8, "fat", "fat", "FAT File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 9, "exfat", "exfat", "exFAT File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 10, "xfs", "xfs", "XFS File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 11, "btrfs", "btrfs", "Btrfs File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },
	{ 12, "f2fs", "f2fs", "F2FS File System", B_DISK_SYSTEM_SUPPORTS_WRITING | B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, false },

	{ 13, "squashfs", "squashfs", "SquashFS File System", B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, true },
	{ 14, "iso9660", "iso9660", "ISO9660 File System", B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, true },
	{ 15, "udf", "udf", "UDF File System", B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, true },
	{ 16, "erofs", "erofs", "EROFS File System", B_DISK_SYSTEM_SUPPORTS_CONTENT_NAME, true, true },

	{ -1, "", "", "", 0, false, false }
};

static int gDiskSystemCount = -1;

static int
get_disk_system_count()
{
	if (gDiskSystemCount < 0) {
		gDiskSystemCount = 0;
		while (gDiskSystems[gDiskSystemCount].id >= 0)
			gDiskSystemCount++;
	}
	return gDiskSystemCount;
}


struct FileDiskDevice {
	partition_id	id;
	char			filePath[PATH_MAX];
	char			loopPath[PATH_MAX];
	int				loopNumber;
	bool			isReadOnly;
};

static std::mutex gFileDiskLock;
static std::map<partition_id, FileDiskDevice*> gFileDevices;


struct DiskIterator {
	DIR*			dir;
	char			currentDevice[NAME_MAX];
	int				partitionIndex;
	bool			deviceReturned;
};

static std::mutex gIteratorLock;
static std::map<int32, DiskIterator*> gIterators;
static int32 gNextIteratorCookie = 1;


static bool
should_include_device(const char* devName)
{
	if (devName[0] == '.')
		return false;

	if (strncmp(devName, "ram", 3) == 0)
		return false;

	if (strncmp(devName, "loop", 4) == 0) {
		char sysPath[PATH_MAX];
		snprintf(sysPath, sizeof(sysPath), "%s/%s/loop/backing_file", 
			SYS_BLOCK_PATH, devName);
		
		if (!BKernelPrivate::file_exists(sysPath)) {
			char devPath[PATH_MAX];
			snprintf(devPath, sizeof(devPath), "/dev/%s", devName);
			
			std::lock_guard<std::mutex> lock(gFileDiskLock);
			for (const auto& pair : gFileDevices) {
				if (strcmp(pair.second->loopPath, devPath) == 0)
					return true;
			}
			return false;
		}
	}

	if (strncmp(devName, "dm-", 3) == 0) {
		char sizePath[PATH_MAX];

		snprintf(sizePath, sizeof(sizePath), "%s/%s/size",
			SYS_BLOCK_PATH, devName);

		uint64_t size = 0;
		if (!read_sysfs_uint64(sizePath, &size) || size == 0)
			return false;
	}

	return true;
}


static void
fill_partition_info(const char* devPath, const char* sysPath, 
	user_partition_data* data, bool wholeDevice, int partIndex)
{
	memset(data, 0, sizeof(*data));

	data->id = make_partition_id(devPath);

	char sizePath[PATH_MAX];
	snprintf(sizePath, sizeof(sizePath), "%s/size", sysPath);
	uint64_t sectors = 0;
	read_sysfs_uint64(sizePath, &sectors);
	data->size = sectors * 512;
	data->content_size = data->size;

	if (!wholeDevice) {
		char startPath[PATH_MAX];
		snprintf(startPath, sizeof(startPath), "%s/start", sysPath);
		uint64_t startSector = 0;
		read_sysfs_uint64(startPath, &startSector);
		data->offset = startSector * 512;
	}

	data->block_size = get_block_size(devPath);
	data->index = partIndex;
	data->status = B_PARTITION_VALID;

	char namePath[PATH_MAX];
	snprintf(namePath, sizeof(namePath), "%s/partition", sysPath);
	FILE* f = fopen(namePath, "r");
	if (f) {
		char buf[256];
		if (fgets(buf, sizeof(buf), f)) {
			size_t l = strlen(buf);
			if (l && buf[l-1] == '\n') buf[l-1] = '\0';
			data->name = (buf[0] ? strdup(buf) : NULL);
		}
		fclose(f);
	}

	char typePath[PATH_MAX];
	snprintf(typePath, sizeof(typePath), "%s/type", sysPath);
	f = fopen(typePath, "r");
	if (f) {
		char buf[256];
		if (fgets(buf, sizeof(buf), f)) {
			size_t l = strlen(buf);
			if (l && buf[l-1] == '\n') buf[l-1] = '\0';
			data->type = (buf[0] ? strdup(buf) : NULL);
		}
		fclose(f);
	}

	char fsType[64] = {0};
	char mountPoint[PATH_MAX] = {0};

	bool mounted = BKernelPrivate::get_mount_info_by_device(devPath,
		mountPoint, sizeof(mountPoint), fsType, sizeof(fsType));

	if (mounted) {
		const char* bp = strrchr(mountPoint, '/');
		if (bp && bp[1] != '\0')
			data->content_name = strdup(bp + 1);
		else
			data->content_name = strdup(mountPoint);

		if (fsType[0])
			data->content_type = strdup(fsType);

		FILE* mounts = fopen("/proc/mounts", "r");
		if (mounts) {
			char line[1024];
			while (fgets(line, sizeof(line), mounts)) {
				char dev[256], mnt[256], ftype[64], opts[256];
				if (sscanf(line, "%255s %255s %63s %255s", dev, mnt,
						ftype, opts) == 4) {
					if (strcmp(dev, devPath) == 0 && strcmp(mnt, mountPoint) == 0) {
						data->parameters = (opts[0] ? strdup(opts) : NULL);
						break;
					}
				}
			}
			fclose(mounts);
		}
	} else {
		if (BKernelPrivate::detect_filesystem(devPath, fsType,
				sizeof(fsType))) {
			data->content_type = (fsType[0] ? strdup(fsType) : NULL);
		}
	}

	char mountPoint2[PATH_MAX] = {0};
	char fsType2[64] = {0};
		
	if (BKernelPrivate::get_mount_info_by_device(devPath, mountPoint2,
			sizeof(mountPoint2), fsType2, sizeof(fsType2))) {
		struct stat st;
		if (stat(mountPoint2, &st) == 0)
			data->volume = st.st_dev;

		if (BKernelPrivate::is_readonly_filesystem(fsType2))
			data->flags |= B_PARTITION_READ_ONLY;
	} else {
		data->volume = -1;

		if (BKernelPrivate::detect_filesystem(devPath, fsType2,
				sizeof(fsType2))) {
			if (BKernelPrivate::is_readonly_filesystem(fsType2))
				data->flags |= B_PARTITION_READ_ONLY;
		}
	}
}


static int
find_free_loop_device()
{
	int fd = open(DEV_LOOP_CONTROL, O_RDWR);
	if (fd < 0)
		return -1;

	int loop_num = ioctl(fd, LOOP_CTL_GET_FREE);
	close(fd);
	return loop_num;
}


static status_t
setup_loop_device(const char* filePath, int loopNum, bool readOnly, 
	char* loopPath, size_t loopPathSize)
{
	snprintf(loopPath, loopPathSize, "/dev/loop%d", loopNum);

	int fileFd = open(filePath, readOnly ? O_RDONLY : O_RDWR);
	if (fileFd < 0) {
		fileFd = open(filePath, O_RDONLY);

		if (fileFd < 0)
			return -errno;

		readOnly = true;
	}

	int loopFd = open(loopPath, readOnly ? O_RDONLY : O_RDWR);
	if (loopFd < 0) {
		close(fileFd);
		return -errno;
	}

	if (ioctl(loopFd, LOOP_SET_FD, fileFd) < 0) {
		int err = errno;
		close(loopFd);
		close(fileFd);
		return -err;
	}

	struct loop_info64 info;
	memset(&info, 0, sizeof(info));
	strlcpy((char*)info.lo_file_name, filePath, LO_NAME_SIZE);

	if (readOnly)
		info.lo_flags |= LO_FLAGS_READ_ONLY;

	info.lo_flags |= LO_FLAGS_AUTOCLEAR;

	ioctl(loopFd, LOOP_SET_STATUS64, &info);

	close(loopFd);
	close(fileFd);

	return B_OK;
}


static status_t
detach_loop_device(const char* loopPath)
{
	int loopFd = open(loopPath, O_RDONLY);
	if (loopFd < 0)
		return -errno;

	int ret = ioctl(loopFd, LOOP_CLR_FD);
	close(loopFd);

	return (ret < 0) ? -errno : B_OK;
}


extern "C" {


partition_id
_kern_get_next_disk_device_id(int32* cookie, size_t* neededSize)
{
	CALLED();

	if (cookie == nullptr)
		return B_BAD_VALUE;

	std::lock_guard<std::mutex> lock(gIteratorLock);

	DiskIterator* iter = nullptr;

	if (*cookie == 0) {
		iter = new DiskIterator();
		iter->dir = opendir(SYS_BLOCK_PATH);
		if (!iter->dir) {
			delete iter;
			return B_ERROR;
		}
		iter->currentDevice[0] = '\0';
		iter->partitionIndex = -1;
		iter->deviceReturned = false;

		*cookie = gNextIteratorCookie++;
		gIterators[*cookie] = iter;
	} else {
		auto it = gIterators.find(*cookie);
		if (it == gIterators.end())
			return B_BAD_VALUE;
		iter = it->second;
	}

	while (true) {
		if (iter->currentDevice[0] != '\0' && !iter->deviceReturned) {
			iter->deviceReturned = true;

			char devPath[PATH_MAX];
			snprintf(devPath, sizeof(devPath), "/dev/%s", iter->currentDevice);

			if (neededSize) {
				int partCount = BKernelPrivate::count_partitions(iter->currentDevice);
				*neededSize = sizeof(user_disk_device_data) +
							  partCount * sizeof(user_partition_data);
			}

			return make_partition_id(devPath);
		}

		struct dirent* entry = readdir(iter->dir);
		if (entry == nullptr) {
			closedir(iter->dir);
			gIterators.erase(*cookie);
			delete iter;
			*cookie = -1;
			return B_ENTRY_NOT_FOUND;
		}

		if (!should_include_device(entry->d_name))
			continue;

		char devPath[PATH_MAX];
		snprintf(devPath, sizeof(devPath), "/dev/%s", entry->d_name);
		if (!BKernelPrivate::file_exists(devPath))
			continue;

		struct udev* udev = BKernelPrivate::Team::GetUDev();
		if (!udev)
			continue;
		struct udev_device* udev_dev = udev_device_new_from_subsystem_sysname(udev, "block", entry->d_name);
		if (udev_dev) {
			struct udev_device* parent = udev_device_get_parent_with_subsystem_devtype(udev_dev, "block", "disk");
			if (parent) {
				udev_device_unref(udev_dev);
				udev_unref(udev);
				continue;
			}
			udev_device_unref(udev_dev);
		}
		udev_unref(udev);

		strlcpy(iter->currentDevice, entry->d_name, sizeof(iter->currentDevice));
		iter->deviceReturned = false;
	}
}


partition_id
_kern_find_disk_device(const char* filename, size_t* neededSize)
{
	CALLED();

	if (filename == nullptr)
		return B_BAD_VALUE;

	char realPath[PATH_MAX];
	if (realpath(filename, realPath) == nullptr)
		strlcpy(realPath, filename, sizeof(realPath));

	struct stat st;
	if (stat(realPath, &st) < 0)
		return B_ENTRY_NOT_FOUND;

	if (!S_ISBLK(st.st_mode))
		return B_BAD_VALUE;

	const char* devName = strrchr(realPath, '/');
	if (!devName)
		return B_BAD_VALUE;
	devName++;

	struct udev* udev = BKernelPrivate::Team::GetUDev();
	bool is_whole_disk = false;
	if (udev) {
		struct udev_device* udev_dev = udev_device_new_from_subsystem_sysname(udev, "block", devName);
		if (udev_dev) {
			struct udev_device* parent = udev_device_get_parent_with_subsystem_devtype(udev_dev, "block", "disk");
			if (!parent)
				is_whole_disk = true;
			else
				is_whole_disk = false;

			udev_device_unref(udev_dev);
		}
		udev_unref(udev);
	}

	char sysPath[PATH_MAX];
	snprintf(sysPath, sizeof(sysPath), "%s/%s", SYS_BLOCK_PATH, devName);

	if (!is_whole_disk && !BKernelPrivate::file_exists(sysPath))
		return B_ENTRY_NOT_FOUND;

	if (!is_whole_disk)
		return B_ENTRY_NOT_FOUND;

	if (neededSize) {
		int partCount = BKernelPrivate::count_partitions(devName);
		*neededSize = sizeof(user_disk_device_data) +
					  partCount * sizeof(user_partition_data);
	}

	return make_partition_id(realPath);
}


partition_id
_kern_find_partition(const char* filename, size_t* neededSize)
{
	CALLED();

	if (filename == nullptr)
		return B_BAD_VALUE;

	char realPath[PATH_MAX];
	if (realpath(filename, realPath) == nullptr)
		strlcpy(realPath, filename, sizeof(realPath));

	struct stat st;
	if (stat(realPath, &st) < 0)
		return B_ENTRY_NOT_FOUND;

	if (!S_ISBLK(st.st_mode))
		return B_BAD_VALUE;

	if (neededSize)
		*neededSize = sizeof(user_partition_data);

	return make_partition_id(realPath);
}


partition_id
_kern_find_file_disk_device(const char* filename, size_t* neededSize)
{
	CALLED();

	if (filename == nullptr)
		return B_BAD_VALUE;

	char realPath[PATH_MAX];
	if (realpath(filename, realPath) == nullptr)
		strlcpy(realPath, filename, sizeof(realPath));

	std::lock_guard<std::mutex> lock(gFileDiskLock);

	for (const auto& pair : gFileDevices) {
		if (strcmp(pair.second->filePath, realPath) == 0) {
			if (neededSize)
				*neededSize = sizeof(user_disk_device_data);
			return pair.first;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
_kern_get_disk_device_data(partition_id deviceID, bool deviceOnly,
	struct user_disk_device_data* buffer, size_t bufferSize, size_t* neededSize)
{
	CALLED();

	if (buffer == nullptr && neededSize == nullptr)
		return B_BAD_VALUE;

	DIR* devDir = opendir("/dev");
	if (!devDir)
		return B_ERROR;

	char devPath[PATH_MAX] = {0};
	char devName[NAME_MAX] = {0};
	struct dirent* entry;

	while ((entry = readdir(devDir)) != nullptr) {
		if (entry->d_name[0] == '.')
			continue;

		char testPath[PATH_MAX];
		snprintf(testPath, sizeof(testPath), "/dev/%s", entry->d_name);

		struct stat st;
		if (stat(testPath, &st) == 0 && S_ISBLK(st.st_mode)) {
			if ((partition_id)st.st_ino == deviceID) {
				strlcpy(devPath, testPath, sizeof(devPath));
				strlcpy(devName, entry->d_name, sizeof(devName));
				break;
			}
		}
	}
	closedir(devDir);

	if (devPath[0] == '\0')
		return B_ENTRY_NOT_FOUND;

	char sysPath[PATH_MAX];
	snprintf(sysPath, sizeof(sysPath), "%s/%s", SYS_BLOCK_PATH, devName);
	if (!BKernelPrivate::file_exists(sysPath))
		return B_ENTRY_NOT_FOUND;

	int partCount = deviceOnly ? 0 : BKernelPrivate::count_partitions(devName);
	size_t needed = sizeof(user_disk_device_data) + 
		partCount * sizeof(user_partition_data);

	if (neededSize)
		*neededSize = needed;

	if (buffer == nullptr || bufferSize < needed)
		return (buffer == nullptr) ? B_OK : B_BUFFER_OVERFLOW;

	memset(buffer, 0, bufferSize);

	buffer->device_flags = B_DISK_DEVICE_HAS_MEDIA;

	if (is_removable_device(sysPath))
		buffer->device_flags |= B_DISK_DEVICE_REMOVABLE;
	if (is_readonly_device(sysPath))
		buffer->device_flags |= B_DISK_DEVICE_READ_ONLY;

	if (strncmp(devName, "loop", 4) == 0)
		buffer->device_flags |= B_DISK_DEVICE_IS_FILE;

	fill_partition_info(devPath, sysPath, &buffer->device_partition_data, true, -1);
	buffer->device_partition_data.child_count = partCount;

	if (partCount > 0) {
		strlcpy(buffer->device_partition_data.content_type, "Intel Partition Map",
			sizeof(buffer->device_partition_data.content_type));
	}

	if (!deviceOnly && partCount > 0) {
		user_partition_data* partData = (user_partition_data*)
			((char*)buffer + sizeof(user_disk_device_data));

		for (int i = 0; i < partCount; i++) {
			char partName[NAME_MAX];
			if (BKernelPrivate::get_partition_name(devName, i, partName, sizeof(partName))) {
				char partDevPath[PATH_MAX];
				char partSysPath[PATH_MAX];
				snprintf(partDevPath, sizeof(partDevPath), "/dev/%s", partName);
				snprintf(partSysPath, sizeof(partSysPath), "%s/%s/%s", 
					SYS_BLOCK_PATH, devName, partName);

				fill_partition_info(partDevPath, partSysPath, &partData[i], false, i);
			}
		}
	}

	return B_OK;
}


partition_id
_kern_register_file_device(const char* filename)
{
	CALLED();

	if (filename == nullptr)
		return B_BAD_VALUE;

	if (!BKernelPrivate::is_regular_file(filename))
		return B_ENTRY_NOT_FOUND;

	char realPath[PATH_MAX];
	if (realpath(filename, realPath) == nullptr)
		return -errno;

	std::lock_guard<std::mutex> lock(gFileDiskLock);

	for (const auto& pair : gFileDevices) {
		if (strcmp(pair.second->filePath, realPath) == 0)
			return pair.first;
	}

	int loopNum = find_free_loop_device();
	if (loopNum < 0)
		return B_NO_MORE_FDS;

	FileDiskDevice* fileDev = new FileDiskDevice();
	strlcpy(fileDev->filePath, realPath, sizeof(fileDev->filePath));
	fileDev->loopNumber = loopNum;
	fileDev->isReadOnly = (access(realPath, W_OK) != 0);

	status_t status = setup_loop_device(realPath, loopNum, fileDev->isReadOnly,
		fileDev->loopPath, sizeof(fileDev->loopPath));
	if (status != B_OK) {
		delete fileDev;
		return status;
	}

	fileDev->id = make_partition_id(fileDev->loopPath);
	gFileDevices[fileDev->id] = fileDev;

	return fileDev->id;
}


status_t
_kern_unregister_file_device(partition_id deviceID, const char* filename)
{
	CALLED();

	std::lock_guard<std::mutex> lock(gFileDiskLock);

	FileDiskDevice* fileDev = nullptr;

	if (deviceID >= 0) {
		auto it = gFileDevices.find(deviceID);
		if (it != gFileDevices.end())
			fileDev = it->second;
	} else if (filename) {
		char realPath[PATH_MAX];
		if (realpath(filename, realPath) == nullptr)
			strlcpy(realPath, filename, sizeof(realPath));

		for (auto& pair : gFileDevices) {
			if (strcmp(pair.second->filePath, realPath) == 0) {
				fileDev = pair.second;
				break;
			}
		}
	}

	if (!fileDev)
		return B_ENTRY_NOT_FOUND;

	detach_loop_device(fileDev->loopPath);
	gFileDevices.erase(fileDev->id);

	delete fileDev;
	return B_OK;
}


status_t
_kern_get_file_disk_device_path(partition_id id, char* buffer, size_t bufferSize)
{
	CALLED();

	if (buffer == nullptr || bufferSize == 0)
		return B_BAD_VALUE;

	std::lock_guard<std::mutex> lock(gFileDiskLock);

	auto it = gFileDevices.find(id);
	if (it == gFileDevices.end())
		return B_ENTRY_NOT_FOUND;

	if (strlcpy(buffer, it->second->filePath, bufferSize) >= bufferSize)
		return B_BUFFER_OVERFLOW;

	return B_OK;
}


status_t
_kern_get_disk_system_info(disk_system_id id, struct user_disk_system_info* info)
{
	CALLED();

	if (info == nullptr)
		return B_BAD_VALUE;

	int count = get_disk_system_count();
	if (id < 0 || id >= count)
		return B_ENTRY_NOT_FOUND;

	info->id = gDiskSystems[id].id;
	strlcpy(info->name, gDiskSystems[id].name, sizeof(info->name));
	strlcpy(info->short_name, gDiskSystems[id].shortName, sizeof(info->short_name));
	strlcpy(info->pretty_name, gDiskSystems[id].prettyName, sizeof(info->pretty_name));
	info->flags = gDiskSystems[id].flags;

	return B_OK;
}


status_t
_kern_get_next_disk_system_info(int32* cookie, struct user_disk_system_info* info)
{
	CALLED();

	if (cookie == nullptr || info == nullptr)
		return B_BAD_VALUE;

	int count = get_disk_system_count();
	if (*cookie < 0 || *cookie >= count)
		return B_ENTRY_NOT_FOUND;

	int idx = *cookie;
	(*cookie)++;

	info->id = gDiskSystems[idx].id;
	strlcpy(info->name, gDiskSystems[idx].name, sizeof(info->name));
	strlcpy(info->short_name, gDiskSystems[idx].shortName, sizeof(info->short_name));
	strlcpy(info->pretty_name, gDiskSystems[idx].prettyName, sizeof(info->pretty_name));
	info->flags = gDiskSystems[idx].flags;

	return B_OK;
}


status_t
_kern_find_disk_system(const char* name, struct user_disk_system_info* info)
{
	CALLED();

	if (name == nullptr || info == nullptr)
		return B_BAD_VALUE;

	int count = get_disk_system_count();
	for (int i = 0; i < count; i++) {
		if (strcmp(gDiskSystems[i].name, name) == 0 ||
			strcmp(gDiskSystems[i].shortName, name) == 0) {
			info->id = gDiskSystems[i].id;
			strlcpy(info->name, gDiskSystems[i].name, sizeof(info->name));
			strlcpy(info->short_name, gDiskSystems[i].shortName, sizeof(info->short_name));
			strlcpy(info->pretty_name, gDiskSystems[i].prettyName, sizeof(info->pretty_name));
			info->flags = gDiskSystems[i].flags;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


// Stubs
status_t _kern_defragment_partition(partition_id, int32*) { return B_NOT_SUPPORTED; }
status_t _kern_repair_partition(partition_id, int32*, bool) { return B_NOT_SUPPORTED; }
status_t _kern_resize_partition(partition_id, int32*, partition_id, int32*, off_t, off_t) { return B_NOT_SUPPORTED; }
status_t _kern_move_partition(partition_id, int32*, partition_id, int32*, off_t, partition_id*, int32*, int32) { return B_NOT_SUPPORTED; }
status_t _kern_set_partition_name(partition_id, int32*, partition_id, int32*, const char*) { return B_NOT_SUPPORTED; }
status_t _kern_set_partition_content_name(partition_id, int32*, const char*) { return B_NOT_SUPPORTED; }
status_t _kern_set_partition_type(partition_id, int32*, partition_id, int32*, const char*) { return B_NOT_SUPPORTED; }
status_t _kern_set_partition_parameters(partition_id, int32*, partition_id, int32*, const char*) { return B_NOT_SUPPORTED; }
status_t _kern_set_partition_content_parameters(partition_id, int32*, const char*) { return B_NOT_SUPPORTED; }
status_t _kern_initialize_partition(partition_id, int32*, const char*, const char*, const char*) { return B_NOT_SUPPORTED; }
status_t _kern_uninitialize_partition(partition_id, int32*, partition_id, int32*) { return B_NOT_SUPPORTED; }
status_t _kern_create_child_partition(partition_id, int32*, off_t, off_t, const char*, const char*, const char*, partition_id*, int32*) { return B_NOT_SUPPORTED; }
status_t _kern_delete_child_partition(partition_id, int32*, partition_id, int32) { return B_NOT_SUPPORTED; }


status_t
_kern_get_partition_path(partition_id id, char* buffer, size_t bufferSize)
{
	CALLED();

	if (buffer == nullptr || bufferSize == 0)
		return B_BAD_VALUE;

	DIR* devDir = opendir("/dev");
	if (!devDir)
		return B_ERROR;

	struct dirent* entry;
	while ((entry = readdir(devDir)) != nullptr) {
		if (entry->d_name[0] == '.')
			continue;

		char devPath[PATH_MAX];
		snprintf(devPath, sizeof(devPath), "/dev/%s", entry->d_name);

		struct stat st;
		if (stat(devPath, &st) == 0 && S_ISBLK(st.st_mode)) {
			if ((partition_id)st.st_ino == id) {
				closedir(devDir);
				if (strlcpy(buffer, devPath, bufferSize) >= bufferSize)
					return B_BUFFER_OVERFLOW;
				return B_OK;
			}
		}
	}
	closedir(devDir);

	return B_ENTRY_NOT_FOUND;
}


status_t
_kern_get_partition_mount_point(partition_id id, char* buffer, size_t bufferSize)
{
	CALLED();

	if (buffer == nullptr || bufferSize == 0)
		return B_BAD_VALUE;

	char devPath[PATH_MAX];
	status_t status = _kern_get_partition_path(id, devPath, sizeof(devPath));
	if (status != B_OK)
		return status;

	if (!BKernelPrivate::get_mount_info_by_device(devPath, buffer, bufferSize,
			nullptr, 0)) {
		return B_ERROR;
	}

	return B_OK;
}


status_t
_kern_mount_partition(partition_id id, const char* mountPoint,
	const char* diskSystem, const char* parameters, uint32 flags)
{
	CALLED();

	char devPath[PATH_MAX];
	status_t status = _kern_get_partition_path(id, devPath, sizeof(devPath));
	if (status != B_OK)
		return status;

	char existingMount[PATH_MAX];
	if (BKernelPrivate::get_mount_info_by_device(devPath, existingMount,
			sizeof(existingMount), nullptr, 0)) {
		return B_BUSY;
	}

	const char* fsType = nullptr;
	char detectedFs[64] = {0};

	if (diskSystem && diskSystem[0] != '\0') {
		fsType = diskSystem;
	} else if (BKernelPrivate::detect_filesystem(devPath, detectedFs,
			sizeof(detectedFs))) {
		fsType = detectedFs;
	}

	if (!fsType || fsType[0] == '\0')
		return B_ERROR;

	char mountDir[PATH_MAX];
	if (mountPoint && mountPoint[0] != '\0') {
		strlcpy(mountDir, mountPoint, sizeof(mountDir));
	} else {
		const char* devName = strrchr(devPath, '/');
		devName = devName ? devName + 1 : devPath;
		snprintf(mountDir, sizeof(mountDir), "%s/%s", DEFAULT_MOUNT_BASE, devName);
	}

	if (mkdir(mountDir, 0755) < 0 && errno != EEXIST)
		return -errno;

	unsigned long mountFlags = 0;
	if (flags & B_MOUNT_READ_ONLY)
		mountFlags |= MS_RDONLY;
	if (BKernelPrivate::is_readonly_filesystem(fsType))
		mountFlags |= MS_RDONLY;

	char options[MOUNT_OPTIONS_SIZE] = {0};
	if (strcmp(fsType, "ntfs3") == 0 || strcmp(fsType, "ntfs") == 0 ||
		strcmp(fsType, "vfat") == 0 || strcmp(fsType, "exfat") == 0) {
		strlcpy(options, "uid=1000,gid=1000,dmask=022,fmask=133", sizeof(options));
	}

	const char* mountData = options[0] != '\0' ? options : nullptr;

	int ret = mount(devPath, mountDir, fsType, mountFlags, mountData);
	if (ret < 0) {
		status_t err = -errno;

		if (mountData)
			ret = mount(devPath, mountDir, fsType, mountFlags, nullptr);

		if (ret < 0) {
			rmdir(mountDir);
			return err;
		}
	}

	return B_OK;
}


status_t
_kern_unmount_partition(partition_id id, uint32 flags)
{
	CALLED();

	char devPath[PATH_MAX];
	status_t status = _kern_get_partition_path(id, devPath, sizeof(devPath));
	if (status != B_OK)
		return status;

	char mountPoint[PATH_MAX];
	if (!BKernelPrivate::get_mount_info_by_device(devPath, mountPoint, sizeof(mountPoint), nullptr, 0))
		return B_ERROR;

	int umountFlags = 0;
	if (flags & B_FORCE_UNMOUNT)
		umountFlags |= MNT_FORCE;

	int ret = umount2(mountPoint, umountFlags);
	if (ret < 0) {
		int err = errno;

		if (err == EBUSY && (flags & B_FORCE_UNMOUNT)) {
			ret = umount2(mountPoint, MNT_DETACH);
			if (ret < 0)
				return -errno;
		} else {
			return -err;
		}
	}

	rmdir(mountPoint);
	return B_OK;
}


} // extern "C"


__attribute__((destructor))
static void
disk_device_cleanup()
{
	std::lock_guard<std::mutex> iteratorLock(gIteratorLock);
	for (auto& pair : gIterators) {
		if (pair.second->dir)
			closedir(pair.second->dir);
		delete pair.second;
	}
	gIterators.clear();

	std::lock_guard<std::mutex> fileDiskLock(gFileDiskLock);
	for (auto& pair : gFileDevices) {
		if (pair.second->loopPath[0] != '\0')
			detach_loop_device(pair.second->loopPath);
		delete pair.second;
	}
	gFileDevices.clear();
}
