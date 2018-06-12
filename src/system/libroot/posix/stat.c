/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de. All rights reserved.
 * Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <errno.h>
#include <sys/stat.h>

#include <compat/sys/stat.h>

#include <errno_private.h>
#include <syscalls.h>
//#include <symbol_versioning.h>
#include <syscall_utils.h>


void
convert_to_stat_beos(const struct stat* stat, struct stat_beos* beosStat)
{

}


void
convert_from_stat_beos(const struct stat_beos* beosStat, struct stat* stat)
{

}

