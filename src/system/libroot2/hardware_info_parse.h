/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */

#ifndef _HARDWARE_INFO_PARSE_H
#define _HARDWARE_INFO_PARSE_H


/* root is "/" or a fixture directory. */


#define VOS_HW_MAX_GROUPS		8
#define VOS_HW_NAME_LENGTH		128
#define VOS_HW_MAX_CPUS			256


struct vos_hw_cpu_group {
	unsigned int			cpu_count;
	unsigned long long		max_frequency;	/* Hz, 0 = unknown */
	char					name[VOS_HW_NAME_LENGTH];
};


struct vos_hw_info {
	unsigned int			group_count;
	vos_hw_cpu_group		groups[VOS_HW_MAX_GROUPS];
	char					machine_name[VOS_HW_NAME_LENGTH];
	char					soc_name[VOS_HW_NAME_LENGTH];
	char					gpu_name[VOS_HW_NAME_LENGTH];
	unsigned long long		memory_total;	/* bytes, 0 = unknown */
	unsigned long long		memory_usable;	/* bytes, 0 = unknown */
};


int vos_hw_parse(const char* root, struct vos_hw_info* out);


#endif	/* _HARDWARE_INFO_PARSE_H */
