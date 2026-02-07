/*
 * Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include "LinuxVolume.h"

#include <fs_info.h>
#include <syscalls.h>

#include "fs/utils.h"
#include "KernelDebug.h"

#define WRITE_FLAG_READONLY 0x1
#define WRITE_DEVICE_NAME   0x2


namespace BKernelPrivate {


static FILE* sVolumeIterator = NULL;


status_t
LinuxVolume::FillVolumeInfo(struct mntent* mountEntry, fs_info* info)
{
	if (mountEntry == NULL || info == NULL)
		return B_BAD_VALUE;

	memset(info, 0, sizeof(fs_info));

	const char* volName = strrchr(mountEntry->mnt_dir, '/');
	if (volName && volName[1] != '\0')
		volName++;
	else
		volName = mountEntry->mnt_dir;

	strlcpy(info->volume_name, volName, B_FILE_NAME_LENGTH);
	strlcpy(info->device_name, mountEntry->mnt_fsname, 128);
	strlcpy(info->fsh_name, mountEntry->mnt_type, B_OS_NAME_LENGTH);

	struct statvfs volume;
	if (statvfs(mountEntry->mnt_dir, &volume) != 0)
		return -errno;

	struct stat st;
	if (stat(mountEntry->mnt_dir, &st) < 0)
		return B_ENTRY_NOT_FOUND;

	info->dev = st.st_dev;
	info->root = st.st_ino;
	info->flags = 0;

	info->block_size = (int32) volume.f_bsize;
	info->io_size = (int32) (volume.f_frsize ? volume.f_frsize : volume.f_bsize);
	info->total_blocks = (uint64) volume.f_blocks;
	info->free_blocks = (uint64) volume.f_bavail;
	info->total_nodes = (uint64) volume.f_files;
	info->free_nodes = (uint64) volume.f_favail;
	
	if (volume.f_flag & ST_RDONLY)
		info->flags |= B_FS_IS_READONLY;
	if (volume.f_flag & ST_NOSUID)
		info->flags |= B_FS_HAS_MIME;

	const char* devName = strrchr(mountEntry->mnt_fsname, '/');
	if (devName)
		devName++;
	else
		devName = mountEntry->mnt_fsname;

	if (devName[0] != '\0' && is_removable_device(devName))
		info->flags |= B_FS_IS_REMOVABLE;

	return B_OK;
}


struct mntent*
LinuxVolume::FindVolume(dev_t device)
{
	FILE* mounts = setmntent(PROC_MOUNTS, "r");
	if (mounts == NULL)
		return NULL;

	struct mntent* mountEntry;
	while ((mountEntry = getmntent(mounts)) != NULL) {
		struct stat st;
		if (stat(mountEntry->mnt_dir, &st) == 0 && st.st_dev == device) {
			struct mntent* result = (struct mntent*)malloc(sizeof(struct mntent));
			if (result) {
				result->mnt_fsname = strdup(mountEntry->mnt_fsname);
				result->mnt_dir = strdup(mountEntry->mnt_dir);
				result->mnt_type = strdup(mountEntry->mnt_type);
				result->mnt_opts = strdup(mountEntry->mnt_opts);
				result->mnt_freq = mountEntry->mnt_freq;
				result->mnt_passno = mountEntry->mnt_passno;
			}
			endmntent(mounts);
			return result;
		}
	}

	endmntent(mounts);
	return NULL;
}


void
LinuxVolume::FreeVolumeEntry(struct mntent* entry)
{
	if (entry) {
		free(entry->mnt_fsname);
		free(entry->mnt_dir);
		free(entry->mnt_type);
		free(entry->mnt_opts);
		free(entry);
	}
}


dev_t
LinuxVolume::GetNextVolume(int32* cookie)
{
	if (cookie == NULL)
		return B_BAD_VALUE;

	if (*cookie == 0 || sVolumeIterator == NULL) {
		if (sVolumeIterator != NULL)
			endmntent(sVolumeIterator);

		sVolumeIterator = setmntent(PROC_MOUNTS, "r");
		if (sVolumeIterator == NULL)
			return B_ERROR;
	}

	struct mntent* mountEntry;
	while ((mountEntry = getmntent(sVolumeIterator)) != NULL) {
		if (strcmp(mountEntry->mnt_type, "proc") == 0 ||
			strcmp(mountEntry->mnt_type, "sysfs") == 0 ||
			strcmp(mountEntry->mnt_type, "devtmpfs") == 0 ||
			strcmp(mountEntry->mnt_type, "devpts") == 0 ||
			strcmp(mountEntry->mnt_type, "cgroup") == 0 ||
			strcmp(mountEntry->mnt_type, "cgroup2") == 0 ||
			strcmp(mountEntry->mnt_type, "securityfs") == 0 ||
			strcmp(mountEntry->mnt_type, "pstore") == 0 ||
			strcmp(mountEntry->mnt_type, "efivarfs") == 0 ||
			strcmp(mountEntry->mnt_type, "bpf") == 0 ||
			strcmp(mountEntry->mnt_type, "tracefs") == 0 ||
			strcmp(mountEntry->mnt_type, "debugfs") == 0 ||
			strcmp(mountEntry->mnt_type, "configfs") == 0 ||
			strcmp(mountEntry->mnt_type, "fusectl") == 0 ||
			strcmp(mountEntry->mnt_type, "hugetlbfs") == 0 ||
			strcmp(mountEntry->mnt_type, "mqueue") == 0 ||
			strcmp(mountEntry->mnt_type, "autofs") == 0 ||
			strcmp(mountEntry->mnt_type, "rpc_pipefs") == 0 ||
			strcmp(mountEntry->mnt_type, "nfsd") == 0) {
			continue;
		}

		struct stat st;
		if (stat(mountEntry->mnt_dir, &st) < 0)
			continue;

		(*cookie)++;
		return st.st_dev;
	}

	endmntent(sVolumeIterator);
	sVolumeIterator = NULL;
	*cookie = -1;
	return B_BAD_VALUE;
}


} /* namespace BKernelPrivate */


using namespace BKernelPrivate;


dev_t
next_dev(int32* cookie)
{
	CALLED();

	if (cookie == NULL || *cookie < 0)
		return B_BAD_VALUE;

	return LinuxVolume::GetNextVolume(cookie);
}


status_t
fs_stat_dev(dev_t device, fs_info* info)
{
	CALLED();

	if (device < 0 || info == NULL)
		return B_BAD_VALUE;

	struct mntent* entry = LinuxVolume::FindVolume(device);
	if (entry == NULL)
		return B_BAD_VALUE;

	status_t result = LinuxVolume::FillVolumeInfo(entry, info);
	LinuxVolume::FreeVolumeEntry(entry);

	return result;
}


dev_t
dev_for_path(const char* path)
{
	if (path == NULL)
		return B_BAD_VALUE;

	struct stat st;
	if (stat(path, &st) < 0)
		return B_ENTRY_NOT_FOUND;

	return st.st_dev;
}


status_t
_kern_read_fs_info(dev_t device, fs_info* info)
{
	return fs_stat_dev(device, info);
}


status_t
_kern_write_fs_info(dev_t device, const struct fs_info* info, int mask)
{
#if 0
	if (info == NULL)
		return B_BAD_VALUE;

	if ((mask & (WRITE_FLAG_READONLY | WRITE_DEVICE_NAME)) == 0)
		return B_OK;

	if (geteuid() != 0)
		return B_NOT_ALLOWED;

	const char* fstab_path = "/etc/fstab";
	char tmp_path[PATH_MAX];

	snprintf(tmp_path, sizeof(tmp_path), "%s.%ld.tmp",
		fstab_path, (long)getpid());

	FILE* in = fopen(fstab_path, "r");
	if (!in)
		return -errno;

	FILE* out = fopen(tmp_path, "w");
	if (!out) {
		int saved = -errno;
		fclose(in);
		return saved;
	}

	status_t ret_status = B_OK;
	char* line = NULL;
	size_t linelen = 0;

	while (getline(&line, &linelen, in) != -1) {
		char* p = line;
		while (*p == ' ' || *p == '\t') ++p;
		if (*p == '#' || *p == '\n' || *p == '\0') {
			fputs(line, out);
			continue;
		}

		char* copy = strdup(line);
		if (!copy) {
			ret_status = B_NO_MEMORY;
			break;
		}

		char* saveptr = NULL;
		char* fs_spec = strtok_r(copy, " \t\n", &saveptr);
		char* fs_file = strtok_r(NULL, " \t\n", &saveptr);
		char* fs_vfstype = strtok_r(NULL, " \t\n", &saveptr);
		char* fs_mntops = strtok_r(NULL, " \t\n", &saveptr);
		char* fs_freq = strtok_r(NULL, " \t\n", &saveptr);
		char* fs_passno = strtok_r(NULL, " \t\n", &saveptr);

		if (!fs_spec || !fs_file) {
			fputs(line, out);
			free(copy);
			continue;
		}

		struct stat st;
		if (stat(fs_file, &st) != 0) {
			fputs(line, out);
			free(copy);
			continue;
		}

		if (st.st_dev == device) {
			char new_fs_spec[sizeof(((fs_info*)0)->device_name)];
			char new_mntops[1024] = {0};

			if ((mask & WRITE_DEVICE_NAME) && info->device_name[0] != '\0') {
				strncpy(new_fs_spec, info->device_name, sizeof(new_fs_spec) - 1);
				new_fs_spec[sizeof(new_fs_spec) - 1] = '\0';
			} else {
				strncpy(new_fs_spec, fs_spec, sizeof(new_fs_spec) - 1);
				new_fs_spec[sizeof(new_fs_spec) - 1] = '\0';
			}

			int want_ro = (info->flags & B_FS_IS_READONLY) ? 1 : 0;

			if (fs_mntops) {
				char* opts_dup = strdup(fs_mntops);
				if (!opts_dup) {
					free(copy);
					ret_status = ENOMEM;
					break;
				}

				char* tok, *o_save;
				for (tok = strtok_r(opts_dup, ",", &o_save); tok;
						tok = strtok_r(NULL, ",", &o_save)) {

					if (strcmp(tok, "ro") == 0 || strcmp(tok, "rw") == 0)
						continue;

					if (new_mntops[0] != '\0')
						strlcat(new_mntops, ",", sizeof(new_mntops));

					strlcat(new_mntops, tok, sizeof(new_mntops));
				}
				free(opts_dup);
			}

			if (mask & WRITE_FLAG_READONLY) {
				if (new_mntops[0] != '\0')
					strlcat(new_mntops, ",", sizeof(new_mntops));

				strlcat(new_mntops, want_ro ? "ro" : "rw", sizeof(new_mntops));
			} else {
				if (fs_mntops)
					strlcpy(new_mntops, fs_mntops, sizeof(new_mntops));
			}

			fprintf(out, "%s\t%s\t%s\t%s\t%s\t%s\n",
				new_fs_spec,
				fs_file,
				fs_vfstype ? fs_vfstype : "-",
				new_mntops[0] ? new_mntops : "-",
				fs_freq ? fs_freq : "0",
				fs_passno ? fs_passno : "0");

			free(copy);
		} else {
			fputs(line, out);
			free(copy);
		}
	}

	free(line);
	fclose(in);

	if (ret_status != B_OK) {
		fclose(out);
		unlink(tmp_path);
		return ret_status;
	}

	if (fflush(out) != 0 || fsync(fileno(out)) != 0 || fclose(out) != 0) {
		unlink(tmp_path);
		return -errno;
	}

	if (rename(tmp_path, fstab_path) != 0) {
		unlink(tmp_path);
		return -errno;
	}

	return B_OK;
#endif

	UNIMPLEMENTED();
	return B_NOT_SUPPORTED;
}


dev_t
_kern_next_device(int32* cookie)
{
	return next_dev(cookie);
}
