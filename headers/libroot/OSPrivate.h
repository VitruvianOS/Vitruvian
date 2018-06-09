/*
 * Copyright 2004-2009, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PRIVATE_OS_H
#define _PRIVATE_OS_H

//! Private kernel kit functions

#include <OS.h>


#ifdef __cplusplus
extern "C" {
#endif

extern int dump_sem_info(int argc, char **argv);
extern int dump_port_info(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* _OS_H */
