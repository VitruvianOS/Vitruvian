/*
 * Copyright 2018-2025, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <stdlib.h>
#include <dirent.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>


extern int32 __gCPUCount;


int32
is_computer_on()
{
	return 1L;
}


double
is_computer_on_fire()
{
	return 0.63739;
}


bigtime_t system_time() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return B_ERROR;
    return (int64_t)ts.tv_sec * (int64_t)1000000 + (int64_t)(ts.tv_nsec / 1000);
}


status_t
_kern_get_system_info(system_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	memset(info, 0, sizeof(*info));

	struct rlimit rl;
	if (getrlimit(RLIMIT_NPROC, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
		info->max_threads = (int32_t)rl.rlim_cur;
	else
		info->max_threads = 65536;

	info->cpu_count = __gCPUCount;

	struct utsname buf;
	if (uname(&buf) == 0) {
		strncpy(info->kernel_name, buf.sysname, sizeof(info->kernel_name) - 1);
		info->kernel_name[sizeof(info->kernel_name) - 1] = '\0';

		// kernel_version: parse major version from release
		const char *p = buf.release;
		// skip non-digits
		while (*p && (*p < '0' || *p > '9')) ++p;
		if (*p) {
			char tmp[64];
			size_t n = strcspn(p, ".-_ ");
			if (n >= sizeof(tmp))
				n = sizeof(tmp) - 1;
			memcpy(tmp, p, n);
			tmp[n] = '\0';
			info->kernel_version = atoll(tmp);
		} else {
			info->kernel_version = 0;
		}

		time_t mtime = 0;
		int hasMtime = 0;

		// Try common candidate paths that include the release string
		const char *candidates_fmt[] = {
			"/boot/vmlinuz-%s",
			"/vmlinuz-%s",
			"/boot/kernel-%s",
			NULL
		};
		char path[PATH_MAX];
		for (const char **pf = candidates_fmt; !hasMtime && *pf; ++pf) {
			if (snprintf(path, sizeof(path), *pf, buf.release) >= (int)sizeof(path))
				continue;
			struct stat st;
			if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
				mtime = st.st_mtime;
				hasMtime = 1;
				break;
			}
		}

		// Try common non-formatted locations
		if (!hasMtime) {
			const char *candidates[] = {
				"/boot/vmlinuz",
				"/vmlinuz",
				"/boot/kernel",
				NULL
			};
			for (const char **p = candidates; !hasMtime && *p; ++p) {
				struct stat st;
				if (stat(*p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
					mtime = st.st_mtime;
					hasMtime = 1;
				}
			}
		}

		// If still not found, scan /boot for files containing the release substring
		if (!hasMtime) {
			DIR *d = opendir("/boot");
			if (d) {
				struct dirent *ent;
				while ((ent = readdir(d)) != NULL) {
					if (ent->d_type != DT_REG && ent->d_type != DT_LNK && ent->d_type != DT_UNKNOWN)
						continue;
					if (strstr(ent->d_name, buf.release) == NULL)
						continue;
					if (snprintf(path, sizeof(path), "/boot/%s", ent->d_name) >= (int)sizeof(path))
						continue;
					struct stat st;
					if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
						mtime = st.st_mtime;
						hasMtime = 1;
						break;
					}
				}
				closedir(d);
			}
		}

		// Format build date/time from mtime if found
		if (hasMtime) {
			struct tm tm;
			if (gmtime_r(&mtime, &tm) != NULL) {
				strftime(info->kernel_build_date, sizeof(info->kernel_build_date),
					"%Y-%m-%d", &tm);
				strftime(info->kernel_build_time, sizeof(info->kernel_build_time),
			         "%H:%M:%S UTC", &tm);
			} else {
				strncpy(info->kernel_build_date, "unknown", sizeof(info->kernel_build_date) - 1);
				info->kernel_build_date[sizeof(info->kernel_build_date) - 1] = '\0';
				strncpy(info->kernel_build_time, "unknown", sizeof(info->kernel_build_time) - 1);
				info->kernel_build_time[sizeof(info->kernel_build_time) - 1] = '\0';
			}
		} else {
			// Fallback: copy uname().version to build_date (heuristic)
			strncpy(info->kernel_build_date, buf.version, sizeof(info->kernel_build_date) - 1);
			info->kernel_build_date[sizeof(info->kernel_build_date) - 1] = '\0';
			strncpy(info->kernel_build_time, "unknown", sizeof(info->kernel_build_time) - 1);
			info->kernel_build_time[sizeof(info->kernel_build_time) - 1] = '\0';
		}
	} else {
		// uname failed, fill defaults
		strncpy(info->kernel_name, "unknown", sizeof(info->kernel_name) - 1);
		info->kernel_name[sizeof(info->kernel_name) - 1] = '\0';
		info->kernel_version = 0;
		strncpy(info->kernel_build_date, "unknown", sizeof(info->kernel_build_date) - 1);
		info->kernel_build_date[sizeof(info->kernel_build_date) - 1] = '\0';
		strncpy(info->kernel_build_time, "unknown", sizeof(info->kernel_build_time) - 1);
		info->kernel_build_time[sizeof(info->kernel_build_time) - 1] = '\0';
	}

	info->abi = 0x002000000;

	return B_OK;
}


status_t
_kern_get_cpu_info(uint32 firstCPU, uint32 cpuCount, cpu_info* info)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_get_cpu_topology_info(cpu_topology_node_info* topologyInfos,
	uint32* topologyInfoCount)
{
	UNIMPLEMENTED();
	return B_ERROR;
}
