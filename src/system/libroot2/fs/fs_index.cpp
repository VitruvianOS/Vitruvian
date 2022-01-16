/*
 *  Copyright 2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */


#include <fs_attr.h>


extern "C" status_t
fs_create_index(dev_t device, const char* name, uint32 type, uint32 flags)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


extern "C" status_t
fs_remove_index(dev_t device, const char* name)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


extern "C" int
fs_stat_index(dev_t device, const char* name, struct index_info* indexInfo)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


extern "C" DIR*
fs_open_index_dir(dev_t device)
{
	UNIMPLEMENTED();
	return 0;
}


extern "C" int
fs_close_index_dir(DIR* dir)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


extern "C" struct dirent*
fs_read_index_dir(DIR* dir)
{
	UNIMPLEMENTED();
	return NULL;
}
