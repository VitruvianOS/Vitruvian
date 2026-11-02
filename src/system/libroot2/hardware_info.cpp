/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#include <pthread.h>
#include <string.h>

#include <private/system/hardware_info.h>

#include "hardware_info_parse.h"


static pthread_once_t sOnce = PTHREAD_ONCE_INIT;
static hardware_info sInfo;


static void
load_hardware_info()
{
	vos_hw_info parsed;
	memset(&parsed, 0, sizeof(parsed));
	vos_hw_parse("/", &parsed);

	memset(&sInfo, 0, sizeof(sInfo));
	sInfo.group_count = parsed.group_count;
	if (sInfo.group_count > B_MAX_CPU_GROUPS)
		sInfo.group_count = B_MAX_CPU_GROUPS;

	for (uint32 i = 0; i < sInfo.group_count; i++) {
		sInfo.groups[i].cpu_count = parsed.groups[i].cpu_count;
		sInfo.groups[i].max_frequency = parsed.groups[i].max_frequency;
		memcpy(sInfo.groups[i].name, parsed.groups[i].name,
			B_HARDWARE_NAME_LENGTH);
		sInfo.groups[i].name[B_HARDWARE_NAME_LENGTH - 1] = '\0';
	}

	memcpy(sInfo.machine_name, parsed.machine_name,
		B_HARDWARE_NAME_LENGTH);
	sInfo.machine_name[B_HARDWARE_NAME_LENGTH - 1] = '\0';
	memcpy(sInfo.soc_name, parsed.soc_name, B_HARDWARE_NAME_LENGTH);
	sInfo.soc_name[B_HARDWARE_NAME_LENGTH - 1] = '\0';
	memcpy(sInfo.gpu_name, parsed.gpu_name, B_HARDWARE_NAME_LENGTH);
	sInfo.gpu_name[B_HARDWARE_NAME_LENGTH - 1] = '\0';

	sInfo.memory_total = parsed.memory_total;
}


extern "C" status_t
get_hardware_info(hardware_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	pthread_once(&sOnce, load_hardware_info);
	*info = sInfo;
	return B_OK;
}
