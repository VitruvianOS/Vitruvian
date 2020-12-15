/*
 * Copyright 2018-2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <syscalls.h>

#include <sys/time.h>
#include <sys/utsname.h>


extern int32 __gCPUCount;


int32
_kern_is_computer_on()
{
	return 1L;
}


bigtime_t
system_time()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (1000000LL * tv.tv_sec) + tv.tv_usec;	
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
