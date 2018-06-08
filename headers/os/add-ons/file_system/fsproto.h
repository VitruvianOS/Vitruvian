/*
	Copyright 1999-2001, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.
*/

#ifndef _FSPROTO_H
#define _FSPROTO_H

#include <dirent.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include <OS.h>
#include <fs_attr.h>
#include <fs_info.h>
#include <BeBuild.h>

typedef dev_t		nspace_id;
typedef ino_t		vnode_id;

/*
 * PUBLIC PART OF THE FILE SYSTEM PROTOCOL
 */

#define		WSTAT_MODE		0x0001
#define		WSTAT_UID		0x0002
#define		WSTAT_GID		0x0004
#define		WSTAT_SIZE		0x0008
#define		WSTAT_ATIME		0x0010
#define		WSTAT_MTIME		0x0020
#define		WSTAT_CRTIME	0x0040

#define		WFSSTAT_NAME	0x0001

#define		B_ENTRY_CREATED		1
#define		B_ENTRY_REMOVED		2
#define		B_ENTRY_MOVED		3
#define		B_STAT_CHANGED		4
#define		B_ATTR_CHANGED		5
#define		B_DEVICE_MOUNTED	6
#define		B_DEVICE_UNMOUNTED	7

#define		B_STOP_WATCHING     0x0000
#define		B_WATCH_NAME		0x0001
#define		B_WATCH_STAT		0x0002
#define		B_WATCH_ATTR		0x0004
#define		B_WATCH_DIRECTORY	0x0008

#define		SELECT_READ			1
#define		SELECT_WRITE		2
#define 	SELECT_EXCEPTION	3

#define		B_CUR_FS_API_VERSION	2

#define		IOCTL_FILE_UNCACHED_IO	10000
#define		IOCTL_CREATE_TIME		10002
#define		IOCTL_MODIFIED_TIME		10003

struct attr_info;
struct index_info;

#endif
