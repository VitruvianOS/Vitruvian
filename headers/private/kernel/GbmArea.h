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

/* A duplicate of the descriptor behind a local area mapping, or -1. The
   caller closes it. */
extern int		_kern_area_fd(area_id area);

#ifdef __cplusplus
}
#endif

#endif	/* _KERNEL_GBM_AREA_H */
