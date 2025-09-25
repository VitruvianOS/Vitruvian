/*
 * Copyright 2018-2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <sys/utsname.h>
#include <time.h>


extern int32 __gCPUCount;


int32
is_computer_on()
{
	return 1L;
}


double
is_computer_on_fire(void)
{
	return 0.63739;
}


bigtime_t system_time() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (1000000LL * ts.tv_sec) + (ts.tv_nsec / 1000);
}


status_t
_kern_get_system_info(system_info* info)
{
	UNIMPLEMENTED();

	// TODO: getrusage + proc
	info->max_threads = 93966;

	info->cpu_count = __gCPUCount;

	struct utsname buf;
	uname(&buf);

	// TODO: Use major version number from buf.release
	info->kernel_version = 5LL;
	strcpy(info->kernel_name, buf.sysname);
	// TODO: Use date from buf.version
	strcpy(info->kernel_build_date, buf.version);
	strcpy(info->kernel_build_time, "unknown");

	// TODO: define proper ABI flags
	info->abi = 0x00200000;

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
