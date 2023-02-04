/*
 * Copyright 2018-2023, Dario Casalinuovo.
 * Distributed under the terms of the LGPL License.
 */

#include <OS.h>


ssize_t
wait_for_objects(object_wait_info* infos, int numInfos)
{
	// This function should be doable, however it doesn't seem to
	// have any use. If some day it becomes useful, feel free
	// to implement and move it back.
	UNIMPLEMENTED();
	return 0;
}


ssize_t
wait_for_objects_etc(object_wait_info* infos, int numInfos, uint32 flags,
	bigtime_t timeout)
{
	UNIMPLEMENTED();
	return 0;
}
