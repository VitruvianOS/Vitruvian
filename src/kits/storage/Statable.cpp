/*
 * Copyright 2002-2014 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Tyler Dauwalder
 *		Ingo Weinhold, bonefish@users.sf.net
 */


#include <Statable.h>

#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <compat/sys/stat.h>

#include <Node.h>
#include <NodeMonitor.h>
#include <Volume.h>


class BStatable::Private {
public:
	Private(const BStatable* object)
		:
		fObject(object)
	{
	}

	status_t GetStatBeOS(struct stat_beos* stat)
	{
		return fObject->_GetStat(stat);
	}

private:
	const BStatable*	fObject;
};


#if __GNUC__ > 3
BStatable::~BStatable()
{
}
#endif


// Default: derive statx from the classic stat. Subclasses that can hit
// _kern_read_statx directly (Node/Entry/Directory) override this to get real
// btime + nsec timestamps.
status_t
BStatable::GetStatX(struct statx* stx) const
{
	if (stx == NULL)
		return B_BAD_VALUE;

	struct stat st;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
	status_t err = GetStat(&st);
#pragma GCC diagnostic pop
	if (err != B_OK)
		return err;

	memset(stx, 0, sizeof(*stx));
	stx->stx_mask = STATX_BASIC_STATS;
	stx->stx_blksize = st.st_blksize;
	stx->stx_nlink = st.st_nlink;
	stx->stx_uid = st.st_uid;
	stx->stx_gid = st.st_gid;
	stx->stx_mode = st.st_mode;
	stx->stx_ino = st.st_ino;
	stx->stx_size = st.st_size;
	stx->stx_blocks = st.st_blocks;
	stx->stx_atime.tv_sec = st.st_atim.tv_sec;
	stx->stx_atime.tv_nsec = st.st_atim.tv_nsec;
	stx->stx_mtime.tv_sec = st.st_mtim.tv_sec;
	stx->stx_mtime.tv_nsec = st.st_mtim.tv_nsec;
	stx->stx_ctime.tv_sec = st.st_ctim.tv_sec;
	stx->stx_ctime.tv_nsec = st.st_ctim.tv_nsec;
	stx->stx_dev_major = major(st.st_dev);
	stx->stx_dev_minor = minor(st.st_dev);
	stx->stx_rdev_major = major(st.st_rdev);
	stx->stx_rdev_minor = minor(st.st_rdev);
	return B_OK;
}


// Returns whether or not the current node is a file.
bool
BStatable::IsFile() const
{
	struct statx stx;
	if (GetStatX(&stx) == B_OK)
		return S_ISREG(stx.stx_mode);
	else
		return false;
}


// Returns whether or not the current node is a directory.
bool
BStatable::IsDirectory() const
{
	struct statx stx;
	if (GetStatX(&stx) == B_OK)
		return S_ISDIR(stx.stx_mode);
	else
		return false;
}


// Returns whether or not the current node is a symbolic link.
bool
BStatable::IsSymLink() const
{
	struct statx stx;
	if (GetStatX(&stx) == B_OK)
		return S_ISLNK(stx.stx_mode);
	else
		return false;
}


#ifndef __VOS__
// Fills out ref with the node_ref of the node.
status_t
BStatable::GetNodeRef(node_ref* ref) const
{
	status_t result = (ref ? B_OK : B_BAD_VALUE);
	struct stat stat;

	if (result == B_OK)
		result = GetStat(&stat);

	if (result == B_OK) {
		ref->device  = stat.st_dev;
		ref->node = stat.st_ino;
	}

	return result;
}
#endif


// Fills out the node's UID into owner.
status_t
BStatable::GetOwner(uid_t* owner) const
{
	if (owner == NULL)
		return B_BAD_VALUE;

	struct statx stx;
	status_t result = GetStatX(&stx);
	if (result == B_OK)
		*owner = stx.stx_uid;
	return result;
}


// Sets the node's UID to owner.
status_t
BStatable::SetOwner(uid_t owner)
{
	struct stat stat;
	stat.st_uid = owner;

	return set_stat(stat, B_STAT_UID);
}


// Fills out the node's GID into group.
status_t
BStatable::GetGroup(gid_t* group) const
{
	if (group == NULL)
		return B_BAD_VALUE;

	struct statx stx;
	status_t result = GetStatX(&stx);
	if (result == B_OK)
		*group = stx.stx_gid;
	return result;
}


// Sets the node's GID to group.
status_t
BStatable::SetGroup(gid_t group)
{
	struct stat stat;
	stat.st_gid = group;

	return set_stat(stat, B_STAT_GID);
}


// Fills out permissions with the node's permissions.
status_t
BStatable::GetPermissions(mode_t* permissions) const
{
	if (permissions == NULL)
		return B_BAD_VALUE;

	struct statx stx;
	status_t result = GetStatX(&stx);
	if (result == B_OK)
		*permissions = (stx.stx_mode & S_IUMSK);
	return result;
}


// Sets the node's permissions to permissions.
status_t
BStatable::SetPermissions(mode_t permissions)
{
	struct stat stat;
	// the FS should do the correct masking -- only the S_IUMSK part is
	// modifiable
	stat.st_mode = permissions;

	return set_stat(stat, B_STAT_MODE);
}


// Fills out the size of the node's data (not counting attributes) into size.
status_t
BStatable::GetSize(off_t* size) const
{
	if (size == NULL)
		return B_BAD_VALUE;

	struct statx stx;
	status_t result = GetStatX(&stx);
	if (result == B_OK)
		*size = stx.stx_size;
	return result;
}


// Fills out mtime with the last modification time of the node.
status_t
BStatable::GetModificationTime(time_t* mtime) const
{
	if (mtime == NULL)
		return B_BAD_VALUE;

	struct statx stx;
	status_t result = GetStatX(&stx);
	if (result == B_OK)
		*mtime = stx.stx_mtime.tv_sec;
	return result;
}


// Sets the node's last modification time to mtime.
status_t
BStatable::SetModificationTime(time_t mtime)
{
	struct stat stat;
	stat.st_mtime = mtime;

	return set_stat(stat, B_STAT_MODIFICATION_TIME);
}


// Fills out ctime with the creation time of the node
status_t
BStatable::GetCreationTime(time_t* ctime) const
{
	if (ctime == NULL)
		return B_BAD_VALUE;

#ifdef __VOS__
	struct statx stx;
	status_t result = GetStatX(&stx);
	if (result != B_OK)
		return result;
	// overlayfs / squashfs / older kernels may not report btime; fall back to
	// mtime so callers get a plausible timestamp instead of the epoch.
	if (stx.stx_mask & STATX_BTIME)
		*ctime = stx.stx_btime.tv_sec;
	else
		*ctime = stx.stx_mtime.tv_sec;
	return B_OK;
#else
	struct stat stat;
	status_t result = GetStat(&stat);
	if (result == B_OK)
		*ctime = stat.st_crtime;
	return result;
#endif
}


// Sets the node's creation time to ctime.
status_t
BStatable::SetCreationTime(time_t ctime)
{
	struct stat stat;
#ifndef __VOS__
	stat.st_crtime = ctime;
#else
	UNIMPLEMENTED();
#endif

	return set_stat(stat, B_STAT_CREATION_TIME);
}


// Fills out atime with the access time of the node.
status_t
BStatable::GetAccessTime(time_t* atime) const
{
	if (atime == NULL)
		return B_BAD_VALUE;

	struct statx stx;
	status_t result = GetStatX(&stx);
	if (result == B_OK)
		*atime = stx.stx_atime.tv_sec;
	return result;
}


// Sets the node's access time to atime.
status_t
BStatable::SetAccessTime(time_t atime)
{
	struct stat stat;
	stat.st_atime = atime;

	return set_stat(stat, B_STAT_ACCESS_TIME);
}


// Fills out vol with the the volume that the node lives on.
status_t
BStatable::GetVolume(BVolume* volume) const
{
	if (volume == NULL)
		return B_BAD_VALUE;

	struct statx stx;
	status_t result = GetStatX(&stx);
	if (result == B_OK)
		result = volume->SetTo(makedev(stx.stx_dev_major, stx.stx_dev_minor));
	return result;
}


// _OhSoStatable1() -> GetStat()
/*extern "C" status_t
#if __GNUC__ == 2
_OhSoStatable1__9BStatable(const BStatable* self, struct stat* stat)
#else
_ZN9BStatable14_OhSoStatable1Ev(const BStatable* self, struct stat* stat)
#endif
{
	// No Perform() method -- we have to use the old GetStat() method instead.
	struct stat_beos oldStat;
	status_t result = BStatable::Private(self).GetStatBeOS(&oldStat);
	if (result != B_OK)
		return result;

	convert_from_stat_beos(&oldStat, stat);

	return B_OK;
}

*/
void BStatable::_OhSoStatable2() {}
void BStatable::_OhSoStatable3() {}
