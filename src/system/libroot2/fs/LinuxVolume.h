/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT2_LINUX_VOLUME
#define _LIBROOT2_LINUX_VOLUME

#include <fs_info.h>
#include <mntent.h>


namespace BKernelPrivate {

class LinuxVolume {
public:
	static status_t	FillInfo(struct mntent* mountEntry, fs_info* info);
	static struct	mntent* FindVolume(dev_t volume);
	static dev_t	GetNext(int32* cookie);
};

}


#endif
