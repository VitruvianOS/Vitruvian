/*
 * Copyright 2018, Dario Casalinuovo.
 * Distributed under the terms of the MIT License.
 */

#include "LinuxVolume.h"

#include <fs_info.h>

#include <mntent.h>


LinuxVolume::LinuxVolume(struct mntent* mountEntry, dev_t id)
	:
	fName(mountEntry->mnt_fsname)
{
	//printf("%s\n", mountEntry->mnt_opts);

	fDevice = (dev_t)id;

	fCStatus = (fDevice == -1) ? -1 : 0;
}


LinuxVolume::~LinuxVolume()
{
}


status_t
LinuxVolume::InitCheck() const
{	
	return fCStatus;
}


dev_t
LinuxVolume::Device() const 
{
	return fDevice;
}


const char*
LinuxVolume::Name() const
{
	return fName.String();
}
