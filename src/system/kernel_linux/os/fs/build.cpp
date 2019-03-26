#include <LinuxBuildCompatibility.h>
#include <NodeMonitor.h>
#include <syscalls.h>
#include <syscall_utils.h>

#include "fs_descriptors.h"

using namespace BPrivate;

#if defined(_HAIKU_BUILD_NO_FUTIMENS) || defined(_HAIKU_BUILD_NO_FUTIMENS)

template<typename File>
static int
utimes_helper(File& file, const struct timespec times[2])
{
	if (times == NULL)
		return file.SetTimes(NULL);

	timeval timeBuffer[2];
	timeBuffer[0].tv_sec = times[0].tv_sec;
	timeBuffer[0].tv_usec = times[0].tv_nsec / 1000;
	timeBuffer[1].tv_sec = times[1].tv_sec;
	timeBuffer[1].tv_usec = times[1].tv_nsec / 1000;

	if (times[0].tv_nsec == UTIME_OMIT || times[1].tv_nsec == UTIME_OMIT) {
		struct stat st;
		if (file.GetStat(st) != 0)
			return -1;

		if (times[0].tv_nsec == UTIME_OMIT && times[1].tv_nsec == UTIME_OMIT)
			return 0;

		if (times[0].tv_nsec == UTIME_OMIT) {
			timeBuffer[0].tv_sec = st.st_atimespec.tv_sec;
			timeBuffer[0].tv_usec = st.st_atimespec.tv_nsec / 1000;
		}

		if (times[1].tv_nsec == UTIME_OMIT) {
			timeBuffer[1].tv_sec = st.st_mtimespec.tv_sec;
			timeBuffer[1].tv_usec = st.st_mtimespec.tv_nsec / 1000;
		}
	}

	if (times[0].tv_nsec == UTIME_NOW || times[1].tv_nsec == UTIME_NOW) {
		timeval now;
		gettimeofday(&now, NULL);

		if (times[0].tv_nsec == UTIME_NOW)
			timeBuffer[0] = now;

		if (times[1].tv_nsec == UTIME_NOW)
			timeBuffer[1] = now;
	}

	return file.SetTimes(timeBuffer);	
}

#endif	// _HAIKU_BUILD_NO_FUTIMENS || _HAIKU_BUILD_NO_FUTIMENS


#ifdef _HAIKU_BUILD_NO_FUTIMENS

struct FDFile {
	FDFile(int fd)
		:
		fFD(fd)
	{
	}

	int GetStat(struct stat& _st)
	{
		return fstat(fFD, &_st);
	}

	int SetTimes(const timeval times[2])
	{
		return futimes(fFD, times);
	}

private:
	int fFD;
};


int
futimens(int fd, const struct timespec times[2])
{
	FDFile file(fd);
	return utimes_helper(file, times);
}

#endif	// _HAIKU_BUILD_NO_FUTIMENS

#ifdef _HAIKU_BUILD_NO_UTIMENSAT

struct FDPathFile {
	FDPathFile(int fd, const char* path, int flag)
		:
		fFD(fd),
		fPath(path),
		fFlag(flag)
	{
	}

	int GetStat(struct stat& _st)
	{
		return fstatat(fFD, fPath, &_st, fFlag);
	}

	int SetTimes(const timeval times[2])
	{
		// TODO: fFlag (AT_SYMLINK_NOFOLLOW) is not supported here!
		return futimesat(fFD, fPath, times);
	}

private:
	int			fFD;
	const char*	fPath;
	int			fFlag;
};


int
utimensat(int fd, const char* path, const struct timespec times[2], int flag)
{
	FDPathFile file(fd, path, flag);
	return utimes_helper(file, times);
}

#endif	// _HAIKU_BUILD_NO_UTIMENSAT


// #pragma mark -


int
_haiku_build_fchmod(int fd, mode_t mode)
{
	return _haiku_build_fchmodat(fd, NULL, mode, AT_SYMLINK_NOFOLLOW);
}


int
_haiku_build_fchmodat(int fd, const char* path, mode_t mode, int flag)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return fchmodat(fd, path, mode, flag);

	struct stat st;
	st.st_mode = mode;

	RETURN_AND_SET_ERRNO(_kern_write_stat(fd, path,
		(flag & AT_SYMLINK_NOFOLLOW) == 0, &st, sizeof(st), B_STAT_MODE));
}


int
_haiku_build_fstat(int fd, struct stat* st)
{
	return _haiku_build_fstatat(fd, NULL, st, AT_SYMLINK_NOFOLLOW);
}


int
_haiku_build_fstatat(int fd, const char* path, struct stat* st, int flag)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return fstatat(fd, path, st, flag);

	RETURN_AND_SET_ERRNO(_kern_read_stat(fd, path,
		(flag & AT_SYMLINK_NOFOLLOW) == 0, st, sizeof(*st)));
}


int
_haiku_build_mkdirat(int fd, const char* path, mode_t mode)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return mkdirat(fd, path, mode);

	RETURN_AND_SET_ERRNO(_kern_create_dir(fd, path, mode));
}


int
_haiku_build_mkfifoat(int fd, const char* path, mode_t mode)
{
	return mkfifoat(fd, path, mode);

	// TODO: Handle non-system FDs.
}
/*

int
_haiku_build_utimensat(int fd, const char* path, const struct timespec times[2],
	int flag)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return utimensat(fd, path, times, flag);

	struct stat stat;
	status_t status;
	uint32 mask = 0;

	// Init the stat time fields to the current time, if at least one time is
	// supposed to be set to it.
	if (times == NULL || times[0].tv_nsec == UTIME_NOW
		|| times[1].tv_nsec == UTIME_NOW) {
		timeval now;
		gettimeofday(&now, NULL);
		HAIKU_HOST_STAT_ATIM(stat).tv_sec
			= HAIKU_HOST_STAT_MTIM(stat).tv_sec = now.tv_sec;
		HAIKU_HOST_STAT_ATIM(stat).tv_nsec
			= HAIKU_HOST_STAT_MTIM(stat).tv_nsec = now.tv_usec * 1000;
	}

	if (times != NULL) {
		// access time
		if (times[0].tv_nsec != UTIME_OMIT) {
			mask |= B_STAT_ACCESS_TIME;

			if (times[0].tv_nsec != UTIME_NOW) {
				if (times[0].tv_nsec < 0 || times[0].tv_nsec > 999999999)
					RETURN_AND_SET_ERRNO(EINVAL);
			}

			HAIKU_HOST_STAT_ATIM(stat) = times[0];
		}

		// modified time
		if (times[1].tv_nsec != UTIME_OMIT) {
			mask |= B_STAT_MODIFICATION_TIME;

			if (times[1].tv_nsec != UTIME_NOW) {
				if (times[1].tv_nsec < 0 || times[1].tv_nsec > 999999999)
					RETURN_AND_SET_ERRNO(EINVAL);
			}

			HAIKU_HOST_STAT_MTIM(stat) = times[1];
		}
	} else
		mask |= B_STAT_ACCESS_TIME | B_STAT_MODIFICATION_TIME;

	// set the times -- as per spec we even need to do this, if both have
	// UTIME_OMIT set
	status = _kern_write_stat(fd, path, (flag & AT_SYMLINK_NOFOLLOW) == 0,
		&stat, sizeof(struct stat), mask);

	RETURN_AND_SET_ERRNO(status);
}


int
_haiku_build_futimens(int fd, const struct timespec times[2])
{
	return _haiku_build_utimensat(fd, NULL, times, AT_SYMLINK_NOFOLLOW);
}
*/

int
_haiku_build_faccessat(int fd, const char* path, int accessMode, int flag)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return faccessat(fd, path, accessMode, flag);

	// stat the file
	struct stat st;
	status_t error = _kern_read_stat(fd, path, false, &st, sizeof(st));
	if (error != B_OK)
		RETURN_AND_SET_ERRNO(error);

	// get the current user
	uid_t uid = (flag & AT_EACCESS) != 0 ? geteuid() : getuid();

	int fileMode = 0;

	if (uid == 0) {
		// user is root
		// root has always read/write permission, but at least one of the
		// X bits must be set for execute permission
		fileMode = R_OK | W_OK;
		if ((st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0)
			fileMode |= X_OK;
	} else if (st.st_uid == uid) {
		// user is node owner
		if ((st.st_mode & S_IRUSR) != 0)
			fileMode |= R_OK;
		if ((st.st_mode & S_IWUSR) != 0)
			fileMode |= W_OK;
		if ((st.st_mode & S_IXUSR) != 0)
			fileMode |= X_OK;
	} else if (st.st_gid == ((flag & AT_EACCESS) != 0 ? getegid() : getgid())) {
		// user is in owning group
		if ((st.st_mode & S_IRGRP) != 0)
			fileMode |= R_OK;
		if ((st.st_mode & S_IWGRP) != 0)
			fileMode |= W_OK;
		if ((st.st_mode & S_IXGRP) != 0)
			fileMode |= X_OK;
	} else {
		// user is one of the others
		if ((st.st_mode & S_IROTH) != 0)
			fileMode |= R_OK;
		if ((st.st_mode & S_IWOTH) != 0)
			fileMode |= W_OK;
		if ((st.st_mode & S_IXOTH) != 0)
			fileMode |= X_OK;
	}

	if ((accessMode & ~fileMode) != 0)
		RETURN_AND_SET_ERRNO(EACCES);

	return 0;
}


int
_haiku_build_fchdir(int fd)
{
	if (is_unknown_or_system_descriptor(fd))
		return fchdir(fd);

	RETURN_AND_SET_ERRNO(B_FILE_ERROR);
}


int
_haiku_build_close(int fd)
{
	if (get_descriptor(fd) == NULL)
		return close(fd);

	RETURN_AND_SET_ERRNO(_kern_close(fd));
}


int
_haiku_build_dup(int fd)
{
	if (get_descriptor(fd) == NULL)
		return close(fd);

	RETURN_AND_SET_ERRNO(_kern_dup(fd));
}


int
_haiku_build_dup2(int fd1, int fd2)
{
	if (is_unknown_or_system_descriptor(fd1))
		return dup2(fd1, fd2);

	// TODO: Handle non-system FDs.
	RETURN_AND_SET_ERRNO(B_NOT_SUPPORTED);
}


int
_haiku_build_linkat(int toFD, const char* toPath, int pathFD, const char* path,
	int flag)
{
	return linkat(toFD, toPath, pathFD, path, flag);

	// TODO: Handle non-system FDs.
}


int
_haiku_build_unlinkat(int fd, const char* path, int flag)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return unlinkat(fd, path, flag);

	RETURN_AND_SET_ERRNO(_kern_unlink(fd, path));
}


ssize_t
_haiku_build_readlinkat(int fd, const char* path, char* buffer,
	size_t bufferSize)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return readlinkat(fd, path, buffer, bufferSize);

	status_t error = _kern_read_link(fd, path, buffer, &bufferSize);
	if (error != B_OK)
		RETURN_AND_SET_ERRNO(error);

	return bufferSize;
}


int
_haiku_build_symlinkat(const char* toPath, int fd, const char* symlinkPath)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return symlinkat(toPath, fd, symlinkPath);

	RETURN_AND_SET_ERRNO(_kern_create_symlink(fd, symlinkPath, toPath,
		S_IRWXU | S_IRWXG | S_IRWXO));
}


int
_haiku_build_ftruncate(int fd, off_t newSize)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return ftruncate(fd, newSize);

	struct stat st;
	st.st_size = newSize;

	RETURN_AND_SET_ERRNO(_kern_write_stat(fd, NULL, false, &st, sizeof(st),
		B_STAT_SIZE));
}


int
_haiku_build_fchown(int fd, uid_t owner, gid_t group)
{
	return _haiku_build_fchownat(fd, NULL, owner, group, AT_SYMLINK_NOFOLLOW);
}


int
_haiku_build_fchownat(int fd, const char* path, uid_t owner, gid_t group,
	int flag)
{
	if (fd >= 0 && fd != AT_FDCWD && get_descriptor(fd) == NULL)
		return fchownat(fd, path, owner, group, flag);

	struct stat st;
	st.st_uid = owner;
	st.st_gid = group;

	RETURN_AND_SET_ERRNO(_kern_write_stat(fd, path,
		(flag & AT_SYMLINK_NOFOLLOW) == 0, &st, sizeof(st),
		B_STAT_UID | B_STAT_GID));
}


int
_haiku_build_mknodat(int fd, const char* name, mode_t mode, dev_t dev)
{
	return mknodat(fd, name, mode, dev);

	// TODO: Handle non-system FDs.
}


int
_haiku_build_creat(const char* path, mode_t mode)
{
	return _haiku_build_open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}


int
_haiku_build_open(const char* path, int openMode, mode_t permissions)
{
	return _haiku_build_openat(AT_FDCWD, path, openMode, permissions);
}


int
_haiku_build_openat(int fd, const char* path, int openMode, mode_t permissions)
{
	// adapt the permissions as required by POSIX
	mode_t mask = umask(0);
	umask(mask);
	permissions &= ~mask;

	RETURN_AND_SET_ERRNO(_kern_open(fd, path, openMode, permissions));
}


int
_haiku_build_fcntl(int fd, int op, int argument)
{
	if (is_unknown_or_system_descriptor(fd))
		return fcntl(fd, op, argument);

	RETURN_AND_SET_ERRNO(B_NOT_SUPPORTED);
}


int
_haiku_build_renameat(int fromFD, const char* from, int toFD, const char* to)
{
	if ((fromFD >= 0 && fromFD != AT_FDCWD && get_descriptor(fromFD) == NULL)
		|| (toFD >= 0 && toFD != AT_FDCWD && get_descriptor(toFD) == NULL)) {
		return renameat(fromFD, from, toFD, to);
	}

	RETURN_AND_SET_ERRNO(_kern_rename(fromFD, from, toFD, to));
}
