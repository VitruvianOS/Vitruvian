/*
 * Copyright 2019, Dario Casalinuovo. All rights reserved.
 * Distributed under the terms of the LGPL License.
 */
#ifndef KERNEL_MAIN_H
#define KERNEL_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

status_t port_init();
void init_area_map();
void init_thread();

void teardown_ports();
void teardown_threads();

#ifdef __cplusplus
}
#endif

#endif
