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
_kern_snooze_etc(bigtime_t amount, int timeBase, int32 flags,
	bigtime_t* _remainingTime)
{
	// TODO: complete me

	if (_remainingTime != NULL)
		*_remainingTime = 0;

	return usleep(amount);
}


status_t
_kern_get_system_info(system_info* psInfo)
{
	UNIMPLEMENTED();

	// TODO: proc
	psInfo->max_threads = 93966;
	psInfo->kernel_version = 3LL;
	psInfo->cpu_count = __gCPUCount;

	struct utsname buf;
	uname(&buf);

	strcpy(psInfo->kernel_name, buf.sysname );
	strcpy(psInfo->kernel_build_date, buf.release );
	strcpy(psInfo->kernel_build_time, "unknown" );

	return B_OK;
}


status_t
_kern_start_watching_system(int32 object, uint32 flags,
	port_id port, int32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


status_t
_kern_stop_watching_system(int32 object, uint32 flags,
	port_id port, int32 token)
{
	UNIMPLEMENTED();
	return B_ERROR;
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
