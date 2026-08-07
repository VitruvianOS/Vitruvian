/*
 * Copyright 2019-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

// Needed before any system header for the new mount API wrappers and
// unshare(CLONE_NEWUSER) used below.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <fs_volume.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fs/utils.h"
#include "KernelDebug.h"


// Builds a userns mapping the session owner <-> on-disk root (0); other
// on-disk uids fall to overflow/nobody (a known, accepted limitation).
// Returns a userns fd (CLOEXEC) or -errno.
static int
make_idmap_userns(uid_t owner, gid_t ownerGroup)
{
	int pair[2];
	if (pipe2(pair, O_CLOEXEC) < 0)
		return -errno;

	pid_t pid = fork();
	if (pid < 0) {
		int e = errno;
		close(pair[0]);
		close(pair[1]);
		return -e;
	}

	if (pid == 0) {
		// Must stay async-signal-safe (multithreaded parent): raw syscalls
		// only, no malloc/stdio.
		close(pair[0]);
		if (unshare(CLONE_NEWUSER) < 0) {
			unsigned char c = (unsigned char)errno;
			if (c == 0)
				c = EIO;
			if (write(pair[1], &c, 1) < 0)
				;
			_exit(127);
		}
		unsigned char c = 0;
		if (write(pair[1], &c, 1) != 1)
			_exit(127);
		close(pair[1]);
		// hold the namespace alive until the parent has opened its fd
		pause();
		_exit(0);
	}

	// parent
	close(pair[1]);
	unsigned char childErr = 0;
	ssize_t n = read(pair[0], &childErr, 1);
	close(pair[0]);
	if (n != 1 || childErr != 0) {
		int e = (n == 1 && childErr != 0) ? childErr : EIO;
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return -e;
	}

	// uid_map: <id_in_ns=owner> <id_on_parent=0> <count=1>
	char path[64];
	char buf[128];
	snprintf(path, sizeof(path), "/proc/%d/uid_map", pid);
	snprintf(buf, sizeof(buf), "%u %u 1\n", (unsigned)owner, 0u);
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		int e = errno;
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return -e;
	}
	if (write(fd, buf, strlen(buf)) < 0) {
		int e = errno;
		close(fd);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return -e;
	}
	close(fd);

	// gid_map requires "deny" on setgroups first when mapping anything.
	snprintf(path, sizeof(path), "/proc/%d/setgroups", pid);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd >= 0) {
		if (write(fd, "deny", 4) < 0)
			;
		close(fd);
	}

	snprintf(path, sizeof(path), "/proc/%d/gid_map", pid);
	snprintf(buf, sizeof(buf), "%u %u 1\n", (unsigned)ownerGroup, 0u);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		int e = errno;
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return -e;
	}
	if (write(fd, buf, strlen(buf)) < 0) {
		int e = errno;
		close(fd);
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return -e;
	}
	close(fd);

	// Grab the userns fd, then let the child go.
	snprintf(path, sizeof(path), "/proc/%d/ns/user", pid);
	int nsFd = open(path, O_RDONLY | O_CLOEXEC);
	int e = errno;
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	if (nsFd < 0)
		return -e;
	return nsFd;
}


// Feeds a comma-separated option string to fsconfig() one key at a time;
// bare keys become SET_FLAG, "k=v" become SET_STRING.
static status_t
apply_options_fsconfig(int fsfd, const char* options)
{
	if (options == NULL || options[0] == '\0')
		return B_OK;

	char buf[512];
	strlcpy(buf, options, sizeof(buf));

	char* save = NULL;
	for (char* tok = strtok_r(buf, ",", &save);
		tok != NULL;
		tok = strtok_r(NULL, ",", &save)) {
		if (tok[0] == '\0')
			continue;
		char* eq = strchr(tok, '=');
		if (eq != NULL) {
			*eq = '\0';
			if (fsconfig(fsfd, FSCONFIG_SET_STRING, tok, eq + 1, 0) < 0)
				return -errno;
		} else {
			if (fsconfig(fsfd, FSCONFIG_SET_FLAG, tok, NULL, 0) < 0)
				return -errno;
		}
	}
	return B_OK;
}


// Mount via the new mount API, attaching the idmap when wantIdmap is set.
// Returns B_OK (*_dev filled) or -errno, captured before cleanup can clobber it.
static status_t
mount_new_api(const char* fsType, const char* where, const char* device,
	const char* options, bool readonly, uid_t owner, gid_t ownerGroup,
	bool wantIdmap, dev_t* _dev)
{
	int fsfd = fsopen(fsType, FSOPEN_CLOEXEC);
	if (fsfd < 0)
		return -errno;

	if (device != NULL && device[0] != '\0') {
		if (fsconfig(fsfd, FSCONFIG_SET_STRING, "source", device, 0) < 0) {
			int e = errno;
			close(fsfd);
			return -e;
		}
	}

	status_t optError = apply_options_fsconfig(fsfd, options);
	if (optError != B_OK) {
		close(fsfd);
		return optError;
	}

	if (readonly) {
		if (fsconfig(fsfd, FSCONFIG_SET_FLAG, "ro", NULL, 0) < 0) {
			int e = errno;
			close(fsfd);
			return -e;
		}
	}

	if (fsconfig(fsfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) < 0) {
		int e = errno;
		close(fsfd);
		return -e;
	}

	int mntfd = fsmount(fsfd, FSMOUNT_CLOEXEC, 0);
	if (mntfd < 0) {
		int e = errno;
		close(fsfd);
		return -e;
	}
	close(fsfd);

	// Fail outright if the idmap userns can't be built, rather than silently
	// handing back a root-owned mount the session user can't use.
	struct mount_attr attr;
	memset(&attr, 0, sizeof(attr));
	attr.attr_set = MOUNT_ATTR_NOSUID | MOUNT_ATTR_NODEV;
	if (readonly)
		attr.attr_set |= MOUNT_ATTR_RDONLY;

	int usernsFd = -1;
	if (wantIdmap) {
		usernsFd = make_idmap_userns(owner, ownerGroup);
		if (usernsFd < 0) {
			// make_idmap_userns() already returns -errno.
			close(mntfd);
			return usernsFd;
		}
		attr.attr_set |= MOUNT_ATTR_IDMAP;
		attr.userns_fd = (unsigned long long)usernsFd;
	}

	if (mount_setattr(mntfd, "", AT_EMPTY_PATH, &attr, sizeof(attr)) < 0) {
		int e = errno;
		if (usernsFd >= 0)
			close(usernsFd);
		close(mntfd);
		return -e;
	}
	if (usernsFd >= 0)
		close(usernsFd);

	if (move_mount(mntfd, "", AT_FDCWD, where, MOVE_MOUNT_F_EMPTY_PATH) < 0) {
		int e = errno;
		close(mntfd);
		return -e;
	}
	close(mntfd);

	struct stat st;
	if (stat(where, &st) < 0)
		return -errno;
	*_dev = st.st_dev;
	return B_OK;
}


// Classic mount(2) fallback for "auto"/unknown-fs (the new mount API needs
// a concrete fs context). Returns -errno of the FIRST attempt on failure,
// the most descriptive one.
static status_t
mount_legacy(const char* where, const char* device, const char* fsType,
	uint32 flags, const char* parameters, dev_t* _dev)
{
	unsigned long mountFlags = 0;
	if (flags & B_MOUNT_READ_ONLY)
		mountFlags |= MS_RDONLY;
	if (BKernelPrivate::is_readonly_filesystem(fsType))
		mountFlags |= MS_RDONLY;
	mountFlags |= MS_NOSUID | MS_NODEV;

	char options[512];
	options[0] = '\0';
	if (parameters && parameters[0] != '\0')
		strlcpy(options, parameters, sizeof(options));
	const char* mountData = options[0] != '\0' ? options : NULL;
	const char* source = (device != NULL) ? device : "none";

	int ret = mount(source, where, fsType, mountFlags, mountData);
	int firstErr = (ret < 0) ? errno : 0;
	if (ret < 0) {
		if (strcmp(fsType, "ntfs3") == 0) {
			ret = mount(source, where, "ntfs", mountFlags, mountData);
			if (ret < 0)
				ret = mount(source, where, "fuseblk", mountFlags, mountData);
		}
		if (ret < 0 && mountData != NULL)
			ret = mount(source, where, fsType, mountFlags, NULL);
		if (ret < 0)
			return -firstErr;
	}

	struct stat st;
	if (stat(where, &st) < 0)
		return -errno;
	*_dev = st.st_dev;
	return B_OK;
}


// Single mount chokepoint for every interactive/user mount. `owner` is the
// kernel-attested session uid, or (uid_t)-1 with no per-user requester;
// a real owner gets an idmapped mount (or uid=/gid= for FAT/NTFS/exFAT).
dev_t
fs_mount_volume(const char* where, const char* device,
	const char* filesystem, uint32 flags, const char* parameters, uid_t owner)
{
	CALLED();

	// dev_t is unsigned, so failure is B_INVALID_DEV with the real code left
	// in errno for the caller to recover via `return -errno`.
	if (where == NULL || where[0] == '\0') {
		errno = EINVAL;
		return B_INVALID_DEV;
	}

	struct stat st;
	if (stat(where, &st) < 0)
		return B_INVALID_DEV;			// errno set by stat()

	if (BKernelPrivate::is_mount_point(where)) {
		errno = EBUSY;
		return B_INVALID_DEV;
	}

	// Resolve a concrete Linux fs context name. fsopen() takes a real fs, not
	// "auto"; a still-unknown type degrades to the classic mount(2) path.
	const char* fsType = NULL;
	char detectedFs[128] = {0};
	if (filesystem && filesystem[0] != '\0')
		fsType = BKernelPrivate::translate_fs_to_linux(filesystem);
	if (fsType == NULL && device != NULL) {
		if (BKernelPrivate::detect_filesystem(device, detectedFs,
				sizeof(detectedFs))) {
			fsType = detectedFs;
		}
	}
	if (fsType == NULL || strcmp(fsType, "auto") == 0) {
		dev_t dev = B_INVALID_DEV;
		status_t error = mount_legacy(where, device, "auto", flags, parameters,
			&dev);
		if (error != B_OK) {
			errno = -error;
			return B_INVALID_DEV;
		}
		return dev;
	}

	bool readonly = (flags & B_MOUNT_READ_ONLY)
		|| BKernelPrivate::is_readonly_filesystem(fsType);

	// Resolve the session user's primary group (idmap gid_map + FAT gid=).
	gid_t ownerGroup = (gid_t)-1;
	if (owner != (uid_t)-1) {
		struct passwd* pw = getpwuid(owner);
		if (pw != NULL)
			ownerGroup = pw->pw_gid;
	}

	char options[512];
	BKernelPrivate::build_mount_options(fsType, parameters, owner, ownerGroup,
		options, sizeof(options));

	// Candidate fs contexts: the resolved type, then the documented ntfs
	// fallback chain (ntfs3 -> ntfs -> fuseblk).
	const char* candidates[3];
	unsigned int nCand = 0;
	candidates[nCand++] = fsType;
	if (strcmp(fsType, "ntfs3") == 0) {
		candidates[nCand++] = "ntfs";
		candidates[nCand++] = "fuseblk";
	}

	// On total failure report the FIRST candidate's error; later fallback
	// candidates fail for derived reasons.
	status_t firstError = B_ERROR;
	for (unsigned int i = 0; i < nCand; i++) {
		bool wantIdmap = (owner != (uid_t)-1)
			&& BKernelPrivate::is_idmap_capable_filesystem(candidates[i]);
		dev_t dev = B_INVALID_DEV;
		status_t error = mount_new_api(candidates[i], where, device, options,
			readonly, owner, ownerGroup, wantIdmap, &dev);
		if (error == B_OK)
			return dev;
		if (firstError == B_ERROR)
			firstError = error;

		// Retry option-less on failure, but only when dropping options can't
		// silently lose ownership (a load-bearing uid=/gid= for non-idmap
		// FAT/exFAT/NTFS mounts).
		bool ownershipInOptions = (owner != (uid_t)-1) && !wantIdmap;
		if (options[0] != '\0' && !ownershipInOptions) {
			error = mount_new_api(candidates[i], where, device, "",
				readonly, owner, ownerGroup, wantIdmap, &dev);
			if (error == B_OK)
				return dev;
		}
	}

	errno = (firstError < 0) ? -firstError : EINVAL;
	return B_INVALID_DEV;
}


status_t
fs_unmount_volume(const char* path, uint32 flags)
{
	CALLED();

	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;

	int umountFlags = 0;
	if (flags & B_FORCE_UNMOUNT)
		umountFlags |= MNT_FORCE;

	int ret = umount2(path, umountFlags);
	if (ret < 0) {
		int err = errno;

		if (err == EBUSY && (flags & B_FORCE_UNMOUNT)) {
			ret = umount2(path, MNT_DETACH);
			if (ret < 0)
				return -errno;
		} else {
			return -err;
		}
	}

	return B_OK;
}
