/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Copyright 2002-2008, Haiku Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Tyler Dauwalder
 *		Ingo Weinhold, bonefish@users.sf.net
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <errno.h>
#include <limits.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <AutoDeleter.h>
#include <Bitmap.h>
#include <Drivers.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_attr.h>
#include <fs_info.h>
#include <IconUtils.h>
#include <Mime.h>
#include <MimeType.h>
#include <Node.h>
#include <Path.h>
#include <RegistrarDefs.h>
#include <Roster.h>
#include <RosterPrivate.h>


using namespace BPrivate;


// Helper function that contacts the registrar for mime update calls
status_t
do_mime_update(int32 what, const char* path, int recursive,
	int synchronous, int force)
{
	BEntry root;
	entry_ref ref;

	status_t err = root.SetTo(path ? path : "/");
	if (!err)
		err = root.GetRef(&ref);
	if (!err) {
		BMessage msg(what);
		BMessage reply;
		status_t result;

		// Build and send the message, read the reply
		if (!err)
			err = msg.AddRef("entry", &ref);
		if (!err)
			err = msg.AddBool("recursive", recursive);
		if (!err)
			err = msg.AddBool("synchronous", synchronous);
		if (!err)
			err = msg.AddInt32("force", force);
		if (!err)
			err = BRoster::Private().SendTo(&msg, &reply, true);
		if (!err)
			err = reply.what == B_REG_RESULT ? B_OK : B_BAD_VALUE;
		if (!err)
			err = reply.FindInt32("result", &result);
		if (!err)
			err = result;
	}
	return err;
}


// Updates the MIME information (i.e MIME type) for one or more files.
int
update_mime_info(const char* path, int recursive, int synchronous, int force)
{
	// Force recursion when given a NULL path
	if (!path)
		recursive = true;

	return do_mime_update(B_REG_MIME_UPDATE_MIME_INFO, path, recursive,
		synchronous, force);
}


// Creates a MIME database entry for one or more applications.
status_t
create_app_meta_mime(const char* path, int recursive, int synchronous,
	int force)
{
	// Force recursion when given a NULL path
	if (!path)
		recursive = true;

	return do_mime_update(B_REG_MIME_CREATE_APP_META_MIME, path, recursive,
		synchronous, force);
}


// Haiku's disk drivers answer the B_GET_ICON_NAME / B_GET_VECTOR_ICON
// ioctls with an icon baked into the driver itself; there is no such
// driver layer under Linux. The functions below classify the device
// through sysfs instead, and reuse the icon of an existing, closely
// matching MIME type rather than drawing new artwork.


// Resolves "device" (e.g. "/dev/sda1") to the name of the whole disk it
// belongs to (e.g. "sda"), climbing from a partition to its disk when
// needed. Fails if the kernel has no block device by that name at all,
// which is the case for loopback-mounted images, tmpfs, and the live
// ISO's overlay root.
static status_t
get_linux_disk_name(const char* device, char* diskName, size_t diskNameSize)
{
	const char* base = strrchr(device, '/');
	base = base != NULL ? base + 1 : device;
	if (base[0] == '\0')
		return B_ENTRY_NOT_FOUND;

	char sysfsPath[PATH_MAX];
	snprintf(sysfsPath, sizeof(sysfsPath), "/sys/class/block/%s", base);

	char resolved[PATH_MAX];
	if (realpath(sysfsPath, resolved) == NULL)
		return B_ENTRY_NOT_FOUND;

	// A partition's canonical sysfs path nests under its whole disk's
	// directory (".../block/sda/sda1"); a whole disk sits directly under
	// "block" (".../block/sda"). Strip one path component to tell them
	// apart, then classify by the physical medium, not the slice.
	char* lastSlash = strrchr(resolved, '/');
	if (lastSlash == NULL)
		return B_ERROR;
	*lastSlash = '\0';

	char* parentSlash = strrchr(resolved, '/');
	const char* parentName = parentSlash != NULL ? parentSlash + 1 : resolved;

	if (strcmp(parentName, "block") == 0)
		strlcpy(diskName, lastSlash + 1, diskNameSize);
	else
		strlcpy(diskName, parentName, diskNameSize);

	return B_OK;
}


// Optical drives are always exposed as "sr*" under Linux regardless of
// their transport (ATAPI, USB, virtio); the SCSI peripheral device type
// (5 == TYPE_ROM) confirms it for anything named differently.
static bool
get_linux_disk_is_optical(const char* diskName)
{
	if (strncmp(diskName, "sr", 2) == 0)
		return true;

	char path[PATH_MAX];
	snprintf(path, sizeof(path), "/sys/block/%s/device/type", diskName);

	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;

	int type = -1;
	bool isRom = fscanf(file, "%d", &type) == 1 && type == 5;
	fclose(file);

	return isRom;
}


// Classifies "device" (a BVolume's fs_info.device_name) and hands back a
// stock icon for it. Only optical media are answered here: nothing else
// has an icon in the mime_db that beats the caller's own B_VOLUME
// fallback, and Tracker supplies the boot volume's icon itself.
static status_t
get_linux_stock_disk_icon(const char* device, uint8** _data, size_t* _size,
	type_code* _type)
{
	// A pseudo source such as "overlay" or "tmpfs" is not a path; do not
	// try to resolve it.
	if (device[0] != '/')
		return B_ENTRY_NOT_FOUND;

	char diskName[B_FILE_NAME_LENGTH];
	if (get_linux_disk_name(device, diskName, sizeof(diskName)) != B_OK)
		return B_ENTRY_NOT_FOUND;

	if (!get_linux_disk_is_optical(diskName))
		return B_ENTRY_NOT_FOUND;

	BMimeType mimeType("application/x-cd-image");

	status_t status = mimeType.InitCheck();
	if (status == B_OK)
		status = mimeType.GetIcon(_data, _size);
	if (status == B_OK)
		*_type = B_VECTOR_ICON_TYPE;

	return status;
}


// Retrieves an icon associated with a given device.
status_t
get_device_icon(const char* device, void* icon, int32 size)
{
	if (device == NULL || icon == NULL
		|| (size != B_LARGE_ICON && size != B_MINI_ICON))
		return B_BAD_VALUE;

	// ToDo: The mounted directories for volumes can also have META:X:STD_ICON
	// attributes. Should those attributes override the icon returned by
	// ioctl(,B_GET_ICON,)?
	bool haveIcon = false;
	int fd = open(device, O_RDONLY);
	if (fd >= 0) {
		device_icon iconData = {size, icon};
		haveIcon = ioctl(fd, B_GET_ICON, &iconData, sizeof(device_icon)) == 0;
		close(fd);
	}

	if (!haveIcon) {
		// The legacy icon was not available, or "device" could not even
		// be opened (e.g. a pseudo source like "overlay"); try the
		// vector icon, which also covers the Linux sysfs fallback.
		uint8* data;
		size_t dataSize;
		type_code type;
		status_t status = get_device_icon(device, &data, &dataSize, &type);
		if (status != B_OK)
			return status;

		BBitmap* icon32 = new(std::nothrow) BBitmap(
			BRect(0, 0, size - 1, size - 1), B_BITMAP_NO_SERVER_LINK,
			B_RGBA32);
		BBitmap* icon8 = new(std::nothrow) BBitmap(
			BRect(0, 0, size - 1, size - 1), B_BITMAP_NO_SERVER_LINK,
			B_CMAP8);

		ArrayDeleter<uint8> dataDeleter(data);
		ObjectDeleter<BBitmap> icon32Deleter(icon32);
		ObjectDeleter<BBitmap> icon8Deleter(icon8);

		if (icon32 == NULL || icon32->InitCheck() != B_OK || icon8 == NULL
			|| icon8->InitCheck() != B_OK) {
			return B_NO_MEMORY;
		}

		status = BIconUtils::GetVectorIcon(data, dataSize, icon32);
		if (status == B_OK)
			status = BIconUtils::ConvertToCMAP8(icon32, icon8);
		if (status == B_OK)
			memcpy(icon, icon8->Bits(), icon8->BitsLength());

		return status;
	}

	return B_OK;
}


// Retrieves an icon associated with a given device.
status_t
get_device_icon(const char* device, BBitmap* icon, icon_size which)
{
	// check parameters
	if (device == NULL || icon == NULL)
		return B_BAD_VALUE;

	uint8* data;
	size_t size;
	type_code type;
	status_t status = get_device_icon(device, &data, &size, &type);
	if (status == B_OK) {
		status = BIconUtils::GetVectorIcon(data, size, icon);
		delete[] data;
		return status;
	}

	// Vector icon was not available, try old one

	BRect rect;
	if (which == B_MINI_ICON)
		rect.Set(0, 0, 15, 15);
	else if (which == B_LARGE_ICON)
		rect.Set(0, 0, 31, 31);

	BBitmap* bitmap = icon;
	int32 iconSize = which;

	if (icon->ColorSpace() != B_CMAP8
		|| (which != B_MINI_ICON && which != B_LARGE_ICON)) {
		if (which < B_LARGE_ICON)
			iconSize = B_MINI_ICON;
		else
			iconSize = B_LARGE_ICON;

		bitmap = new(std::nothrow) BBitmap(
			BRect(0, 0, iconSize - 1, iconSize -1), B_BITMAP_NO_SERVER_LINK,
			B_CMAP8);
		if (bitmap == NULL || bitmap->InitCheck() != B_OK) {
			delete bitmap;
			return B_NO_MEMORY;
		}
	}

	// get the icon, convert temporary data into bitmap if necessary
	status = get_device_icon(device, bitmap->Bits(), iconSize);
	if (status == B_OK && icon != bitmap)
		status = BIconUtils::ConvertFromCMAP8(bitmap, icon);

	if (icon != bitmap)
		delete bitmap;

	return status;
}


status_t
get_device_icon(const char* device, uint8** _data, size_t* _size,
	type_code* _type)
{
	if (device == NULL || _data == NULL || _size == NULL || _type == NULL)
		return B_BAD_VALUE;

	int fd = open(device, O_RDONLY);
	if (fd < 0) {
		// Not an openable path at all (e.g. "overlay", "tmpfs", the
		// mountinfo source of a live ISO's root); classify from the
		// string alone instead of failing outright.
		return get_linux_stock_disk_icon(device, _data, _size, _type);
	}

	// Try to get the icon by name first

	char name[B_FILE_NAME_LENGTH];
	if (ioctl(fd, B_GET_ICON_NAME, name, sizeof(name)) >= 0) {
		status_t status = get_named_icon(name, _data, _size, _type);
		if (status == B_OK) {
			close(fd);
			return B_OK;
		}
	}

	// Getting the named icon failed, try vector icon next

	// NOTE: The actual icon size is unknown as of yet. After the first call
	// to B_GET_VECTOR_ICON, the actual size is known and the final buffer
	// is allocated with the correct size. If the buffer needed to be
	// larger, then the temporary buffer above will not yet contain the
	// valid icon data. In that case, a second call to B_GET_VECTOR_ICON
	// retrieves it into the final buffer.
	uint8 data[8192];
	device_icon iconData = {sizeof(data), data};
	status_t status = ioctl(fd, B_GET_VECTOR_ICON, &iconData,
		sizeof(device_icon));
	if (status != 0)
		status = errno;

	if (status == B_OK) {
		*_data = new(std::nothrow) uint8[iconData.icon_size];
		if (*_data == NULL)
			status = B_NO_MEMORY;
	}

	if (status == B_OK) {
		if (iconData.icon_size > (int32)sizeof(data)) {
			// the stack buffer does not contain the data, see NOTE above
			iconData.icon_data = *_data;
			status = ioctl(fd, B_GET_VECTOR_ICON, &iconData,
				sizeof(device_icon));
			if (status != 0)
				status = errno;
		} else
			memcpy(*_data, data, iconData.icon_size);

		*_size = iconData.icon_size;
		*_type = B_VECTOR_ICON_TYPE;
	}

	// TODO: also support getting the old icon?
	close(fd);

	if (status != B_OK) {
		// A real device node, but no Haiku disk driver behind it to
		// answer these ioctls (there is only a Linux block device here);
		// fall back to sysfs classification.
		status = get_linux_stock_disk_icon(device, _data, _size, _type);
	}

	return status;
}


status_t
get_named_icon(const char* name, BBitmap* icon, icon_size which)
{
	// check parameters
	if (name == NULL || icon == NULL)
		return B_BAD_VALUE;

	BRect rect;
	if (which == B_MINI_ICON)
		rect.Set(0, 0, 15, 15);
	else if (which == B_LARGE_ICON)
		rect.Set(0, 0, 31, 31);
	else
		return B_BAD_VALUE;

	if (icon->Bounds() != rect)
		return B_BAD_VALUE;

	uint8* data;
	size_t size;
	type_code type;
	status_t status = get_named_icon(name, &data, &size, &type);
	if (status == B_OK) {
		status = BIconUtils::GetVectorIcon(data, size, icon);
		delete[] data;
	}

	return status;
}


status_t
get_named_icon(const char* name, uint8** _data, size_t* _size, type_code* _type)
{
	if (name == NULL || _data == NULL || _size == NULL || _type == NULL)
		return B_BAD_VALUE;

	directory_which kWhich[] = {
		B_USER_NONPACKAGED_DATA_DIRECTORY,
		B_USER_DATA_DIRECTORY,
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY,
	};

	status_t status = B_ENTRY_NOT_FOUND;
	BFile file;
	off_t size;

	for (uint32 i = 0; i < sizeof(kWhich) / sizeof(kWhich[0]); i++) {
		BPath path;
		if (find_directory(kWhich[i], &path) != B_OK)
			continue;

		path.Append("icons");
		path.Append(name);

		status = file.SetTo(path.Path(), B_READ_ONLY);
		if (status == B_OK) {
			status = file.GetSize(&size);
			if (size > 1024 * 1024)
				status = B_ERROR;
		}
		if (status == B_OK)
			break;
	}

	if (status != B_OK)
		return status;

	*_data = new(std::nothrow) uint8[size];
	if (*_data == NULL)
		return B_NO_MEMORY;

	if (file.Read(*_data, size) != size) {
		delete[] *_data;
		return B_ERROR;
	}

	*_size = size;
	*_type = B_VECTOR_ICON_TYPE;
		// TODO: for now

	return B_OK;
}
