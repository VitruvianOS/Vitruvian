/*
 * Copyright 2018, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 */

#include <fs_info.h>
#include <ObjectList.h>
#include <errno.h>

#include <mntent.h>

#include "syscalls.h"
#include "volume/LinuxVolume.h"


BObjectList<LinuxVolume> sVolumesList = NULL;


void
_init_devices_list()
{
	sVolumesList = BObjectList<LinuxVolume>(true);
	
	struct mntent* mtEntry = NULL;
	FILE* fstab = setmntent(_PATH_MOUNTED, "r");

	int32 i = 0;
	while ((mtEntry = getmntent(fstab)) != NULL)
		sVolumesList.AddItem(new LinuxVolume(mtEntry, i++));

	// TODO: Check errno on fail?
	if (endmntent(fstab) == 0)
		sVolumesList.MakeEmpty();
}


dev_t
_kern_next_device(int32 *cookie)
{
	if (sVolumesList.CountItems() == 0)
		_init_devices_list();

	if(*cookie >= sVolumesList.CountItems())
		return -1;

	printf("dev_t: %d\n", *cookie);

	LinuxVolume* volume = sVolumesList.ItemAt(*cookie);

	if (volume == NULL)
		return -1;

	(*cookie)++;

	return volume->Device();
}


status_t
_kern_write_fs_info(dev_t device, const struct fs_info* info, int mask)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_read_fs_info(dev_t device, fs_info *info)
{
	// The mntent structure is defined in <mntent.h> as follows:
	//
	//    struct mntent {
	//       char *mnt_fsname;   /* name of mounted filesystem */
	//        char *mnt_dir;      /* filesystem path prefix */
	//        char *mnt_type;     /* mount type (see mntent.h) */
	//       char *mnt_opts;     /* mount options (see mntent.h) */
	//        int   mnt_freq;     /* dump frequency in days */
	//        int   mnt_passno;   /* pass number on parallel fsck */
	//    };

	// TODO fill fs_info with data from the fs

	LinuxVolume* volume = sVolumesList.ItemAt((int32)device);
	if (volume == NULL)
		return B_ERROR;

	info->dev = device;
	strcpy(info->volume_name, volume->Name());
	strcpy(info->device_name, volume->DeviceName());

	return B_OK;
}
