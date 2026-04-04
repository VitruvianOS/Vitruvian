/*
 * Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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


bigtime_t
system_time()
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return B_ERROR;
	return (int64_t)ts.tv_sec * 1000000 + (int64_t)(ts.tv_nsec / 1000);
}


static void
set_string(char* dest, size_t destSize, const char* src)
{
	if (dest == NULL || destSize == 0)
		return;
	if (src == NULL) {
		dest[0] = '\0';
		return;
	}
	size_t len = strnlen(src, destSize);
	if (len > 0) {
		memcpy(dest, src, len);
		dest[len] = '\0';
	} else {
		dest[0] = '\0';
	}
}


static int64
parse_kernel_version(const char* release)
{
	if (release == NULL)
		return 0;

	const char* ptr = release;
	while (*ptr != '\0' && (*ptr < '0' || *ptr > '9'))
		ptr++;

	if (*ptr == '\0')
		return 0;

	char tmp[64];
	size_t len = strcspn(ptr, ".-_ ");
	if (len >= sizeof(tmp))
		len = sizeof(tmp) - 1;
	memcpy(tmp, ptr, len);
	tmp[len] = '\0';
	return atoll(tmp);
}


static time_t
get_kernel_mtime(const char* release)
{
	if (release == NULL || *release == '\0')
		return 0;

	char path[PATH_MAX];
	struct stat st;

	const char* fmtPaths[] = {
		"/boot/vmlinuz-%s",
		"/vmlinuz-%s",
		"/boot/kernel-%s",
		NULL
	};

	for (const char** fmt = fmtPaths; *fmt != NULL; fmt++) {
		if (snprintf(path, sizeof(path), *fmt, release) >= (int)sizeof(path))
			continue;
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
			return st.st_mtime;
	}

	const char* paths[] = {
		"/boot/vmlinuz",
		"/vmlinuz",
		"/boot/kernel",
		NULL
	};

	for (const char** p = paths; *p != NULL; p++) {
		if (stat(*p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
			return st.st_mtime;
	}

	DIR* dir = opendir("/boot");
	if (dir == NULL)
		return 0;

	struct dirent* entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type != DT_REG && entry->d_type != DT_LNK
				&& entry->d_type != DT_UNKNOWN)
			continue;
		if (strstr(entry->d_name, release) == NULL)
			continue;
		if (snprintf(path, sizeof(path), "/boot/%s", entry->d_name)
				>= (int)sizeof(path))
			continue;
		if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
			closedir(dir);
			return st.st_mtime;
		}
	}
	closedir(dir);
	return 0;
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

	struct utsname utsBuf;
	memset(&utsBuf, 0, sizeof(utsBuf));

	if (uname(&utsBuf) == 0) {
		set_string(info->kernel_name, sizeof(info->kernel_name),
			utsBuf.sysname);
		info->kernel_version = parse_kernel_version(utsBuf.release);

		time_t kernelMtime = get_kernel_mtime(utsBuf.release);
		if (kernelMtime != 0) {
			struct tm tm;
			if (gmtime_r(&kernelMtime, &tm) != NULL) {
				strftime(info->kernel_build_date,
					sizeof(info->kernel_build_date), "%Y-%m-%d", &tm);
				strftime(info->kernel_build_time,
					sizeof(info->kernel_build_time), "%H:%M:%S UTC", &tm);
			} else {
				set_string(info->kernel_build_date,
					sizeof(info->kernel_build_date), "unknown");
				set_string(info->kernel_build_time,
					sizeof(info->kernel_build_time), "unknown");
			}
		} else {
			set_string(info->kernel_build_date,
				sizeof(info->kernel_build_date), utsBuf.version);
			set_string(info->kernel_build_time,
				sizeof(info->kernel_build_time), "");
		}
	} else {
		set_string(info->kernel_name, sizeof(info->kernel_name), "unknown");
		info->kernel_version = 0;
		set_string(info->kernel_build_date, sizeof(info->kernel_build_date),
			"unknown");
		set_string(info->kernel_build_time, sizeof(info->kernel_build_time),
			"unknown");
	}

	info->abi = 0x002000000;

	FILE* file = fopen("/proc/meminfo", "r");
	if (file != NULL) {
		char line[128];
		unsigned long memTotalKB = 0;
		unsigned long memAvailKB = 0;
		unsigned long memFreeKB = 0;
		unsigned long buffersKB = 0;
		unsigned long cachedKB = 0;
		unsigned long swapTotalKB = 0;
		unsigned long swapFreeKB = 0;

		while (fgets(line, sizeof(line), file) != NULL) {
			unsigned long value = 0;
			if (sscanf(line, "MemTotal: %lu kB", &value) == 1)
				memTotalKB = value;
			else if (sscanf(line, "MemAvailable: %lu kB", &value) == 1)
				memAvailKB = value;
			else if (sscanf(line, "MemFree: %lu kB", &value) == 1)
				memFreeKB = value;
			else if (sscanf(line, "Buffers: %lu kB", &value) == 1)
				buffersKB = value;
			else if (sscanf(line, "Cached: %lu kB", &value) == 1)
				cachedKB = value;
			else if (sscanf(line, "SwapTotal: %lu kB", &value) == 1)
				swapTotalKB = value;
			else if (sscanf(line, "SwapFree: %lu kB", &value) == 1)
				swapFreeKB = value;
		}
		fclose(file);

		if (memTotalKB > 0) {
			uint64 pageSize = B_PAGE_SIZE;
			info->max_pages = (uint64)memTotalKB * 1024ULL / pageSize;
			uint64 freeKB = (memAvailKB > 0)
				? memAvailKB
				: (memFreeKB + buffersKB + cachedKB);
			uint64 freePages = freeKB * 1024ULL / pageSize;
			info->used_pages = (info->max_pages > freePages)
				? info->max_pages - freePages : 0;
			info->cached_pages = (uint64)(buffersKB + cachedKB) * 1024ULL
				/ pageSize;
		}

		if (swapTotalKB > 0) {
			uint64 pageSize = B_PAGE_SIZE;
			info->max_swap_pages = (uint64)swapTotalKB * 1024ULL / pageSize;
			info->free_swap_pages = (uint64)swapFreeKB * 1024ULL / pageSize;
		}
	}

	return B_OK;
}


status_t
_kern_get_cpu_info(uint32 firstCPU, uint32 cpuCount, cpu_info* info)
{
	if (info == NULL || cpuCount == 0)
		return B_BAD_VALUE;

	memset(info, 0, sizeof(cpu_info) * cpuCount);

	static long ticksPerSec = 0;
	if (ticksPerSec == 0)
		ticksPerSec = sysconf(_SC_CLK_TCK);
	if (ticksPerSec <= 0)
		ticksPerSec = 100;

	FILE* file = fopen("/proc/stat", "r");
	if (file == NULL)
		return B_ERROR;

	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		if (strncmp(line, "cpu", 3) != 0 || !isdigit(line[3]))
			continue;

		int cpuNum;
		uint64 user, nice, system, idle, iowait, irq, softirq, steal = 0;
		int fields = sscanf(line + 3, "%d %lu %lu %lu %lu %lu %lu %lu %lu",
			&cpuNum, &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);

		if (fields < 5)
			continue;

		if (cpuNum >= (int)firstCPU && (uint32)cpuNum < firstCPU + cpuCount) {
			uint32 idx = cpuNum - firstCPU;
			if (idx < cpuCount) {
				uint64 activeJiffies = user + nice + system + irq + softirq + steal;
				info[idx].active_time = (bigtime_t)((activeJiffies * 1000000ULL) / ticksPerSec);
				info[idx].enabled = true;

				char path[64];
				snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpuNum);
				FILE* online = fopen(path, "r");
				if (online) {
					int onlineVal;
					if (fscanf(online, "%d", &onlineVal) == 1)
						info[idx].enabled = (onlineVal == 1);
					fclose(online);
				}

				snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpuNum);
				FILE* freq = fopen(path, "r");
				if (freq) {
					unsigned long khz;
					if (fscanf(freq, "%lu", &khz) == 1)
						info[idx].current_frequency = khz * 1000ULL;
					fclose(freq);
				}
			}
		}
	}

	fclose(file);
	return B_OK;
}


static uint64
read_cpu_freq(void)
{
	FILE* file = fopen(
		"/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
	if (file != NULL) {
		unsigned long khz = 0;
		if (fscanf(file, "%lu", &khz) == 1) {
			fclose(file);
			return (uint64)khz * 1000ULL;
		}
		fclose(file);
	}

	file = fopen("/proc/cpuinfo", "r");
	if (file != NULL) {
		char line[256];
		while (fgets(line, sizeof(line), file) != NULL) {
			double mhz = 0;
			if (sscanf(line, "cpu MHz : %lf", &mhz) == 1
					|| sscanf(line, "cpu MHz\t: %lf", &mhz) == 1) {
				fclose(file);
				return (uint64)(mhz * 1000000.0);
			}
		}
		fclose(file);
	}

	return 0;
}


static enum cpu_vendor
read_cpu_vendor(void)
{
	FILE* file = fopen("/proc/cpuinfo", "r");
	if (file == NULL)
		return B_CPU_VENDOR_UNKNOWN;

	char line[256];
	while (fgets(line, sizeof(line), file) != NULL) {
		char vid[64] = {0};
		if (sscanf(line, "vendor_id : %63s", vid) == 1
				|| sscanf(line, "vendor_id\t: %63s", vid) == 1) {
			fclose(file);
			if (strcmp(vid, "GenuineIntel") == 0)
				return B_CPU_VENDOR_INTEL;
			if (strcmp(vid, "AuthenticAMD") == 0)
				return B_CPU_VENDOR_AMD;
			return B_CPU_VENDOR_UNKNOWN;
		}
	}
	fclose(file);
	return B_CPU_VENDOR_UNKNOWN;
}


status_t
_kern_get_cpu_topology_info(cpu_topology_node_info* topologyInfos,
	uint32* topologyInfoCount)
{
	if (topologyInfoCount == NULL)
		return B_BAD_VALUE;

	uint32 coreCount = __gCPUCount;
	if (coreCount == 0)
		coreCount = 1;

	uint32 nodeCount = 2 + coreCount;

	if (topologyInfos == NULL) {
		*topologyInfoCount = nodeCount;
		return B_OK;
	}

	if (*topologyInfoCount < nodeCount)
		nodeCount = *topologyInfoCount;
	*topologyInfoCount = nodeCount;

	memset(topologyInfos, 0, nodeCount * sizeof(cpu_topology_node_info));

	uint64 freqHz = read_cpu_freq();
	enum cpu_vendor vendor = read_cpu_vendor();

	uint32 index = 0;

	if (index < nodeCount) {
		topologyInfos[index].id = index;
		topologyInfos[index].type = B_TOPOLOGY_ROOT;
		topologyInfos[index].level = 0;
#if defined(__aarch64__)
		topologyInfos[index].data.root.platform = B_CPU_ARM_64;
#elif defined(__arm__)
		topologyInfos[index].data.root.platform = B_CPU_ARM;
#elif defined(__riscv)
	#if __riscv_xlen == 64
		topologyInfos[index].data.root.platform = B_CPU_RISC_V;
	#else
		topologyInfos[index].data.root.platform = B_CPU_UNKNOWN;
	#endif
#elif defined(__x86_64__)
		topologyInfos[index].data.root.platform = B_CPU_x86_64;
#elif defined(__i386__)
		topologyInfos[index].data.root.platform = B_CPU_x86;
#else
		topologyInfos[index].data.root.platform = B_CPU_UNKNOWN;
#endif
		index++;
	}

	if (index < nodeCount) {
		topologyInfos[index].id = index;
		topologyInfos[index].type = B_TOPOLOGY_PACKAGE;
		topologyInfos[index].level = 1;
		topologyInfos[index].data.package.vendor = vendor;
		topologyInfos[index].data.package.cache_line_size = 64;
		index++;
	}

	for (uint32 i = 0; i < coreCount && index < nodeCount; i++, index++) {
		topologyInfos[index].id = index;
		topologyInfos[index].type = B_TOPOLOGY_CORE;
		topologyInfos[index].level = 2;
		topologyInfos[index].data.core.model = 0;
		topologyInfos[index].data.core.default_frequency = freqHz;
	}

	return B_OK;
}


bool
_kern_cpu_enabled(int32 cpu)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);

	FILE* file = fopen(path, "r");
	if (file == NULL)
		return false;

	int online;
	if (fscanf(file, "%d", &online) != 1) {
		fclose(file);
		return false;
	}
	fclose(file);
	return online == 1;
}


status_t
_kern_set_cpu_enabled(int32 cpu, bool enabled)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);

	FILE* file = fopen(path, "w");
	if (file == NULL)
		return B_ERROR;

	int result = fprintf(file, "%d", enabled ? 1 : 0);
	fclose(file);

	if (result < 0)
		return B_ERROR;
	return B_OK;
}
