/*
 * Copyright 2026, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_GBM_AREA_H
#define _KERNEL_GBM_AREA_H


#include <OS.h>


#ifdef __cplusplus
extern "C" {
#endif

/* Registers a GBM dma-buf as an area of the GBM kind. The descriptor stays
   owned by the caller. Reserved for app_server; clients only clone the id. */
extern area_id	_kern_create_gbm_area(const char* name, void** startAddress,
					uint32 addressSpec, size_t size, uint32 lock,
					uint32 protection, int fd);

#ifdef __cplusplus
}
#endif

#endif	/* _KERNEL_GBM_AREA_H */
