/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 *
 * Process-local snapshot of /proc/self/mountinfo (fallback: /proc/mounts).
 * Immutable, gen-counted. Caller-side reads do not block; writers swap
 * atomically. shared_ptr<const snapshot> must NOT cross process boundaries.
 */

#ifndef _MOUNT_INFO_H
#define _MOUNT_INFO_H

#include <SupportDefs.h>
#include <String.h>

#include <sys/types.h>
#include <stdint.h>
#include <memory>
#include <vector>


namespace BPrivate {


struct MountEntry {
	dev_t		dev;
	BString		mount_point;
	BString		device_path;
	BString		fs_type;
	BString		opts;
	uint32_t	flags;			// MS_RDONLY mirror (parsed from opts)
	int		mountinfo_id;	// /proc/self/mountinfo field 1
	int		parent_id;		// field 2
	bool		is_bind;		// shared/bind clone of another entry
};


typedef std::vector<MountEntry> MountEntryList;
typedef std::shared_ptr<const MountEntryList> MountSnapshot;


class MountInfo {
public:
	static MountSnapshot	Snapshot();
	static uint64_t			Generation();
	static void				Invalidate();

	static bool				FindByDev(dev_t dev, MountEntry* out);
	static bool				FindByDevicePath(const char* devPath,
								MountEntry* out);
	static bool				FindByMountPoint(const char* path,
								MountEntry* out);
	static bool				LongestPrefix(const char* path, MountEntry* out);
};


} // namespace BPrivate

#endif // _MOUNT_INFO_H
