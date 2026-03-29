/*
 * Copyright 2026, Dario Casalinuovo.
 * Distributed under the terms of the LGPL License.
 */


#include <extended_system_info.h>

#include <util/KMessage.h>


namespace BPrivate {


status_t
get_extended_team_info(team_id teamID, uint32 flags, KMessage& info)
{
	char commPath[64];
	snprintf(commPath, sizeof(commPath), "/proc/%d/comm", (int)teamID);

	FILE* commFile = fopen(commPath, "r");
	if (!commFile)
		return B_ENTRY_NOT_FOUND;

	char name[256];
	if (!fgets(name, sizeof(name), commFile)) {
		fclose(commFile);
		return B_ERROR;
	}
	fclose(commFile);

	size_t len = strlen(name);
	if (len > 0 && name[len - 1] == '\n')
		name[len - 1] = '\0';

	char cwdPath[64];
	snprintf(cwdPath, sizeof(cwdPath), "/proc/%d/cwd", (int)teamID);

	char cwdBuffer[PATH_MAX];
	ssize_t cwdLen = readlink(cwdPath, cwdBuffer, sizeof(cwdBuffer) - 1);
	if (cwdLen < 0)
		return B_ERROR;
	cwdBuffer[cwdLen] = '\0';

	struct stat cwdStat;
	if (stat(cwdBuffer, &cwdStat) != 0)
		return B_ERROR;

	if (info.AddString("name", name) != B_OK)
		return B_ERROR;

	if (info.AddString("cwd path", cwdBuffer) != B_OK)
		return B_ERROR;

	if (info.AddInt32("cwd device", (int32)cwdStat.st_dev) != B_OK)
		return B_ERROR;

	if (info.AddInt64("cwd directory", (int64)cwdStat.st_ino) != B_OK)
		return B_ERROR;

	int fd = open(cwdBuffer, O_RDONLY);
	if (fd < 0)
		return B_ERROR;

	vref_id vref = create_vref(fd);
	close(fd);
	if (vref < 0)
		return B_ERROR;

	if (info.AddRef("virtual:cwd directory", get_vref_dev(), (ino_t)vref,
			name) != B_OK)
		return B_ERROR;

	return B_OK;
}


} // namespace BPrivate
