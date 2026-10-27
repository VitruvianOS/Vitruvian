/*
 * Copyright 2026, Dario Casalinuovo.
 * Distributed under the terms of the LGPL License.
 */


#include <extended_system_info.h>

#include <util/KMessage.h>
#include <VRefCache.h>

#include "proc_read.h"


namespace BPrivate {


status_t
get_extended_team_info(team_id teamID, uint32 flags, KMessage& info)
{
	struct proc_dir dir;
	if (!proc_dir_open_pid(&dir, (pid_t)teamID))
		return B_ENTRY_NOT_FOUND;

	char name[256];
	name[0] = '\0';

	int commFd = proc_dir_open_file(&dir, "comm");
	if (commFd >= 0) {
		if (proc_read_all(commFd, name, sizeof(name)) <= 0)
			name[0] = '\0';
		close(commFd);

		size_t len = strlen(name);
		if (len > 0 && name[len - 1] == '\n')
			name[len - 1] = '\0';
	}
	if (name[0] == '\0') {
		proc_dir_close(&dir);
		return B_ERROR;
	}

	char cwdPath[64];
	snprintf(cwdPath, sizeof(cwdPath), "/proc/%d/cwd", (int)teamID);

	char cwdBuffer[PATH_MAX];
	ssize_t cwdLen = readlink(cwdPath, cwdBuffer, sizeof(cwdBuffer) - 1);
	if (cwdLen < 0) {
		proc_dir_close(&dir);
		return B_ERROR;
	}
	cwdBuffer[cwdLen] = '\0';

	// The cwd link can dangle once the target team chdirs or dies; open the
	// pinned directory's own cwd link instead of re-resolving the path.
	int cwdFd = proc_dir_open_file(&dir, "cwd");
	proc_dir_close(&dir);
	if (cwdFd < 0)
		return B_ERROR;

	struct stat cwdStat;
	if (fstat(cwdFd, &cwdStat) != 0) {
		close(cwdFd);
		return B_ERROR;
	}

	if (info.AddString("name", name) != B_OK) {
		close(cwdFd);
		return B_ERROR;
	}

	if (info.AddString("cwd path", cwdBuffer) != B_OK) {
		close(cwdFd);
		return B_ERROR;
	}

	if (info.AddInt64("cwd device", (int64)cwdStat.st_dev) != B_OK) {
		close(cwdFd);
		return B_ERROR;
	}

	if (info.AddInt64("cwd directory", (int64)cwdStat.st_ino) != B_OK) {
		close(cwdFd);
		return B_ERROR;
	}

	BPrivate::vref_handle h = BPrivate::VRefCache::AcquireFromFd(cwdFd);
	close(cwdFd);
	if (h.id < 0)
		return B_ERROR;

	status_t addErr = info.AddRef("virtual:cwd directory", get_vref_dev(),
		(ino_t)h.id, name);
	// AddRef takes its own slot via VRefCache; drop ours.
	BPrivate::VRefCache::Release(h.id, h.ticket);
	if (addErr != B_OK)
		return B_ERROR;

	return B_OK;
}


} // namespace BPrivate
