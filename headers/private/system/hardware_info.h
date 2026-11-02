/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _HARDWARE_INFO_H
#define _HARDWARE_INFO_H


#include <SupportDefs.h>


#define B_MAX_CPU_GROUPS			8
#define B_HARDWARE_NAME_LENGTH		128


typedef struct {
	uint32			cpu_count;
	uint64			max_frequency;	/* Hz, 0 = unknown */
	char			name[B_HARDWARE_NAME_LENGTH];
} cpu_group_info;


typedef struct {
	uint32			group_count;
	cpu_group_info	groups[B_MAX_CPU_GROUPS];

	char			machine_name[B_HARDWARE_NAME_LENGTH];
	char			soc_name[B_HARDWARE_NAME_LENGTH];
	char			gpu_name[B_HARDWARE_NAME_LENGTH];

	uint64			memory_total;	/* bytes, 0 = unknown */
} hardware_info;


#ifdef __cplusplus
extern "C" {
#endif

extern status_t		get_hardware_info(hardware_info* info);

#ifdef __cplusplus
}
#endif


#endif	/* _HARDWARE_INFO_H */
