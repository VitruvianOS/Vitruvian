/*
 * Copyright 2003-2008, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FS_VOLUME_H
#define	_FS_VOLUME_H

#include <OS.h>
#include <sys/types.h>

/* mount flags */
#define B_MOUNT_READ_ONLY		1
#define B_MOUNT_VIRTUAL_DEVICE	2

/* unmount flags */
#define B_FORCE_UNMOUNT			1


#ifdef  __cplusplus
extern "C" {
#endif

/* `owner`: kernel-attested uid of the requesting session user (for idmapped /
   uid= mounts), or (uid_t)-1 for non-user mounts (auto-mount, CLI, urlwrapper).
   No C++ default argument here: this header is also included as C (src/bin/mount.c). */
extern dev_t	fs_mount_volume(const char *where, const char *device,
					const char *filesystem, uint32 flags,
					const char *parameters, uid_t owner);
extern status_t	fs_unmount_volume(const char *path, uint32 flags);

#ifdef  __cplusplus
}
#endif

#endif	/* _FS_VOLUME_H */
