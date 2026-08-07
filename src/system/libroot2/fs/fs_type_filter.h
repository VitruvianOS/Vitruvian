/*
 *  Copyright 2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 *
 *  Thin compatibility shims over BPrivate::FsCaps. The canonical table
 *  (nexus/fs_caps.h) is shared verbatim with the kernel module.
 */

#ifndef _LIBROOT_FS_TYPE_FILTER_H
#define _LIBROOT_FS_TYPE_FILTER_H

#include "../../kernel/nexus/nexus/fs_caps.h"


static inline bool
fs_mnttype_is_pseudo(const char* mntType)
{
	return BPrivate::FsCaps::is_pseudo(mntType);
}


static inline bool
fs_statfs_supports_watches(long fType)
{
	const BPrivate::FsCaps::Entry* e =
		BPrivate::FsCaps::by_magic((uint32_t)fType);
	if (e == 0)
		return true;
	return (e->flags & BPrivate::FsCaps::PSEUDO) == 0;
}


#endif // _LIBROOT_FS_TYPE_FILTER_H
