/*
 *  Copyright 2026, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 *
 *  Shared predicate functions for filtering Linux pseudo-filesystems.
 *  Used by: volume enumeration (fs_info.cpp), node monitor (node_monitor.cpp).
 *
 *  Two forms are provided:
 *    fs_mnttype_is_pseudo()  — string match on mnt_type from /proc/mounts
 *    fs_statfs_supports_watches() — magic number check via fstatfs(2)
 *
 *  Keep both lists in sync.
 */

#ifndef _LIBROOT_FS_TYPE_FILTER_H
#define _LIBROOT_FS_TYPE_FILTER_H

#include <string.h>


// Returns true if the mnt_type string names a pseudo / kernel-internal
// filesystem that should not be exposed as a Haiku volume and must not
// receive inotify/fsnotify marks.
static inline bool
fs_mnttype_is_pseudo(const char* mntType)
{
	static const char* const kPseudo[] = {
		"proc",
		"sysfs",
		"devtmpfs",
		"devpts",
		"cgroup",
		"cgroup2",
		"securityfs",
		"selinuxfs",
		"pstore",
		"efivarfs",
		"bpf",
		"tracefs",
		"debugfs",
		"configfs",
		"fusectl",
		"hugetlbfs",
		"mqueue",
		"autofs",
		"rpc_pipefs",
		"nfsd",
		"binfmt_misc",
		NULL
	};

	if (mntType == NULL)
		return false;

	for (int i = 0; kPseudo[i] != NULL; i++) {
		if (strcmp(mntType, kPseudo[i]) == 0)
			return true;
	}
	return false;
}


// Returns false if fstatfs(2) reveals a filesystem type that does not
// support inotify / fsnotify marks (installing marks on these causes
// kernel-side lockups or silent failures, observed on devtmpfs / /dev).
static inline bool
fs_statfs_supports_watches(long fType)
{
	switch ((unsigned long)fType) {
		case 0x1373:       // devtmpfs
		case 0x1cd1:       // devpts
		case 0x62656572:   // sysfs
		case 0x9fa0:       // proc
		case 0x27e0eb:     // cgroupfs v1
		case 0x63677270:   // cgroupfs v2
		case 0x64626720:   // debugfs
		case 0x74726163:   // tracefs
		case 0x65735543:   // fusectl (FUSE_CTL_SUPER_MAGIC)
		case 0xf97cff8c:   // selinuxfs
		case 0x73636673:   // securityfs
		case 0x42494e4d:   // binfmt_misc
		case 0x6165676c:   // pstore
		case 0xde5e81e4:   // efivarfs
		case 0xcafe4a11:   // bpf
		case 0x62656570:   // configfs
		case 0x958458f6:   // hugetlbfs
		case 0x19800202:   // mqueue
		case 0x0187:       // autofs
			return false;
		default:
			return true;
	}
}


#endif // _LIBROOT_FS_TYPE_FILTER_H
