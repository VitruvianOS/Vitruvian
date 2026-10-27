/*
 * Copyright 2018-2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include "proc_read.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#if defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif


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
	if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0)
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
	size_t len = strlen(src);
	if (len >= destSize)
		len = destSize - 1;
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

	// One bounded line pass over meminfo; the old seven sscanf-per-line
	// chain re-scanned the whole line for each key.
	ProcLineReader meminfo;
	if (meminfo.OpenPath("/proc/meminfo")) {
		unsigned long memTotalKB = 0;
		unsigned long memAvailKB = 0;
		unsigned long memFreeKB = 0;
		unsigned long buffersKB = 0;
		unsigned long cachedKB = 0;
		unsigned long swapTotalKB = 0;
		unsigned long swapFreeKB = 0;

		const char* line;
		while ((line = meminfo.Next()) != NULL) {
			if (strncmp(line, "MemTotal:", 9) == 0)
				memTotalKB = strtoul(line + 9, NULL, 10);
			else if (strncmp(line, "MemAvailable:", 13) == 0)
				memAvailKB = strtoul(line + 13, NULL, 10);
			else if (strncmp(line, "MemFree:", 8) == 0)
				memFreeKB = strtoul(line + 8, NULL, 10);
			else if (strncmp(line, "Buffers:", 8) == 0)
				buffersKB = strtoul(line + 8, NULL, 10);
			else if (strncmp(line, "Cached:", 7) == 0)
				cachedKB = strtoul(line + 7, NULL, 10);
			else if (strncmp(line, "SwapTotal:", 10) == 0)
				swapTotalKB = strtoul(line + 10, NULL, 10);
			else if (strncmp(line, "SwapFree:", 9) == 0)
				swapFreeKB = strtoul(line + 9, NULL, 10);
		}
		meminfo.Close();

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

	// Count teams via /proc — same source _get_next_team_info enumerates —
	// so callers sizing buffers from used_teams won't truncate.
	info->used_teams = 0;
	info->used_threads = 0;

	int loadFd = open("/proc/loadavg", O_RDONLY | O_CLOEXEC);
	if (loadFd >= 0) {
		char buffer[128];
		if (proc_read_all(loadFd, buffer, sizeof(buffer)) > 0) {
			double load1, load5, load15;
			unsigned int running = 0, total = 0;
			if (sscanf(buffer, "%lf %lf %lf %u/%u", &load1, &load5, &load15,
					&running, &total) >= 5) {
				info->used_threads = total;
			}
		}
		close(loadFd);
	}

	ProcDirBatch procBatch;
	if (procBatch.Open("/proc")) {
		pid_t pid;
		while (procBatch.NextPid(&pid))
			info->used_teams++;
	}

	if (info->used_threads < info->used_teams)
		info->used_threads = info->used_teams;

	info->max_teams = info->max_threads;

	return B_OK;
}


status_t
_kern_get_kernel_memory_footprint(uint64* _bytes)
{
	if (_bytes == NULL)
		return B_BAD_VALUE;

	ProcLineReader meminfo;
	if (!meminfo.OpenPath("/proc/meminfo"))
		return B_ERROR;

	unsigned long slabKB = 0;
	unsigned long kernelStackKB = 0;
	unsigned long pageTablesKB = 0;

	const char* line;
	while ((line = meminfo.Next()) != NULL) {
		if (strncmp(line, "Slab:", 5) == 0)
			slabKB = strtoul(line + 5, NULL, 10);
		else if (strncmp(line, "KernelStack:", 12) == 0)
			kernelStackKB = strtoul(line + 12, NULL, 10);
		else if (strncmp(line, "PageTables:", 11) == 0)
			pageTablesKB = strtoul(line + 11, NULL, 10);
	}

	// VmallocUsed is excluded: it can double-count memory already backed by
	// Slab allocations.
	*_bytes = (uint64)(slabKB + kernelStackKB + pageTablesKB) * 1024ULL;
	return B_OK;
}


status_t
_kern_get_cpu_info(uint32 firstCPU, uint32 cpuCount, cpu_info* info)
{
	if (info == NULL || cpuCount == 0)
		return B_BAD_VALUE;

	memset(info, 0, sizeof(cpu_info) * cpuCount);

	long ticksPerSec = proc_clk_tck();

	ProcLineReader stat;
	if (!stat.OpenPath("/proc/stat"))
		return B_ERROR;

	const char* line;
	while ((line = stat.Next()) != NULL) {
		if (strncmp(line, "cpu", 3) != 0 || !isdigit(line[3]))
			continue;

		int cpuNum;
		uint64 user, nice, system, idle, iowait, irq, softirq, steal = 0;
		int fields = sscanf(line + 3, "%d %lu %lu %lu %lu %lu %lu %lu %lu",
			&cpuNum, &user, &nice, &system, &idle, &iowait, &irq, &softirq,
			&steal);

		if (fields < 5)
			continue;

		if (cpuNum >= (int)firstCPU && (uint32)cpuNum < firstCPU + cpuCount) {
			uint32 idx = cpuNum - firstCPU;
			if (idx < cpuCount) {
				uint64 activeJiffies = user + nice + system + irq + softirq + steal;
				info[idx].active_time = (bigtime_t)((activeJiffies * 1000000ULL) / ticksPerSec);
				info[idx].enabled = true;

				char path[128];
				unsigned long value = 0;
				snprintf(path, sizeof(path),
					"/sys/devices/system/cpu/cpu%d/online", cpuNum);
				if (proc_read_sysfs_uint(path, &value))
					info[idx].enabled = (value == 1);

				snprintf(path, sizeof(path),
					"/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq",
					cpuNum);
				if (proc_read_sysfs_uint(path, &value))
					info[idx].current_frequency = value * 1000ULL;
			}
		}
	}

	return B_OK;
}


static const char*
_SkipKeyValueSeparator(const char* p)
{
	while (*p == ' ' || *p == '\t' || *p == ':')
		p++;
	return p;
}


static uint64
read_cpu_freq(void)
{
	unsigned long khz = 0;
	if (proc_read_sysfs_uint(
			"/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", &khz)
		&& khz > 0) {
		return (uint64)khz * 1000ULL;
	}

	ProcLineReader cpuinfo;
	if (cpuinfo.OpenPath("/proc/cpuinfo")) {
		const char* line;
		while ((line = cpuinfo.Next()) != NULL) {
			if (strncmp(line, "cpu MHz", 7) == 0) {
				double mhz = strtod(_SkipKeyValueSeparator(line + 7), NULL);
				if (mhz > 0.0)
					return (uint64)(mhz * 1000000.0);
			}
		}
	}

	return 0;
}


static uint32
read_cpu_signature(void)
{
#if defined(__i386__) || defined(__x86_64__)
	unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
	if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
		return (uint32)eax;
#elif defined(__aarch64__) || defined(__arm__)
	// MIDR layout, from the first core's /proc/cpuinfo fields:
	// implementer[31:24] variant[23:20] part[15:4] revision[3:0].
	ProcLineReader cpuinfo;
	if (!cpuinfo.OpenPath("/proc/cpuinfo"))
		return 0;

	unsigned int implementer = 0, variant = 0, part = 0, revision = 0;
	int found = 0;
	const char* line;
	while (found < 4 && (line = cpuinfo.Next()) != NULL) {
		if (strncmp(line, "CPU implementer", 15) == 0) {
			implementer = (unsigned int)strtoul(
				_SkipKeyValueSeparator(line + 15), NULL, 16);
			found++;
		} else if (strncmp(line, "CPU variant", 11) == 0) {
			variant = (unsigned int)strtoul(
				_SkipKeyValueSeparator(line + 11), NULL, 16);
			found++;
		} else if (strncmp(line, "CPU part", 8) == 0) {
			part = (unsigned int)strtoul(
				_SkipKeyValueSeparator(line + 8), NULL, 16);
			found++;
		} else if (strncmp(line, "CPU revision", 12) == 0) {
			revision = (unsigned int)strtoul(
				_SkipKeyValueSeparator(line + 12), NULL, 10);
			found++;
		}
	}

	return ((uint32)(implementer & 0xff) << 24) | ((variant & 0xf) << 20)
		| ((part & 0xfff) << 4) | (revision & 0xf);
#endif
	return 0;
}


static enum cpu_vendor
read_cpu_vendor(void)
{
	ProcLineReader cpuinfo;
	if (!cpuinfo.OpenPath("/proc/cpuinfo"))
		return B_CPU_VENDOR_UNKNOWN;

	const char* line;
	while ((line = cpuinfo.Next()) != NULL) {
		if (strncmp(line, "vendor_id", 9) != 0)
			continue;

		const char* value = line + 9;
		while (*value == ' ' || *value == '\t' || *value == ':')
			value++;

		char vid[64] = {0};
		size_t i = 0;
		while (i < sizeof(vid) - 1 && value[i] != '\0'
			&& value[i] != ' ' && value[i] != '\t' && value[i] != '\n') {
			vid[i] = value[i];
			i++;
		}

		if (strcmp(vid, "GenuineIntel") == 0)
			return B_CPU_VENDOR_INTEL;
		if (strcmp(vid, "AuthenticAMD") == 0)
			return B_CPU_VENDOR_AMD;
		return B_CPU_VENDOR_UNKNOWN;
	}

	return B_CPU_VENDOR_UNKNOWN;
}


// Topology never changes after boot: computed once, racing benignly on
// idempotent values, so no lock is held across fork.
static uint64
cached_cpu_freq()
{
	static volatile uint64 sValue = 0;

	uint64 value = __atomic_load_n(&sValue, __ATOMIC_RELAXED);
	if (value == 0) {
		value = read_cpu_freq();
		__atomic_store_n(&sValue, value, __ATOMIC_RELAXED);
	}
	return value;
}


static enum cpu_vendor
cached_cpu_vendor()
{
	static volatile int sValue = (int)B_CPU_VENDOR_UNKNOWN;
	// B_CPU_VENDOR_UNKNOWN is a legitimate result; a separate ready flag
	// keeps that from being recomputed forever.
	static volatile bool sReady = false;

	if (!sReady) {
		int value = (int)read_cpu_vendor();
		__atomic_store_n(&sValue, value, __ATOMIC_RELAXED);
		__atomic_store_n(&sReady, true, __ATOMIC_RELEASE);
	}
	return (enum cpu_vendor)__atomic_load_n(&sValue, __ATOMIC_RELAXED);
}


static uint32
cached_cpu_signature()
{
	static volatile bool sReady = false;
	static volatile uint32 sValue = 0;

	if (!sReady) {
		uint32 value = read_cpu_signature();
		__atomic_store_n(&sValue, value, __ATOMIC_RELAXED);
		__atomic_store_n(&sReady, true, __ATOMIC_RELEASE);
	}
	return __atomic_load_n(&sValue, __ATOMIC_RELAXED);
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

	uint64 freqHz = cached_cpu_freq();
	enum cpu_vendor vendor = cached_cpu_vendor();

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

	uint32 cpuSignature = cached_cpu_signature();
	for (uint32 i = 0; i < coreCount && index < nodeCount; i++, index++) {
		topologyInfos[index].id = index;
		topologyInfos[index].type = B_TOPOLOGY_CORE;
		topologyInfos[index].level = 2;
		topologyInfos[index].data.core.model = cpuSignature;
		topologyInfos[index].data.core.default_frequency = freqHz;
	}

	return B_OK;
}


bool
_kern_cpu_enabled(int32 cpu)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);

	unsigned long online = 0;
	if (!proc_read_sysfs_uint(path, &online))
		return false;
	return online == 1;
}


status_t
_kern_set_cpu_enabled(int32 cpu, bool enabled)
{
	char path[64];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/online", cpu);

	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return B_ERROR;

	char value = enabled ? '1' : '0';
	ssize_t written = write(fd, &value, 1);
	close(fd);

	if (written != 1)
		return B_ERROR;
	return B_OK;
}
