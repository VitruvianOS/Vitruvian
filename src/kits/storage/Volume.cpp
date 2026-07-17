/*
 * Copyright 2002-2009, Haiku Inc. All Rights Reserved.
 * Copyright 2026, Dario Casalinuovo <b.vitruvio@gmail.com>. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Tyler Dauwalder
 *		Ingo Weinhold
 *		Dario Casalinuovo
 */


#include <errno.h>
#include <string.h>

#include <Bitmap.h>
#include <Directory.h>
#include <fs_info.h>
#include <Node.h>
#include <OS.h>
#include <Path.h>
#include <String.h>
#include <Volume.h>

#include <storage_support.h>
#include <syscalls.h>
#include <MountInfo.h>

#include <fs_interface.h>


BVolume::BVolume()
	: fDevice((dev_t)-1),
	  fCStatus(B_NO_INIT)
{
	memset(_reserved, 0, sizeof(_reserved));
}


BVolume::BVolume(dev_t device)
	: fDevice((dev_t)-1),
	  fCStatus(B_NO_INIT)
{
	memset(_reserved, 0, sizeof(_reserved));
	SetTo(device);
}


BVolume::BVolume(const BVolume &volume)
	: fDevice(volume.fDevice),
	  fCStatus(volume.fCStatus)
{
	memset(_reserved, 0, sizeof(_reserved));
}


BVolume::~BVolume()
{
}


status_t
BVolume::InitCheck(void) const
{
	return fCStatus;
}


status_t
BVolume::SetTo(dev_t device)
{
	Unset();
	if (device == B_INVALID_DEV)
		return fCStatus = B_BAD_VALUE;

	if (device == get_vref_dev()) {
		fDevice = device;
		fCStatus = B_OK;
		return fCStatus;
	}

	fs_info info;
	if (fs_stat_dev(device, &info) != 0)
		return fCStatus = (errno != 0 ? -errno : B_BAD_VALUE);

	fDevice = device;
	fCStatus = B_OK;
	return fCStatus;
}


void
BVolume::Unset()
{
	fDevice = (dev_t)-1;
	fCStatus = B_NO_INIT;
}


dev_t
BVolume::Device() const
{
	return fDevice;
}


status_t
BVolume::GetRootDirectory(BDirectory *directory) const
{
	if (directory == NULL || InitCheck() != B_OK)
		return B_BAD_VALUE;

	fs_info info;
	if (fs_stat_dev(fDevice, &info) != 0)
		return -errno;

	int dirFd = _kern_open_entry_ref(info.dev, info.root, NULL,
		O_RDONLY | O_CLOEXEC, 0);
	if (dirFd < 0)
		return dirFd;

	node_ref ref(dirFd);
	close(dirFd);
	return directory->SetTo(&ref);
}


off_t
BVolume::Capacity() const
{
	fs_info info;
	if (InitCheck() != B_OK || fs_stat_dev(fDevice, &info) != 0)
		return B_BAD_VALUE;
	return (off_t)info.total_blocks * info.block_size;
}


off_t
BVolume::FreeBytes() const
{
	fs_info info;
	if (InitCheck() != B_OK || fs_stat_dev(fDevice, &info) != 0)
		return B_BAD_VALUE;
	return (off_t)info.free_blocks * info.block_size;
}


off_t
BVolume::BlockSize() const
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;
	fs_info info;
	if (fs_stat_dev(fDevice, &info) != 0)
		return -errno;
	return info.block_size;
}


status_t
BVolume::GetName(char *name) const
{
	if (name == NULL || InitCheck() != B_OK)
		return B_BAD_VALUE;
	fs_info info;
	if (fs_stat_dev(fDevice, &info) != 0)
		return -errno;
	strncpy(name, info.volume_name, B_FILE_NAME_LENGTH);
	return B_OK;
}


status_t
BVolume::SetName(const char *name)
{
	if (!name || InitCheck() != B_OK)
		return B_BAD_VALUE;
	if (strlen(name) >= B_FILE_NAME_LENGTH)
		return B_NAME_TOO_LONG;

	fs_info oldInfo;
	if (fs_stat_dev(fDevice, &oldInfo) != 0)
		return -errno;
	if (strcmp(name, oldInfo.volume_name) == 0)
		return B_OK;

	fs_info newInfo;
	strlcpy(newInfo.volume_name, name, sizeof(newInfo.volume_name));
	status_t error = _kern_write_fs_info(fDevice, &newInfo,
		FS_WRITE_FSINFO_NAME);
	if (error != B_OK)
		return error;

	// R5 implementation checks if an entry with the volume's old name
	// exists in the root directory and renames that entry, if it is indeed
	// the mount point of the volume (or a link referring to it). In all other
	// cases, nothing is done (even if the mount point is named like the
	// volume, but lives in a different directory).
	BPath entryPath;
	BEntry entry;
	BEntry traversedEntry;
	node_ref entryNodeRef;
	if (BPrivate::Storage::check_entry_name(name) == B_OK
		&& BPrivate::Storage::check_entry_name(oldInfo.volume_name) == B_OK
		&& entryPath.SetTo("/", oldInfo.volume_name) == B_OK
		&& entry.SetTo(entryPath.Path(), false) == B_OK
		&& entry.Exists()
		&& traversedEntry.SetTo(entryPath.Path(), true) == B_OK
		&& traversedEntry.GetNodeRef(&entryNodeRef) == B_OK
		&& entryNodeRef.dereference().dev() == fDevice
		&& entryNodeRef.dereference().ino() == oldInfo.root) {
		entry.Rename(name, false);
	}
	return error;
}


status_t
BVolume::GetIcon(BBitmap *icon, icon_size which) const
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;
	fs_info info;
	if (fs_stat_dev(fDevice, &info) != 0)
		return -errno;
	return get_device_icon(info.device_name, icon, which);
}


status_t
BVolume::GetIcon(uint8** _data, size_t* _size, type_code* _type) const
{
	if (InitCheck() != B_OK)
		return B_NO_INIT;
	fs_info info;
	if (fs_stat_dev(fDevice, &info) != 0)
		return -errno;
	return get_device_icon(info.device_name, _data, _size, _type);
}


bool
BVolume::IsRemovable() const
{
	fs_info info;
	return InitCheck() == B_OK && fs_stat_dev(fDevice, &info) == 0
		&& (info.flags & B_FS_IS_REMOVABLE);
}


bool
BVolume::IsReadOnly(void) const
{
	fs_info info;
	return InitCheck() == B_OK && fs_stat_dev(fDevice, &info) == 0
		&& (info.flags & B_FS_IS_READONLY);
}


bool
BVolume::IsPersistent(void) const
{
	fs_info info;
	return InitCheck() == B_OK && fs_stat_dev(fDevice, &info) == 0
		&& (info.flags & B_FS_IS_PERSISTENT);
}


bool
BVolume::IsShared(void) const
{
	fs_info info;
	return InitCheck() == B_OK && fs_stat_dev(fDevice, &info) == 0
		&& (info.flags & B_FS_IS_SHARED);
}


bool
BVolume::KnowsMime(void) const
{
	fs_info info;
	return InitCheck() == B_OK && fs_stat_dev(fDevice, &info) == 0
		&& (info.flags & B_FS_HAS_MIME);
}


bool
BVolume::KnowsAttr(void) const
{
	fs_info info;
	return InitCheck() == B_OK && fs_stat_dev(fDevice, &info) == 0
		&& (info.flags & B_FS_HAS_ATTR);
}


bool
BVolume::KnowsQuery(void) const
{
	fs_info info;
	return InitCheck() == B_OK && fs_stat_dev(fDevice, &info) == 0
		&& (info.flags & B_FS_HAS_QUERY);
}


status_t
BVolume::MountPoint(BPath* out) const
{
	if (out == NULL)
		return B_BAD_VALUE;
	if (InitCheck() != B_OK)
		return B_NO_INIT;
	if (IsVirtual())
		return B_ENTRY_NOT_FOUND;
	BPrivate::MountEntry e;
	if (!BPrivate::MountInfo::FindByDev(fDevice, &e)
		|| e.mount_point.IsEmpty())
		return B_ENTRY_NOT_FOUND;
	return out->SetTo(e.mount_point.String());
}


status_t
BVolume::DeviceNode(BString* out) const
{
	if (out == NULL)
		return B_BAD_VALUE;
	if (InitCheck() != B_OK)
		return B_NO_INIT;
	BPrivate::MountEntry e;
	if (BPrivate::MountInfo::FindByDev(fDevice, &e)) {
		*out = e.device_path;
	} else {
		fs_info info;
		if (fs_stat_dev(fDevice, &info) != 0)
			return -errno;
		*out = info.device_name;
	}
	return B_OK;
}


bool
BVolume::IsVirtual() const
{
	return fCStatus == B_OK && fDevice == get_vref_dev();
}


status_t
BVolume::Refresh()
{
	return InitCheck();
}


bool
BVolume::operator==(const BVolume &volume) const
{
	return ((InitCheck() != B_OK && volume.InitCheck() != B_OK)
			|| fDevice == volume.fDevice);
}


bool
BVolume::operator!=(const BVolume &volume) const
{
	return !(*this == volume);
}


BVolume&
BVolume::operator=(const BVolume &volume)
{
	if (&volume != this) {
		fDevice = volume.fDevice;
		fCStatus = volume.fCStatus;
	}
	return *this;
}


void BVolume::_TurnUpTheVolume1() {}
void BVolume::_TurnUpTheVolume2() {}
void BVolume::_TurnUpTheVolume3() {}
void BVolume::_TurnUpTheVolume4() {}
void BVolume::_TurnUpTheVolume5() {}
void BVolume::_TurnUpTheVolume6() {}
void BVolume::_TurnUpTheVolume7() {}
void BVolume::_TurnUpTheVolume8() {}
