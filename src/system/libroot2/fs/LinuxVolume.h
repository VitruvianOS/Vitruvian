/*
 *  Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#ifndef _LIBROOT2_LINUX_VOLUME
#define _LIBROOT2_LINUX_VOLUME

#include <fs_info.h>
#include <MountInfo.h>


namespace BKernelPrivate {


class LinuxVolume {
public:
	static status_t			FillVolumeInfo(const BPrivate::MountEntry& entry,
								fs_info* info);
	static status_t			FillVolumeInfoForDev(dev_t device, fs_info* info);

	static dev_t			GetNextVolume(int32* cookie);
};


}


#endif
