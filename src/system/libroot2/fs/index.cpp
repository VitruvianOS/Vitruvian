/*
 *  Copyright 2020, Dario Casalinuovo. All rights reserved.
 *  Distributed under the terms of the LGPL License.
 */

#include <fs_index.h>
#include <fs_attr.h>
#include <TypeConstants.h>


extern "C" status_t
fs_create_index(dev_t device, const char* name, uint32 type, uint32 flags)
{
	// Linux filesystems don't support BeFS-style indices
	// Return success (as if index already exists) to avoid breaking initialization
	return B_OK;
}


extern "C" status_t
fs_remove_index(dev_t device, const char* name)
{
	// Not supported on Linux filesystems, but return OK to avoid errors
	return B_OK;
}


extern "C" int
fs_stat_index(dev_t device, const char* name, struct index_info* indexInfo)
{
	// Pretend index exists by returning 0 (success)
	// Fill in minimal info if structure is provided
	if (indexInfo != NULL) {
		indexInfo->type = B_STRING_TYPE;
		indexInfo->size = 0;
		indexInfo->modification_time = 0;
		indexInfo->creation_time = 0;
		indexInfo->uid = 0;
		indexInfo->gid = 0;
	}
	return 0; // success
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
