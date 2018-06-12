
#include <fs_info.h>

#include <List.h>

#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include <errno_private.h>
#include <syscalls.h>

#include <mntent.h>

#include "volume/LinuxVolume.h"


BList mMountList = NULL;

void _DeallocateMountList()
{
	LinuxVolume* aMount;

	while (((aMount = mMountList.RemoveItem(1L))) != NULL)
	{
		delete aMount;
	}
}

void
_init_devices_list()
{
	mMountList = new BList();
	
	struct mntent*	aMountEntry;
	FILE*			fstab;

	fstab = setmntent (_PATH_MOUNTED, "r");

	while ((aMountEntry = getmntent(fstab)))
	{
		mMountList.AddItem(new LinuxVolume(aMountEntry));
	}

	if (endmntent(fstab) == 0)
	{
		int saved_errno = errno;

		_DeallocateMountList();

		errno = saved_errno;
	}
}


dev_t
_kern_next_device(int32 *_cookie)
{
	if (mMountList == NULL)
		_init_devices_list();

	// get next device
	status_t error = B_ERROR;
	LinuxVolume* aMount = (LinuxVolume*)(mMountList.ItemAt(_cookie++));

	if (aMount == NULL)
		return B_ERROR;
	else
		return aMount->Device();
}


status_t
_kern_write_fs_info(dev_t device, const struct fs_info* info, int mask)
{
	return B_ERROR;
}
